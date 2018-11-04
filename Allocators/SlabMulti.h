#pragma once
#include "AllocHelpers.h"

#include <numeric>
#include <vector>
#include <thread>

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

	template<class T>
	Bucket(alloc::SlabMulti<T>* alloc)
	{

	}

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
	int numHeaps;
	std::vector<SlabMultiImpl::Bucket>	buckets; 

public:

	template<class U>
	friend class SlabMulti;

	SlabMulti(int numHeaps) :
		numHeaps{	numHeaps },
		buckets{	numHeaps }
	{

	}

	template<class U>
	SlabMulti(SlabMulti<U>&& other) noexcept :
		numHeaps{	other.numHeaps },
		buckets{	std::move(other.buckets) }
	{
	}

	/*
	template<class U>
	SlabMulti(const SlabMulti<U>& other) noexcept :
		numHeaps{ other.numHeaps },
		buckets{ other.buckets }
	{
	}
	*/

	void addCache(size_type blockSize, size_type count)
	{
		for (auto& c : buckets)
			c.addCache(blockSize, count);
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
		std::hash<std::thread::id> hasher;

		auto bytes	= sizeof(T) * count;
		auto idx	= hasher(std::this_thread::get_id()) % numHeaps; // TODO: Require power of two size for heaps to avoid mod here?

		return reinterpret_cast<T*>(buckets[idx].allocate(bytes)); 
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{

	}
};

}