#pragma once
#include <vector>
#include <numeric>
#include "SlabHelper.h"

namespace SlabMemImpl
{
using byte = alloc::byte;

// Maximum number of Caches that can be added
// to SlabMem (Used to keep the size_type in header small)
//inline constexpr auto MAX_CACHES	= 127;
// NOT in use. Could be useful in future though
/*
struct Header
{
	enum { NO_CACHE = MAX_CACHES + 1 };
	using size_type = typename alloc::FindSizeT<MAX_CACHES, 1>::size_type;
	size_type cacheIdx;
};
*/

struct Slab
{
private:
	using size_type = size_t;

	byte*								mem;
	size_type							blockSize;
	size_type							count;		// TODO: This can be converted to IndexSizeT 
	//char								offset;
	std::vector<SlabImpl::IndexSizeT>	availible;

public:

	Slab(size_t blockSize, size_t count) : 
		mem{		reinterpret_cast<byte*>(operator new(blockSize * count)) },
		blockSize{	blockSize }, 
		count{		count }, 
		availible{	SlabImpl::vecMap[count] }
	{
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
	
// Holds all the memory caches 
// of different sizes
struct Interface
{
	using size_type		= size_t;
	using SmallStore	= std::vector<SlabImpl::Cache<Slab>>;
	using It			= typename SmallStore::iterator;

	inline static SmallStore buckets;

	// TODO: Add a debug check so this function won't add any 
	// (or just any smaller than largest) caches after first allocation for safety?
	// TODO: Add a reserve parameter so it can reserve Slabs!
	//
	static void addCache(size_type blockSize, size_type count)
	{
		count = alloc::nearestPageSz(count * blockSize) / blockSize; // TODO: This causes a major issue with looking up the caches again if need be. Store their input count?
		SlabImpl::addToMap(count);
		buckets.emplace_back(blockSize, count);
	}

	static void addCache2(size_type startSz, size_type maxSz, size_type count)
	{
		for (auto i = 0; startSz <= maxSz; ++i, startSz <<= 1)
			addCache(startSz, count);
	}

	//static void addCacheFib(size_type startSz, size_type maxSz, size_type count)
	//{
	//	for (auto i = 0; startSz <= maxSz; ++i, startSz <<= 1)
	//		caches.emplace_back(startSz, count);
	//}

	template<class T>
	static T* allocate(size_t count)
	{
		// TODO: Binary search if caches is large enough?

		const auto bytes = count * sizeof(T);
		for (auto it = std::begin(buckets), 
			E = std::end(buckets); it != E; ++it)
			if (it->blockSize >= bytes)
				return it->allocate<T>();

		return reinterpret_cast<T*>(operator new(bytes));
	}

	template<class T>
	static void deallocate(T* ptr, size_type count)
	{
		const auto bytes = count * sizeof(T);
		for (auto it = std::begin(buckets), 
			E = std::end(buckets); it != E; ++it)
			if (it->blockSize >= bytes)
			{
				it->deallocate(ptr);
				return;
			}
		operator delete(ptr, count);
	}

	static std::vector<alloc::CacheInfo> info() noexcept
	{
		std::vector<alloc::CacheInfo> stats;
		for (const auto& ch : buckets)
			stats.emplace_back(ch.info());
		return stats;
	}

	template<bool all>
	static void freeFunc(size_t cacheSize)
	{
		for (auto it = std::begin(buckets), 
			E = std::end(buckets); it != E; ++it)
			if (cacheSize == 0 || it->blockSize == cacheSize)
			{
				if constexpr (all)
					it->freeAll();
				else
					it->freeEmpty();
				return;
			}
	}

	static void freeAll(size_t cacheSize)
	{
		freeFunc<true>(cacheSize);
	}

	static void freeEmpty(size_t cacheSize)
	{
		freeFunc<false>(cacheSize);
	}
};
}