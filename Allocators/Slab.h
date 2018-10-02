#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <array>

namespace alloc
{

	struct ObjCache
	{

	};

	struct SmallSlab
	{
		byte* mem;
		std::vector<uint16_t> availible;

		SmallSlab(size_t objSize, size_t count)
		{
			mem = reinterpret_cast<byte*>(operator new(objSize * count));
			availible.resize(count);
			for (auto i = 0; i < count; ++i)
				availible[i] = i;
		}

		byte* allocate()
		{
			auto idx = availible.back();
			availible.pop_back();
			return mem + idx;
		}

		void deallocate(byte* ptr)
		{

		}
	};


	// Cache's based on memory size
	// Not designed around particular objects
	struct SmallCache
	{
		// TODOLIST:
		// TODO: Locking mechanism
		// TODO: Page alignemnt/Page Sizes for slabs? (possible on windows?)
		
		using size_type = size_t;
		using SlabStore = std::vector<SmallSlab>;

		size_type objSize;
		size_type count;
		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		SmallCache() = default;
		SmallCache(size_type objSize, size_type count) : objSize(objSize), count(count)
		{
			newSlab(objSize, count);
		}

		bool operator<(const SmallCache& other) const noexcept
		{
			return objSize < other.objSize;
		}

		void newSlab(size_type objSize, size_type count)
		{
			slabsFree.emplace_back();
		}

		void freeEmpty()
		{

		}

	};

	struct SlabInterface
	{
		using size_type		= size_t;
		using SmallStore	= std::vector<SmallCache>;
		using It			= typename SmallStore::iterator;

		inline static SmallStore caches;

		// TODO: Should these return a Cache* so that
		// they can be easily removed by a seperate function?
		void addCache(size_type objSize, size_type count)
		{
			SmallCache ch{ objSize, count };
			if (caches.empty())
			{
				caches.emplace_back(ch);
				return;
			}

			for(It it = std::begin(caches); it != std::end(caches); ++it)
				if (ch < *it)
				{
					caches.emplace(it, ch);
					break;
				}
		}

		template<class T>
		T* allocate()
		{

		}

		template<class T>
		void deallocate(T* ptr)
		{

		}
	};

	template<class Type>
	class Slab
	{

		inline static SlabInterface storage;

	public:

		using size_type = size_t;


		// TODO: Should this be a <template T> ?
		//
		// Does not take a count argument because 
		// we can only allocate one object at once
		Type* allocate()
		{
			return storage.allocate<Type>();
		}

		void deallocate(Type* ptr)
		{
			storage.deallocate(ptr);
		}

		void addCache(size_type objSize, size_type count)
		{
			storage.addCache(objSize, count);
		}

		template<class T>
		void addCache(size_type count)
		{
			addCache(sizeof(T), count);
		}
	};
}