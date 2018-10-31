#pragma once
#include "SlabHelper.h"
#include <functional>

namespace SlabObjImpl
{
	using byte = alloc::byte;

	// TODO: Look into sorted vec so we can allocate more than one at a time!
	// TODO: Slab Coloring
	// TODO: Custom Alignement?
	template<class T, class Cache, class Xtors>
	struct Slab
	{
		byte*								mem;
		size_t								count;
		std::vector<SlabImpl::IndexSizeT>	availible;
		
		Slab() : mem{ nullptr } {}
		Slab(size_t count) :
			mem{ reinterpret_cast<byte*>(operator new(sizeof(T) * count)) },
			count{ count },
			availible{ SlabImpl::vecMap[count] }
		{
			// TODO: Test coloring alingment here 
			// to prevent slabs from crowding cachelines
			for (auto i = 0; i < count; ++i)
				Cache::xtors->construct(reinterpret_cast<T*>(mem + sizeof(T) * i));
			int a = 5;
		}

		Slab(const Slab& other) = delete;

		Slab(Slab&& other) noexcept :
			mem{ other.mem },
			count{ other.count },
			availible{ std::move(availible) }
		{
			other.mem = nullptr;
		}

		Slab& operator=(Slab&& other) noexcept
		{
			mem			= other.mem;
			count		= other.count;
			availible	= std::move(other.availible);
			other.mem	= nullptr;
			return *this;
		}

		~Slab()
		{
			if (!mem)
				return;

			for (auto i = 0; i < count; ++i)
				reinterpret_cast<T*>(mem + sizeof(T) * i)->~T();

			operator delete(reinterpret_cast<void*>(mem));
		}

		bool full()			const noexcept { return availible.empty(); }
		size_t size()		const noexcept { return count - availible.size(); }
		bool empty()		const noexcept { return availible.size() == count; }

