#pragma once
#include <vector>
#include <numeric>
#include "AllocHelpers.h"

namespace SlabMemImpl
{
	using byte = alloc::byte;

	// Maximum number of Caches that can be added
	// to SlabMem (Used to keep the size_type in header small)
	inline constexpr auto MAX_CACHES = 127;

	// Used to find the Cache in deallocation 
	// with SlabMem
	struct Header
	{
		enum
		{
			NO_CACHE = MAX_CACHES + 1
		};

		using size_type = typename alloc::FindSizeT<MAX_CACHES, 1>::size_type;

		size_type cacheIdx;
	};

	struct Slab
	{
	private:
		using size_type = size_t;

		byte*					mem;
		size_type				blockSize;
		size_type				count;
		std::vector<uint16_t>	availible;

	public:

		Slab() : mem{ nullptr } {}
		Slab(size_t blockSize, size_t count, Header::size_type index) 
			: blockSize{ blockSize }, count{ count }, availible(count)
		{
			mem = reinterpret_cast<byte*>(operator new((blockSize + sizeof(Header)) * count ));
			std::iota(std::rbegin(availible), std::rend(availible), 0);

			// TODO: This assumes the memory handed to us is 16byte aligned
			// 16 byte align whatever data user stores
			//mem += (8 - sizeof(Header));

			//
			// TODO: Header messes up alignment here right?
			// TODO: Coloring offsets for different Slabs
			//
			// Set all the indicies. This only has to be done once
			// and will allow us to find this Cache (quickly) again on deallocation
			for (auto i = 0; i < count; ++i)
				reinterpret_cast<Header*>(mem + (blockSize + sizeof(Header)) * i)->cacheIdx = index;
		}

		void otherMove(Slab&& other) noexcept
		{
			mem			= std::move(other.mem);
			other.mem	= nullptr;
			blockSize	= std::move(other.blockSize);
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
			if (availible.empty()) // TODO: This should never happen? DELETE?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * (blockSize + sizeof(Header))) + sizeof(Header), availible.empty() };
		}

		template<class P>
		void deallocate(P* ptr)
		{
			auto idx = static_cast<size_type>((reinterpret_cast<byte*>(ptr) - sizeof(Header) - mem) / (blockSize + sizeof(Header)));
			availible.emplace_back(idx);
			ptr->~P();
		}

		template<class P>
		static Header* getHeader(P* ptr)
		{
			return reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - sizeof(Header));
		}

		template<class P>
		bool containsMem(P* ptr) const noexcept
		{
			return (reinterpret_cast<byte*>(ptr) >= mem
				 && reinterpret_cast<byte*>(ptr) < (mem + ((blockSize + sizeof(Header)) * count)));
		}
	};


	// Cache's based on memory size
	// Not designed around particular objects
	struct Cache
	{
		// TODOLIST:
		// TODO: Perhaps keep storage in sorted memory order? Would make finding
		// deallocations super fast?
		// TODO: Also, look into using a vector as storage instead of list
		// TODO: Better indexing of memory (Coloring offsets to search slabs faster, etc)
		// TODO: Locking mechanism
		// TODO: Exception Safety 

		using size_type = size_t;
		using SlabStore = alloc::List<Slab>;
		using It		= SlabStore::iterator;

		inline static Header::size_type nextIndex = 0;

		Header::size_type index; // Index of this cache, used for deallocation

		size_type count;
		size_type blockSize;
		size_type myCapacity	= 0;
		size_type mySize		= 0;

		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		//Cache() = default;
		Cache(size_type blockSize, size_type num) : count(num), blockSize(blockSize)
		{
			//count = alloc::nearestPageSz(num * (blockSize * sizeof(Header))) / (blockSize * sizeof(Header)); // This might increase random access times
			index = nextIndex++;
			newSlab();
		}

		Cache(Cache&& other)			= default;
		Cache& operator=(Cache&& other) = default;

		size_type size()						const noexcept { return mySize; }
		size_type capacity()					const noexcept { return myCapacity; }
		bool operator<(const Cache& other)		const noexcept { return size() < other.size(); }
		alloc::CacheInfo info()					const noexcept { return { size(), capacity(), blockSize, count }; }

		void newSlab()
		{
			slabsFree.emplace_back(blockSize, count, index);
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
			myCapacity -= slabsFree.size() * blockSize;
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

		//template<class Func>
		//static void addCache(Func&& func)
		//{
		//	func(caches);
		//}

		// TODO: Add a debug check so this function won't add any 
		// (or just any smaller than largest) caches after first allocation for safety?
		//
		static void addCache(size_type blockSize, size_type count)
		{
			caches.emplace_back(blockSize, count);
		}

		static void addCache2(size_type startSz, size_type maxSz, size_type count)
		{
			for (auto i = 0; startSz <= maxSz; ++i, startSz <<= 1)
				caches.emplace_back(startSz, count);
		}

		//static void addCacheFib(size_type startSz, size_type maxSz, size_type count)
		//{
		//	for (auto i = 0; startSz <= maxSz; ++i, startSz <<= 1)
		//		caches.emplace_back(startSz, count);
		//}

		template<class T>
		static T* allocate(size_t count)
		{
			// TODO: Binary search if caches is large enough?

			const auto bytes = count * sizeof(T);
			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (bytes <= it->blockSize)
					return it->allocate<T>();

			// If the allocation is too large go ahead and allocate from new
			byte* ptr = reinterpret_cast<byte*>(operator new((sizeof(T) + sizeof(Header)) * count));
			reinterpret_cast<Header*>(ptr)->cacheIdx = Header::NO_CACHE;
			return reinterpret_cast<T*>(ptr + sizeof(Header));
		}

		template<class T>
		static void deallocate(T* ptr)
		{
			const auto hPtr = Slab::getHeader(ptr);
			if (hPtr->cacheIdx != Header::NO_CACHE)
			{
				caches[hPtr->cacheIdx].deallocate(ptr);
				return;
			}

			operator delete(ptr);
		}

		static std::vector<alloc::CacheInfo> info() noexcept
		{
			std::vector<alloc::CacheInfo> stats;
			for (const auto& ch : caches)
				stats.emplace_back(ch.info());
			return stats;
		}

		template<bool all>
		static void freeFunc(size_t cacheSize)
		{
			for (auto it = std::begin(caches); it != std::end(caches); ++it)
				if (cacheSize == 0 || it->blockSize == cacheSize)
				{
					if constexpr (all)
						it->freeAll();
					else
						it->freeEmpty();
					return;
				}
		}

		static void freeAll(size_t cacheSize)
		{
			freeFunc<true>(cacheSize);
		}

		static void freeEmpty(size_t cacheSize)
		{
			freeFunc<false>(cacheSize);
		}
	};
}