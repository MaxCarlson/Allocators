#pragma once
#include "AllocHelpers.h"

#include <numeric>
#include <vector>
#include <thread>

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

struct Caches
{
	using size_type = size_t;

	std::vector<Cache> caches;

	void addCache(size_type blockSize, size_type count)
	{
		auto bytes = blockSize * count;
		for(auto it = std::begin(caches), 
			E = std::end(caches); it != E; ++it)
			if (bytes < it->blockSize)
			{
				caches.emplace_back(count, blockSize);
				return;
			}
		caches.emplace_back(count, blockSize);
	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : caches)
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
	std::vector<SlabMultiImpl::Caches>	caches; // TODO: Not Caches, Caches of Caches

public:


	SlabMulti(int numHeaps) :
		numHeaps{	numHeaps },
		caches{		numHeaps }
	{

	}

	SlabMulti(SlabMulti&& other) :
		numHeaps{	numHeaps },
		caches{		std::move(caches) }
	{
	}

	void addCache(size_type blockSize, size_type count)
	{
		for (auto& c : caches)
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

		return reinterpret_cast<T*>(caches[idx].allocate(bytes)); 
	}

	template<class T = Type>
	void deallocate(T* ptr)
	{

	}
};

}