		std::pair<byte*, bool> allocate()
		{
			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * sizeof(T)), availible.empty() };
		}

		template<class P>
		bool containsMem(P* ptr) const noexcept
		{
			return (reinterpret_cast<byte*>(ptr) >= mem
				 && reinterpret_cast<byte*>(ptr) < (mem + (sizeof(T) * count)));
		}

		// Destruct the object and return its index to the
		// list of availible memory. Re-initilize it to the required state
		void deallocate(T* ptr)
		{
			auto idx = static_cast<size_t>((reinterpret_cast<byte*>(ptr) - mem) / sizeof(T));
			availible.emplace_back(idx); 
			Cache::xtors->destruct(ptr); // This defaults to doing nothing, and will only do something if a dtor is passed
		}
	};

	template<class T, class Xtors>
	struct Cache
	{
		using size_type = size_t;
		using Storage	= alloc::List<Slab<T, Cache, Xtors>>;
		using It		= typename Storage::iterator;

		inline static Storage slabsFull;
		inline static Storage slabsPart;
		inline static Storage slabsFree;

		inline static size_type mySize		= 0;	// Total objects
		inline static size_type perCache	= 0;	// Objects per cache
		inline static size_type myCapacity	= 0;	// Total capacity for objects without more allocations

		inline static Xtors* xtors = nullptr;

		// TODO: Should this also reconstruct all objects that 
		// haven't used this xtor and are availible? Yes, probably!
		static void setXtors(Xtors& tors) { xtors = &tors; }


		// TODO: Should we only allow this function to change count on init?
		static void addCache(size_type count, Xtors& tors)
		{
			perCache = count;
			setXtors(tors);
			newSlab();
		}

		static void newSlab()
		{
			slabsFree.emplace_back(perCache);
			myCapacity += perCache;
		}

		// TODO: Right now this code is basically identical to SlabMem's
		// Figure out if it's worth it to combine the functionality (it may start to split off as I add features)
		//
		static std::pair<Storage*, It> findSlab()
		{
			It slabIt;
			Storage* store = nullptr;
			if (!slabsPart.empty())
			{
				slabIt	= std::begin(slabsPart);
				store	= &slabsPart;
			}
			else
			{
				// No empty slabs, need to create one! (TODO: If allowed to create?)
				if (slabsFree.empty())
					newSlab();

				slabIt	= std::begin(slabsFree);
				store	= &slabsFree;
			}

			return { store, slabIt };
		}

		static T* allocate()
		{
			auto[store, it] = findSlab();
			auto[mem, full] = it->allocate();

			// If we're taking memory from a free slab
			// add it to the list of partially full slabs
			if (store == &slabsFree)
				slabsPart.splice(std::begin(slabsPart), *store, it);

			// Give the slab storage to the 
			// full list if it has no more room
			if (full)
				slabsFull.splice(std::begin(slabsFull), *store, it);

			++mySize;
			return reinterpret_cast<T*>(mem);
		}

		template<class P>
		static std::pair<Storage*, It> searchStore(Storage& store, P* ptr)
		{
			for (auto it = std::begin(store);
				it != std::end(store); ++it)
				if (it->containsMem(ptr))
					return { &store, it };
			return { &store, store.end() };
		}

		static void deallocate(T* ptr)
		{
			auto[store, it] = searchStore(slabsFull, ptr);
			// Need to move slab back into partials
			if (it != slabsFull.end())
				slabsPart.splice(std::begin(slabsPart), slabsFull, it);

			else
			{
				// TODO: Super ugly. Due to not being able to structured bind already initlized variables
				auto[s, i] = searchStore(slabsPart, ptr);
				store = s;
				it = i;
			}

			if (it == slabsPart.end()) // TODO: Remove?
				throw alloc::bad_dealloc(); 

			it->deallocate(ptr);

			// Return slab to free list if it's empty
			if (it->empty())
				slabsFree.splice(std::begin(slabsFree), *store, it);

			--mySize;
		}

		static void freeAll()
		{
			slabsFull.clear();
			slabsPart.clear();
			slabsFree.clear();
			myCapacity	= 0;
			mySize		= 0;
		}

		static void freeEmpty()
		{
			myCapacity -= slabsFree.size() * perCache;
			slabsFree.clear();
		}

		static alloc::CacheInfo info() noexcept
		{
			return { mySize, myCapacity, sizeof(T), perCache };
		}
	};

	template<class T, class Xtors>
	struct CacheT
	{
		using CacheTT = SlabImpl::Cache<Slab<T, CacheT<T, Xtors>, Xtors>>;
		inline static CacheTT storage;

		inline static Xtors* xtors = nullptr;

		// TODO: Should this also reconstruct all objects that 
		// haven't used this xtor and are availible? Yes, probably!
		static void setXtors(Xtors& tors) { xtors = &tors; }

		// TODO: Should we only allow this function to change count on init?
		static void addCache(size_t count, Xtors& tors)
		{
			setXtors(tors);
			storage = CacheTT{ sizeof(T), count };
		}

		static T* allocate()
		{
			return storage.allocate<T>();
		}

		static void deallocate(T* ptr)
		{
			storage.deallocate(ptr);
		}

		static void freeAll()
		{
			storage.freeAll();
		}

		static void freeEmpty()
		{
			storage.freeEmpty();
		}

		static alloc::CacheInfo info()
		{
			return storage.info();
		}
	};

	struct Interface
	{
		using size_type = size_t;

		template<class T, class Xtors>
		static void addCache(size_type count, Xtors& tors)
		{
			count = alloc::nearestPageSz(count * sizeof(T)) / sizeof(T);
			SlabImpl::addToMap(count);
			CacheT<T, Xtors>::addCache(count, tors);
		}

		template<class T, class Xtors>
		static T* allocate()
		{
			return CacheT<T, Xtors>::allocate();
		}

		template<class T, class Xtors>
		static void deallocate(T* ptr)
		{
			CacheT<T, Xtors>::deallocate(ptr);
		}

		template<class T, class Xtors>
		static void freeAll()
		{
			CacheT<T, Xtors>::freeAll();
		}

		template<class T, class Xtors>
		static void freeEmpty()
		{
			CacheT<T, Xtors>::freeEmpty();
		}

		template<class T, class Xtors>
		static alloc::CacheInfo info()
		{
			return CacheT<T, Xtors>::info();
		}
	};

		/*
	struct Interface
	{
		using size_type = size_t;

		template<class T, class Xtors>
		static void addCache(size_type count, Xtors& tors)
		{
			count = alloc::nearestPageSz(count * sizeof(T)) / sizeof(T);
			SlabImpl::addToMap(count);
			Cache<T, Xtors>::addCache(count, tors);
		}

		template<class T, class Xtors>
		static T* allocate()
		{
			return Cache<T, Xtors>::allocate();
		}

		template<class T, class Xtors>
		static void deallocate(T* ptr)
		{
			Cache<T, Xtors>::deallocate(ptr);
		}

		template<class T, class Xtors>
		static void freeAll()
		{
			Cache<T, Xtors>::freeAll();
		}

		template<class T, class Xtors>
		static void freeEmpty()
		{
			Cache<T, Xtors>::freeEmpty();
		}

		template<class T, class Xtors>
		static alloc::CacheInfo info() 
		{
			return Cache<T, Xtors>::info();
		}
	};
	*/

}