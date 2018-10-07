#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <array>
#include <numeric>

/*
namespace alloc
{
	struct SmallSlab;
}
template<class>
class List;

inline std::vector<List<alloc::SmallSlab>*> TRACK;
*/

// A custom linked list class with a couple
// extra operations


namespace alloc
{

	struct CacheInfo
	{
		CacheInfo(size_t size, size_t capacity, size_t objectSize, size_t objPerSlab)
			: size(size), capacity(capacity), objectSize(objectSize), objPerSlab(objPerSlab) {}

		size_t size;
		size_t capacity;
		size_t objectSize;
		size_t objPerSlab;
	};

	/*
	template<class T>
	struct ObjSlab
	{

	};

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T>
	struct ObjCache
	{
		using Storage = List<ObjSlab<T>>;

		inline static Storage full;
		inline static Storage free;

	};
	*/

	struct SmallSlab
	{
	private:
		using size_type = size_t;

		byte* mem;
		size_type objSize; 
		size_type count;
		std::vector<uint16_t> availible;
		
	public:

		SmallSlab() = default;
		SmallSlab(size_t objSize, size_t count);

		//~SmallSlab(){ delete mem; } 
		// TODO: Need a manual destroy func if using vec and moving between vecs?
		// TODO: operator delete ? with byte* allocated from operator new?

		bool full()			const noexcept { return availible.empty(); }
		size_type size()	const noexcept { return count - availible.size(); }
		bool empty()		const noexcept { return availible.size() == count; }

		std::pair<byte*, bool> allocate();

		template<class P>
		void deallocate(P* ptr)
		{
			auto idx = static_cast<size_t>((reinterpret_cast<byte*>(ptr) - mem) / objSize);
			availible.emplace_back(idx);
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
	struct SmallCache
	{
		// TODOLIST:
		// TODO: Better indexing of memory (offsets to search slabs faster, etc)
		// TODO: Locking mechanism
		// TODO: Page alignemnt/Page Sizes for slabs? (possible on windows?)
		// TODO: Exception Safety 
		
		using size_type = size_t;
		using Slab		= SmallSlab;
		using SlabStore = List<Slab>;
		using It		= SlabStore::iterator;

		size_type objSize;
		size_type count;
		size_type myCapacity	= 0;
		size_type mySize		= 0;

		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		SmallCache() = default;
		SmallCache(size_type objSize, size_type count);


		size_type size()						const noexcept { return mySize; }
		size_type capacity()					const noexcept { return myCapacity; }
		bool operator<(const SmallCache& other) const noexcept { return size() < other.size(); }
		CacheInfo info()						const noexcept { return { size(), capacity(), objSize, count }; }

		void newSlab();

		std::pair<SlabStore*, It> findFreeSlab();

		template<class T>
		T* allocate()
		{
			auto [store, it] = findFreeSlab();
			auto [mem, full] = it->allocate();

			// If we're taking memory from a free slab
			// add it to the list of partially full slabs
			if (store == &slabsFree)
				store->giveNode(it, slabsPart, std::begin(slabsPart));
			
			// Give the slab storage to the 
			// full list if it has no more room
			if (full)
				store->giveNode(it, slabsFull, std::begin(slabsFull));

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
			auto [store, it] = searchStore(slabsFull, ptr);
			// Need to move slab back into partials
			if (it != slabsFull.end())
			{
				slabsFull.giveNode(it, slabsPart, slabsPart.begin());
			}
			else
			{
				auto[s, i] = searchStore(slabsPart, ptr);
				store = s;
				it = i;
			}

			if (it == slabsPart.end())
				throw std::bad_alloc(); // TODO: Is this the right exception?

			it->deallocate(ptr);
			
			// Return slab to free list if it's empty
			if (it->empty())
				store->giveNode(it, slabsFree, std::begin(slabsFree));

			--mySize;
		}
	};

	// Holds all the memory caches 
	// of different sizes
	struct SlabMemInterface
	{
		using size_type		= size_t;
		using SmallStore	= std::vector<SmallCache>;
		using It			= typename SmallStore::iterator;

		inline static SmallStore caches;

		SlabMemInterface() = default;

		// Add a dynamic cache that stores count 
		// number of objSize memory chunks
		//
		// TODO: Add a debug check so this function won't add any 
		// (or just any smaller than largest) caches after first allocation for safety?
		void addCache(size_type objSize, size_type count);

		std::vector<CacheInfo> info() const noexcept;

		template<class T>
		T* allocate()
		{
			T* mem = nullptr;

			for (It it = std::begin(caches); it != std::end(caches); ++it)
				if (sizeof(T) <= it->objSize)
				{
					mem = it->allocate<T>();
					return mem;
				}

			throw std::bad_alloc();
			return mem;
		}

		template<class T>
		void deallocate(T* ptr)
		{
			for (It it = std::begin(caches); it != std::end(caches); ++it)
				if (sizeof(T) <= it->objSize)
				{
					it->deallocate(ptr);
					return;
				}
		}
	};

	struct SlabObjInterface
	{
		using size_type		= size_t;
		//using SmallStore	= std::vector<SmallCache>;
		//using It			= typename SmallStore::iterator;

		template<class T>
		void addCache(size_type count)
		{

		}
	};

	template<class Type>
	class Slab
	{
		// NOTE: as it stands, all of memstores caches need to be instantiated
		// BEFORE any memory is allocated due the way deallocation works. If any smaller 
		// caches are added after an object has been allocated that the allocated object fits
		// in it will result in undefined behavior on attempted deallocation!
		//
		// Useable for memory sizes up to... (per cache) count <= std::numeric_limits<uint16_t>::max()
		inline static SlabMemInterface memStore;


		inline static SlabObjInterface objStore;

	public:

		using size_type = size_t;


		// Does not take a count argument because 
		// we can only allocate one object at a time
		template<class T = Type>
		T* allocateMem()
		{
			return memStore.allocate<T>();
		}

		template<class T = Type>
		void deallocateMem(T* ptr)
		{
			memStore.deallocate(ptr);
		}

		void addMemCache(size_type objSize, size_type count)
		{
			memStore.addCache(objSize, count);
		}

		template<class T = Type>
		void addMemCache(size_type count)
		{
			memStore.addCache(sizeof(T), count);
		}

		std::vector<CacheInfo> memInfo() const noexcept
		{
			return memStore.info();
		}

		template<class T = Type>
		void addObjCache(size_type count)
		{
			objStore.addCache<T>(count);
		}
	};
}