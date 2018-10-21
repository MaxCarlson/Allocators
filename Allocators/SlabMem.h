#pragma once
#include <vector>
#include <numeric>
#include "AllocHelpers.h"

namespace SlabMemImpl
{
	using byte = alloc::byte;

	struct Slab
	{
	private:
		using size_type = size_t;

		byte*					mem;
		size_type				objSize;
		size_type				count;
		std::vector<uint16_t>	availible;

	public:

		Slab() : mem{ nullptr } {}
		Slab(size_t objSize, size_t count) 
			: objSize{ objSize }, count{ count }, availible(count)
		{
			mem = reinterpret_cast<byte*>(operator new(objSize * count));
			std::iota(std::rbegin(availible), std::rend(availible), 0);
		}

		void otherMove(Slab&& other) noexcept
		{
			mem			= std::move(other.mem);
			other.mem	= nullptr;
			objSize		= std::move(other.objSize);
			count		= std::move(other.count);
			availible	= std::move(other.availible);
		}

		Slab(const Slab& other) = delete;

		Slab& operator=(Slab&& other) noexcept
		{
			otherMove(std::move(other));
			return *this;
		}

		Slab(Slab&& other) noexcept
		{
			otherMove(std::move(other));
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
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * objSize), availible.empty() };
		}

		template<class P>
		void deallocate(P* ptr)
		{
			auto idx = static_cast<size_type>((reinterpret_cast<byte*>(ptr) - mem) / objSize);
			availible.emplace_back(idx);
			ptr->~P();
		}

		template<class P>
		bool containsMem(P* ptr) const noexcept
		{
			return (reinterpret_cast<byte*>(ptr) >= mem
				 && reinterpret_cast<byte*>(ptr) < (mem + (objSize * count)));
		}
	};


	// Cache's based on memory size
	// Not designed around particular objects
	struct Cache
	{
		// TODOLIST:
		// TODO: Better indexing of memory (Coloring offsets to search slabs faster, etc)
		// TODO: Locking mechanism
		// TODO: Exception Safety 

		using size_type = size_t;
		using SlabStore = alloc::List<Slab>;
		using It		= SlabStore::iterator;

		size_type objSize;
		size_type count;
		size_type myCapacity = 0;
		size_type mySize = 0;

		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		Cache() = default;
		Cache(size_type objSize, size_type num) : objSize(objSize), count(num)
		{
			//count = alloc::nearestPageSz(num * objSize) / objSize; // This might increase random access times
			newSlab();
		}

		Cache(Cache&& other)			= default;
		Cache& operator=(Cache&& other) = default;

		size_type size()						const noexcept { return mySize; }
		size_type capacity()					const noexcept { return myCapacity; }
		bool operator<(const Cache& other)		const noexcept { return size() < other.size(); }
		alloc::CacheInfo info()					const noexcept { return { size(), capacity(), objSize, count }; }

		void newSlab()
		{
			slabsFree.emplace_back(objSize, count);
			myCapacity += count;
		}

		std::pair<SlabStore*, It> findFreeSlab()
		{
			It slabIt;
			SlabStore* store = nullptr;
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

		template<class T>
		T* allocate()
		{
			auto[store, it] = findFreeSlab();
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

		// Search slabs for one that holds the memory
		// ptr points to
		//
		// TODO: We should implement a coloring offset scheme
		// so that some Slabs store objects at address of address % 8 == 0
		// and others at addresses % 12 == 0 so we can search faster for the proper Slab
		template<class P>
		std::pair<SlabStore*, It> searchStore(SlabStore& store, P* ptr)
		{
			for (auto it = std::begin(store);
				it != std::end(store); ++it)
				if (it->containsMem(ptr))
					return { &store, it };
			return { &store, store.end() };
		}

		template<class T>
		void deallocate(T* ptr)
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

			if (it == slabsPart.end())
				throw alloc::bad_dealloc(); 

			it->deallocate(ptr);

			// Return slab to free list if it's empty
			if (it->empty())
				slabsFree.splice(std::begin(slabsFree), *store, it);

			--mySize;
		}

		void freeAll()
		{
			slabsFree.clear();
			slabsPart.clear();
			slabsFull.clear();
			myCapacity	= 0;
			mySize		= 0;
		}

		void freeEmpty()
		{
			myCapacity -= slabsFree.size() * objSize;
			slabsFree.clear();
		}

	};

	// Holds all the memory caches 
	// of different sizes
	struct Interface
	{
		using size_type		= size_t;
		using SmallStore	= std::vector<Cache>;
		using It			= typename SmallStore::iterator;

		inline static SmallStore caches;

		Interface() = default;

		// Add a dynamic cache that stores count 
		// number of objSize memory chunks
		//
		// TODO: Add a debug check so this function won't add any 
		// (or just any smaller than largest) caches after first allocation for safety?
		void addCache(size_type objSize, size_type count)
		{
			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (objSize < it->objSize)
				{
					caches.emplace(it, objSize, count);
					return;
				}
			caches.emplace_back(objSize, count);
		}

		std::vector<alloc::CacheInfo> info() const noexcept
		{
			std::vector<alloc::CacheInfo> stats;
			for (const auto& ch : caches)
				stats.emplace_back(ch.info());
			return stats;
		}

		template<class T>
		T* allocate(size_t count)
		{
			const auto bytes = count * sizeof(T);
			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (bytes <= it->objSize)
					return it->allocate<T>();

			// TODO: Should we grow the pool here instead of throwing?
			// OR set a flag to grow/throw?
			throw std::bad_alloc();
			return nullptr;
		}

		template<class T>
		T* allocate()
		{
			return allocate<T>(1);
		}

		template<class T>
		void deallocate(T* ptr, size_t cnt = 1)
		{
			const auto bytes = cnt * sizeof(T);
			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (bytes <= it->objSize)
				{
					it->deallocate(ptr);
					return;
				}
			throw alloc::bad_dealloc();
		}

		template<bool all>
		void freeFunc(size_t cacheSize)
		{
			if (cacheSize == 0)
			{
				for (auto it = std::begin(caches); it != std::end(caches); ++it)
				{
					if constexpr (all)
						it->freeAll();
					else
						it->freeEmpty();
				}
				return;
			}

			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (it->objSize == cacheSize)
				{
					if constexpr (all)
						it->freeAll();
					else
						it->freeEmpty();
					return;
				}
		}

		void freeAll(size_t cacheSize)
		{
			freeFunc<true>(cacheSize);
		}

		void freeEmpty(size_t cacheSize)
		{
			freeFunc<false>(cacheSize);
		}


	};
}