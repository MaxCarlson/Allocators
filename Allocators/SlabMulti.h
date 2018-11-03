#pragma once
#include <numeric>
#include <vector>

namespace SlabMultiImpl
{

struct Cache
{
	using size_type = size_t;

	size_type count;
	size_type blockSize;

};

struct Caches
{
	using size_type = size_t;

	std::vector<Cache> caches;

	void addCache(size_type blockSize, size_type count, int index)
	{
		auto bytes = blockSize * count;
		for(auto it = std::begin(caches), 
			E = std::end(caches); it != E; ++it)
			if (bytes < it->blockSize)
			{
				caches.emplace_back(count, blockSize);
				return;
			}
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

	int threads;
	int numHeaps;
	std::vector<size_type>				blockSizes;
	std::vector<SlabMultiImpl::Caches>	caches; // TODO: Not Caches, Caches of Caches

	SlabMulti(int threads, int numHeaps) :
		threads{	threads },
		numHeaps{	numHeaps },
		caches{		numHeaps }
	{

	}

	void addCache(size_type blockSize, size_type count)
	{
		int idx		= 0;
		auto bytes	= blockSize * count;
		for(auto it = std::begin(blockSizes), 
			E = std::end(blockSizes); it != E; ++it, ++idx)
			if (bytes < *it)
			{

			}
	}

	// Add a cache of memory == (sizeof(T) * count) bytes 
	// divided into sizeof(T) byte blocks
	template<class T = Type>
	void addCache(size_type count)
	{
	}

	template<class T = Type>
	T* allocate(size_t count)
	{

	}

	template<class T = Type>
	void deallocate(T* ptr)
	{

	}
};

}