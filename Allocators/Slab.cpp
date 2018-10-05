#include "Slab.h"

namespace alloc
{
	SmallSlab::SmallSlab(size_t objSize, size_t count) : objSize(objSize), count(count), availible(count)
	{
		mem = reinterpret_cast<byte*>(operator new(objSize * count));
		std::iota(std::begin(availible), std::end(availible), 0);
	}

	std::pair<byte*, bool> SmallSlab::allocate()
	{
		if (availible.empty()) // TODO: This should never happen?
			return { nullptr, false };

		auto idx = availible.back();
		availible.pop_back();
		return { mem + (idx * objSize), availible.empty() };
	}
	

	SmallCache::SmallCache(size_type objSize, size_type count) : objSize(objSize), count(count)
	{
		newSlab();
	}

	bool SmallCache::operator<(const SmallCache & other) const noexcept
	{
		return objSize < other.objSize;
	}

	void SmallCache::newSlab()
	{
		slabsFree.emplace_back(objSize, count);
	}

	std::pair<SmallCache::SlabStore*, SmallCache::It> SmallCache::findFreeSlab()
	{
		It slabIt;
		SlabStore* store = nullptr;
		if (!slabsPart.empty())
		{
			slabIt = std::begin(slabsPart);
			store = &slabsPart;
		}
		else
		{
			// No empty slabs, need to create one! (TODO: If allowed to create?)
			if (slabsFree.empty())
				newSlab();

			slabIt = std::begin(slabsFree);
			store = &slabsFree;
		}

		return { store, slabIt };
	}
	
	
	void SlabMemInterface::addCache(size_type objSize, size_type count)
	{
		if (caches.empty())
		{
			caches.emplace_back(objSize, count);
			return;
		}

		for (It it = std::begin(caches); it != std::end(caches); ++it)
			if (objSize < it->objSize)
			{
				caches.emplace(it, objSize, count);
				break;
			}
	}

}

