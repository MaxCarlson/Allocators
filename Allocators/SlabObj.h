#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <functional>

namespace SlabObj
{
	using byte = alloc::byte;

	template<class T>
	struct Slab
	{
		byte* mem;
		std::vector<uint16_t> availible;

		// TODO: Or just keep a Cache* to our parent?
		// TODO:+ How will we consntruct objects with the ctor if they
		// are not default constructable? 
		// TODO: Prefer to find some type of tuple/variadic init if possible

		//std::function<void(T&)>* ctor;
		//std::function<void(T&)>* dtor;

		
		Slab() = default;
		Slab(size_t count) : availible(count)
		{
			mem = reinterpret_cast<byte*>(operator new(sizeof(T) * count));
			std::iota(std::rbegin(availible), std::rend(availible), 0);

			//for(auto i = 0; i < count; ++i)
			//	::new (mem + (sizeof(T) * i)) T();
		}

		std::pair<byte*, bool> allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * sizeof(T)), availible.empty() };
		}

		void deallocate(T* ptr)
		{

		}
	};

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T>
	struct Cache
	{
		using size_type = size_t;
		using Storage	= alloc::List<Slab<T>>;
		using It		= typename Storage::iterator;

		inline static Storage slabsFull;
		inline static Storage slabsPart;
		inline static Storage slabsFree;

		inline static size_type mySize;		// Total objects
		inline static size_type perCache;	// Objects per cache
		inline static size_type myCapacity; // Total capacity for objects without more allocations

		//inline static std::function<void(T&)> ctor;
		//inline static std::function<void(T&)> dtor;

		//template<class Ctor, class Dtor>
		//static void addCache(size_type count, 
		//	Ctor& nctor, Dtor& ndtor)
		static void addCache(size_type count)
		{
			// TODO: Should we only allow this function to change count on init?
			perCache = count;
			newSlab();
		}

		static void newSlab()
		{
			slabsFree.emplace_back(perCache);
			myCapacity += perCache;
		}

		// TODO: Identical Code as SlabMem!
		static std::pair<Storage*, It> findSlab()
		{
			It slabIt;
			Storage* store = nullptr;
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

		// TODO: Identical Code as SlabMem!
		static T* allocate()
		{
			auto[store, it] = findSlab();
			auto[mem, full] = it->allocate();

			// If we're taking memory from a free slab
			// add it to the list of partially full slabs
			if (store == &slabsFree)
				store->giveNode(it, slabsPart, std::begin(slabsPart));

			// Give the slab storage to the 
			// full list if it has no more room
			if (full)
				store->giveNode(it, full, std::begin(full));

			++mySize;
			return reinterpret_cast<T*>(mem);
		}

		static void deallocate(T* ptr)
		{

		}
	};


	struct Interface
	{
		using size_type = size_t;

		template<class T>
		void addCache(size_type count)
		{
			Cache<T>::addCache(count);
		}

		template<class T>
		T* allocate()
		{
			return Cache<T>::allocate();
		}

		template<class T>
		void deallocate(T* ptr)
		{
			Cache<T>::deallocate(ptr);
		}
	};
}