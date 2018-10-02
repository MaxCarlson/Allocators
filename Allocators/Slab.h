#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <array>

namespace alloc
{

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T>
	struct ObjCache
	{

	};

	struct SmallSlab
	{
	private:
		byte* mem;
		std::vector<uint16_t> availible;
		
	public:

		SmallSlab() = default;
		SmallSlab(size_t objSize, size_t count)
		{
			mem = reinterpret_cast<byte*>(operator new(objSize * count));
			availible.resize(count);
			for (auto i = 0; i < count; ++i)
				availible[i] = i;
		}

		~SmallSlab(){ delete mem; }

		bool full() const noexcept { return availible.empty(); }

		byte* allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return nullptr;

			auto idx = availible.back();
			availible.pop_back();
			return mem + idx;
		}

		void deallocate(byte* ptr)
		{
			auto idx = static_cast<size_t>(ptr - mem);
			availible.emplace_back(idx);
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
			slabsFree.emplace_back(SmallSlab{ objSize, count });
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

		void addMemCache(size_type objSize, size_type count)
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
		void addObjCache(size_type count)
		{

		}

		template<class T>
		T* allocateMem()
		{

		}

		template<class T>
		void deallocateMem(T* ptr)
		{

		}
	};

	template<class Type>
	class Slab
	{

		inline static SlabInterface storage;

	public:

		using size_type = size_t;


		// Does not take a count argument because 
		// we can only allocate one object at a time
		template<class T = Type>
		T* allocateMem()
		{
			return storage.allocate<T>();
		}

		template<class T = Type>
		void deallocateMem(T* ptr)
		{
			storage.deallocate(ptr);
		}

		template<class T = Type>
		void addObjCache(size_type count)
		{
			storage.addObjCache<T>(count);
		}

		void addMemCache(size_type objSize, size_type count)
		{
			storage.addMemCache(objSize, count);
		}

		template<class T = Type>
		void addMemCache(size_type count)
		{
			addCache(sizeof(T), count);
		}
	};
}