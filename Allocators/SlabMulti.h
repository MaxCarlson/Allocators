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

constexpr auto SUPERBLOCK_SIZE	= 1 << 20;
constexpr auto SLAB_SIZE		= 1 << 14;
constexpr auto MAX_SLAB_SIZE	= 65535; // Max number of memory blocks a Slab can be divided into 
constexpr auto SMALLEST_CACHE	= 64;
constexpr auto LARGEST_CACHE	= 1 << 13;
constexpr auto INIT_SUPERBLOCKS = 4;

using byte			= alloc::byte;
using IndexSizeT	= alloc::FindSizeT<MAX_SLAB_SIZE>::size_type;

auto buildCaches = [](int startSz)
{
	std::vector<int> v;
	for (int i = startSz; i <= LARGEST_CACHE; i <<= 1)
		v.emplace_back(i);
	return v;
};

const std::vector<int> cacheSizes = buildCaches(SMALLEST_CACHE);

struct GlobalDispatch
{
	using FreeIndicies = std::vector<std::vector<SlabImpl::IndexSizeT>>;

	GlobalDispatch() :
		mutex{},
		superblocks{},
		availible{ buildIndicies() }
	{
		requestMem(INIT_SUPERBLOCKS);
	}
	
private:

	void requestMem(int blocks = 1)
	{
		for (int i = 0; i < blocks; ++i)
		{
			byte* mem = reinterpret_cast<byte*>(operator new(SUPERBLOCK_SIZE));
			superblocks.emplace_back(mem);
		}
	}

	// Build the vectors of superblock block indices
	FreeIndicies buildIndicies()
	{
		int i = 0;
		FreeIndicies av;
		for (auto& a : av)
		{
			auto count = SUPERBLOCK_SIZE / cacheSizes[i];
			a.resize(count);
			std::iota(std::rbegin(a), std::rend(a), 0);
			++i;
		}
		return av;
	}

	std::mutex			mutex;
	std::vector<byte*>	superblocks;
	const FreeIndicies	availible;
};

struct Slab // This is just a copy of SlabMem right now b
{
private:
	using size_type = size_t;

	byte*								mem;
	size_type							blockSize;
	size_type							count;		// TODO: This can be converted to IndexSizeT 
	std::vector<SlabImpl::IndexSizeT>	availible;

public:

	Slab(size_t blockSize, size_t count) : 
		mem{		reinterpret_cast<byte*>(operator new(blockSize * count)) }, // TODO: Request memory from GlobalDispatch
		blockSize{	blockSize }, 
		count{		count }, 
		availible{	static_cast<IndexSizeT>(count) }
	{
		std::iota(std::rbegin(availible), std::rend(availible), 0);
	}

	Slab(const Slab& other) = delete;

	Slab(Slab&& other) noexcept :
		mem{		other.mem					},
		blockSize{	other.blockSize				},
		count{		other.count					},
		availible{	std::move(other.availible)	}
	{
		other.mem = nullptr;
	}

	Slab& operator=(Slab&& other) noexcept
	{
		mem			= other.mem;
		blockSize	= other.blockSize;
		count		= other.count;
		availible	= std::move(other.availible);
		other.mem	= nullptr;
		return *this;
	}

	~Slab()
	{
		if(mem)
			operator delete(mem);
	}
		
	bool full()			const noexcept { return availible.empty(); }
	size_type size()	const noexcept { return count - availible.size(); }
	bool empty()		const noexcept { return availible.size() == count; }

	std::pair<byte*, bool> allocate() 
	{
		auto idx = availible.back();
		availible.pop_back();
		return { mem + (idx * blockSize), availible.empty() };
	}

	template<class P>
	void deallocate(P* ptr)
	{
		auto idx = static_cast<size_type>((reinterpret_cast<byte*>(ptr) - mem)) / blockSize;
		availible.emplace_back(idx);
	}

	template<class P>
	bool containsMem(P* ptr) const noexcept
	{
		return (reinterpret_cast<byte*>(ptr) >= mem
				&& reinterpret_cast<byte*>(ptr) < (mem + blockSize * count));
	}
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
				buckets.emplace(it, count, blockSize);
				return;
			}
		buckets.emplace_back(count, blockSize);
	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : buckets)
			if (c.blockSize >= bytes)
				return c.allocate();

		throw std::bad_alloc();
	}

	template<class T>
	void deallocate(T* ptr)
	{

	}

	void setup()
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
	std::atomic_flag inUse = ATOMIC_FLAG_INIT;
};
// Interface class for SlabMulti so that
// we can have multiple SlabMulti copies pointing
// to same allocator
struct Interface
{
	using size_type		= size_t;

	SmpVec<BucketPair> buckets;
	SmpVec<std::pair<size_t, size_t>> cacheSizes;

	Interface(int threads = 1) :
		buckets{ threads },
		cacheSizes{ 0 }
	{

	}

	// TODO: Also thinking about getting rid of this function all together
	// Replace with set block sizes for Caches
	//
	// TODO: If this is made non-thread safe it could improve performance
	void addCache(size_type blockSize, size_type count) 
	{
		cacheSizes.emplace_back(blockSize, count); // TODO: Not ordered
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

		byte* mem = nullptr;
		mem = buckets.siterate([&bytes, &id](BucketPair& p) -> byte*
		{
			if (p.id == id)
				return p.bucket.allocate(bytes);

			else if (!p.inUse.test_and_set())
			{
				p.id = std::this_thread::get_id();
				p.bucket.setup();
				return p.bucket.allocate(bytes);
			}
			return nullptr;
		});

		return reinterpret_cast<T*>(mem);
	}

	template<class T>
	void deallocate(T* ptr, size_type n)
	{
		// Look in this threads memory Caches first
		
		// Then other threads
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