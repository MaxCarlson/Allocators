#pragma once
#include <list>
#include "AllocHelpers.h"

namespace alloc
{

	template<size_t bytes>
	struct TreePolicy
	{

	};

	enum AlSearch : byte
	{
		BEST_FIT,
		FIRST_FIT
	};

	template<size_t bytes>
	struct ListPolicy
	{
		inline static byte* MyBegin;
		inline static size_t MyLast;
		inline static std::list<std::pair<byte*, size_t>> availible; // TODO: These list nodes should be contained in memory before allocated blocks?

		inline static bool init			= 1;
		inline static AlSearch search	= FIRST_FIT;

		struct Header
		{
			size_t size;
		};

		inline static constexpr size_t headerSize = sizeof(Header);

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
					availible.insert(it, std::pair{ start, size });
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

		// BUG: Issue here. Not keeping track of allocated block length
		// don't know how much to free
		//
		// TODO: To fix add a header to all allocations ( be sure to include header offsets in all ops )
		template<class T>
		void deallocate(T const* ptr)
		{
			//addToList(ptr, )
		}
	};

	template<class Type, size_t bytes = 0, template<size_t> class Policy = ListPolicy>
	class FreeList
	{
		Policy<bytes> storage;

	public:


		FreeList() = default;

		Type* allocate(size_t count)
		{
			return storage.allocate<Type>(count);
		}

		void deallocate(Type const* ptr)
		{
			storage.deallocate(ptr);
		}
	};
}