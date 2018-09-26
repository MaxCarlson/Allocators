#pragma once
#include <list>
#include "AllocHelpers.h"



namespace alloc
{

	template<size_t bytes, class pred = std::less<size_t>>
	struct TreePolicy
	{

	};

	enum AlSearch : byte
	{
		BEST_FIT,
		FIRST_FIT
	};

	template<size_t bytes, class pred = std::less<size_t>>
	struct ListPolicy
	{
		struct Header
		{
			size_t size;
		};

		inline static byte* MyBegin;
		inline static size_t MyLast;
		inline static std::list<std::pair<byte*, size_t>> availible; // TODO: These list nodes should be contained in memory before allocated blocks?

		inline static bool init			= 1;
		inline static AlSearch search	= FIRST_FIT;
		inline static constexpr size_t headerSize = sizeof(Header);

		static_assert(bytes > headerSize, "Allocator size is smaller than minimum required.");

		ListPolicy()
		{
			if (init)
			{
				init	= false;
				MyLast	= 0;
				MyBegin = reinterpret_cast<byte*>(operator new (bytes));

				addToList(MyBegin, bytes);
			}
		}

		byte* bestFit(const size_t byteCount)
		{
			return nullptr;
		}

		void addToList(byte* start, size_t size)
		{
			if (availible.empty())
			{
				availible.emplace_back(start, size);
				return;
			}

			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				if (it->second > size)
					availible.emplace(it, std::pair{ start, size });
			}
		}

		byte* firstFit(size_t byteCount)
		{
			for (auto it = std::begin(availible); 
					 it != std::end(availible); ++it)
			{
				// Found a section of memory large enough to hold
				// what we want to allocate
				if (it->second >= byteCount)
				{
					auto* mem = it->first;
					auto itBytes = std::move(it->second);
					availible.erase(it);

					// If memory section is larger than what we want
					// add a new list entry the reflects the remaining memory
					if (itBytes > byteCount)
						addToList(mem + byteCount, itBytes - byteCount);
					
					return writeHeader(mem, byteCount - headerSize);
				}
			}
			return nullptr;
		}

		// Writes the header and adjusts the pointer
		// to the memory right after header 
		byte* writeHeader(byte* start, size_t size)
		{
			auto* header = reinterpret_cast<Header*>(start);
			*header = Header{ size };

			return start + (headerSize);
		}

		template<class T>
		T* allocate(size_t count)
		{
			byte* mem = nullptr;
			const size_t byteCount = sizeof(T) * count + headerSize;

			if (search == FIRST_FIT)
				mem = firstFit(byteCount);
			else
				mem = bestFit(byteCount);

			if (!mem) //  || count * sizeof(T) + MyLast > bytes ||| We don't need commented out code here right? It's implicit?
				throw std::bad_alloc();

			MyLast += byteCount;
			return reinterpret_cast<T*>(mem);
		}

		// TODO: Do we keep the smallest or largest chunks of memory first?
		// In first fit smallest is probably best? Opposite in best fit
		//
		// TODO: In order to perform coalescence do we need to keep memory blocks in
		// physical address order??
		// Should we keep a list in pred ordering and add a footer to allocations
		// denoting block's status/size so we can deallocate/merge in O(1) and still allocate O(N)
		template<class T>
		void deallocate(T* ptr)
		{
			Header* header = reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);

			if (availible.empty())
			{
				availible.emplace_back(reinterpret_cast<byte*>(header), header->size);
				return;
			}

			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				if (pred()(header->size, it->second))
				{
					availible.emplace(it, reinterpret_cast<byte*>(header), header->size);
					return;
				}
			}
		}
	};

	template<class Type, size_t bytes = 0, template<size_t, class> class Policy = ListPolicy, class pred = std::less<size_t>>
	class FreeList
	{
		Policy<bytes, pred> storage;

	public:


		FreeList() = default;

		Type* allocate(size_t count)
		{
			return storage.allocate<Type>(count);
		}

		void deallocate(Type* ptr)
		{
			storage.deallocate(ptr);
		}
	};
}