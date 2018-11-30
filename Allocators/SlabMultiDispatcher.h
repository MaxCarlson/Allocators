#pragma once
#include <vector>
#include <mutex>
#include <numeric>
#include "AllocHelpers.h"

namespace ImplSlabMulti
{
constexpr auto SUPERBLOCK_SIZE	= 1 << 20;
constexpr auto SLAB_SIZE		= 1 << 14;
constexpr auto MAX_SLAB_BLOCKS	= 65535;								// Max number of memory blocks a Slab can be divided into 
constexpr auto NUM_CACHES		= 8;
constexpr auto SMALLEST_CACHE	= 64;
constexpr auto LARGEST_CACHE	= SMALLEST_CACHE << (NUM_CACHES - 1);
constexpr auto INIT_SUPERBLOCKS = 4;									// Number of Superblocks allocated per request

static_assert(LARGEST_CACHE <= SLAB_SIZE);

using byte			= alloc::byte;
using IndexSizeT	= alloc::FindSizeT<MAX_SLAB_BLOCKS>::size_type;

auto buildCaches = [](int startSz)
{
	std::vector<int> v;
	for (int i = startSz; i <= LARGEST_CACHE; i <<= 1)
		v.emplace_back(i);
	return v;
};

auto buildBlocksPer = [](const std::vector<int>& cacheSizes)
{
	std::vector<int> v;
	for (const auto s : cacheSizes)
		v.emplace_back(SLAB_SIZE / s);
	return v;
};

const std::vector<int> cacheSizes		= buildCaches(SMALLEST_CACHE);
const std::vector<int> blocksPerSlab	= buildBlocksPer(cacheSizes);

struct GlobalDispatch
{
	using FreeIndicies = std::vector<std::pair<size_t, std::vector<IndexSizeT>>>;

	GlobalDispatch() :
		mutex{},
		blocks{},
		totalSBlocks{ 0 },
		availible{ buildIndicies() }
	{
		requestMem(INIT_SUPERBLOCKS);
	}

	byte* getBlock()
	{
		std::lock_guard lock(mutex);
		if (blocks.empty())
			requestMem(INIT_SUPERBLOCKS);
		byte* mem = blocks.back();
		blocks.pop_back();
		return mem;
	}

	void returnBlock(byte* block)
	{
		std::lock_guard lock(mutex);
		blocks.emplace_back(block);
	}

	std::vector<IndexSizeT> getIndicies(size_t blockSz) const
	{
		for (const auto bs : availible)
			if (bs.first >= blockSz)
				return bs.second;

		throw std::runtime_error("Incorrect Cache size request");
	}

private:

	void requestMem(int sblocks = 1)
	{
		for (int i = 0; i < sblocks; ++i)
		{
			byte* mem = reinterpret_cast<byte*>(operator new(SUPERBLOCK_SIZE));

			for (int idx = 0; idx < SUPERBLOCK_SIZE; idx += SLAB_SIZE)
				blocks.emplace_back(mem + idx);
		}
		totalSBlocks += sblocks;
	}

	// Build the vectors of superblock block indices
	FreeIndicies buildIndicies() const
	{
		int i = 0;
		FreeIndicies av{ static_cast<size_t>(NUM_CACHES) };
		for (auto& a : av)
		{
			a.first = cacheSizes[i];
			a.second.resize(SLAB_SIZE / a.first);
			std::iota(std::rbegin(a.second), std::rend(a.second), 0);
			++i;
		}
		return av;
	}

	std::mutex			mutex;
	std::vector<byte*>	blocks;			// TODO: Should these be kept in address sorted order to improve locality?
	int					totalSBlocks;
	const FreeIndicies	availible;
};

inline GlobalDispatch dispatcher; // TODO: Make this local to the allocator
}