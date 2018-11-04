#pragma once
#include "AllocHelpers.h"

#include <numeric>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <shared_mutex>

namespace alloc
{
template<class T>
class SlabMulti;
}


namespace SlabMultiImpl
{
using byte = alloc::byte;

struct Slab
{

};

struct Cache
{
	using size_type = size_t;

	size_type count;
	size_type blockSize;

	std::vector<Slab> slabs;

	Cache(size_type count, size_type blockSize) :
		count{		count },
		blockSize{	blockSize }
	{
	}

	byte* allocate()
	{

		return nullptr;
	}
};

// Bucket of Caches
struct Bucket
{
	using size_type = size_t;

	std::vector<Cache> buckets;

	Bucket() = default;


	void addCache(size_type blockSize, size_type count)
	{
		auto bytes = blockSize * count; // Extra work is done here for each Bucket
		for(auto it = std::begin(buckets), 
			E = std::end(buckets); it != E; ++it)
			if (bytes < it->blockSize)
			{
				buckets.emplace_back(count, blockSize);
				return;
			}
		buckets.emplace_back(count, blockSize);
	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : buckets)
			if (c.blockSize >= bytes)
				return c.allocate();
	}

	template<class T>
	void deallocate(T* ptr)
	{

	}
};

template<class T>
struct SmpVec
{
	SmpVec(int threads) :
		vec{ threads }
	{}

	template<class... Args>
	decltype(auto) emplace_back(Args&& ...args) 
	{
		std::unique_lock<std::shared_mutex>(mutex);
		return vec.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	decltype(auto) lIterate(Func&& func)
	{
		std::unique_lock<std::shared_mutex>(mutex);
		for (auto& v : vec)
			if (auto ptr = func(v))
				return ptr;
	}

	template<class Func>
	decltype(auto) siterate(Func&& func)
	{
		std::shared_lock<std::shared_mutex>(mutex);
		for (auto& v : vec)
		{
			auto ptr = func(v);
			if (ptr)
				return ptr;
		}
			
	}

private:
	std::vector<T> vec;
	mutable std::shared_mutex mutex;
};

struct BucketPair
{
	Bucket bucket;
	std::thread::id id;
	std::atomic<bool> inUse;
};
// Interface class for SlabMulti so that
// we can have multiple SlabMulti copies pointing
// to same allocator
struct Interface
{
	using size_type		= size_t;

	SmpVec<BucketPair> buckets;

	Interface(int threads = 1) :
		buckets{ threads }
	{

	}

	void addCache(size_type blockSize, size_type count) // TODO: Make this thread safe?
	{
		buckets.lIterate([&](BucketPair& p) -> byte*
		{
			p.bucket.addCache(blockSize, count);
			return nullptr;
		});
	}

	// Add a cache of memory == (sizeof(T) * count) bytes 
	// divided into sizeof(T) byte blocks
	template<class T>
	void addCache(size_type count)
	{
		addCache(sizeof(T), count);
	}

	template<class T>
	T* allocate(size_t count)
	{
		const auto bytes	= sizeof(T) * count;
		const auto id		= std::this_thread::get_id();

		byte* mem = buckets.siterate([&bytes, &id](BucketPair& p) -> byte*
		{
			if (p.id == id)
				return p.bucket.allocate(bytes);

			else if (!p.inUse)
			{
				p.bucket.setup();
				return p.bucket.allocate(bytes);
			}
			return nullptr;
		});

		return reinterpret_cast<T*>(mem);

		//std::hash<std::thread::id> hasher;
		//auto idx = hasher(std::this_thread::get_id()) % numHeaps; // TODO: Require power of two size for heaps to avoid mod here?
		//return reinterpret_cast<T*>(find->second.allocate(bytes)); 
	}

	template<class T>
	void deallocate(T* ptr, size_type n)
	{

	}
};

}// End SlabMultiImpl::

namespace alloc // Move this to Slab.h eventually
{

// TODO IDEAS:
// TODO: Should this have a bookeeping thread for other-than-alloc-operations?
// TODO: Create a global Pool of Slabs that can be passed to Caches in need 
// (that way we can treat the issue of possibly having Slabs that are never used
// in the vector of Caches)
//
template<class Type>
class SlabMulti
{
public:
	using STD_Compatible	= std::true_type;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

private:
	SlabMultiImpl::Interface* interfacePtr;

public:

	template<class U>
	friend class SlabMulti;

	SlabMulti(int threads) :
		interfacePtr{ new SlabMultiImpl::Interface{threads} }
	{

	}

	template<class U>
	SlabMulti(const SlabMulti<U>& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
	}

	template<class U>
	SlabMulti(SlabMulti<U>&& other) noexcept :
		interfacePtr{ std::move(other.interfacePtr) }
	{
		other.interfacePtr = nullptr;
	}
	
	void addCache(size_type blockSize, size_type count)
	{
		interfacePtr->addCache(blockSize, count);
	}

	// Add a cache of memory == (sizeof(T) * count) bytes 
	// divided into sizeof(T) byte blocks
	template<class T = Type>
	void addCache(size_type count)
	{
		addCache(sizeof(T), count);
	}

	template<class T = Type>
	T* allocate(size_t count)
	{
		return interfacePtr->allocate<T>(count);
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{
		interfacePtr->deallocate(ptr, n);
	}
};

}