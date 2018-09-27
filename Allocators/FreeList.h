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
		using size_type = size_t;

		struct Header
		{
			size_type size : (sizeof(size_type) * 8) - 1; 
			size_type free : 1;
		};

		inline static byte* MyBegin;
		inline static std::list<std::pair<byte*, size_t>> availible; // TODO: These list nodes should be contained in memory before allocated blocks?

		inline static bool init			= 1;
		inline static AlSearch search	= FIRST_FIT;
		inline static constexpr size_t headerSize = sizeof(Header);
		inline static constexpr size_t headerSize2 = headerSize * 2;

		static_assert(bytes > headerSize * 2, "Allocator size is smaller than minimum required.");

		ListPolicy()
		{
			if (init)
			{
				init	= false;
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

		byte* firstFit(size_t reqBytes)
		{
			for (auto it = std::begin(availible); 
					 it != std::end(availible); ++it)
			{
				// Found a section of memory large enough to hold
				// what we want to allocate
				if (it->second >= reqBytes)
				{
					auto* mem = it->first;
					auto chunkBytes = std::move(it->second);
					availible.erase(it);

					// If memory section is larger than what we want
					// add a new list entry the reflects the remaining memory
					if (chunkBytes > reqBytes)
						addToList(mem + reqBytes, chunkBytes - reqBytes);
					
					return writeHeader(mem, reqBytes - headerSize2);
				}
			}
			return nullptr;
		}

		// Writes the header and adjusts the returned 
		// pointer to user memory
		byte* writeHeader(byte* start, size_type size)
		{
			// TODO: Does the construction of another header make it less effeciant
			// than replacing variables? Look at assembly/test once finalized
			//
			//*reinterpret_cast<Header*>(start) = Header{ size, false };
			auto* header = reinterpret_cast<Header*>(start);
			*header = Header{ size, false };

			start += headerSize;

			//*reinterpret_cast<Header*>(start + (size - headerSize)) = Header{ size, false };
			auto* footer = reinterpret_cast<Header*>(start + size);
			*footer = Header{ size, false };

			return start;
		}

		template<class T>
		T* allocate(size_t count)
		{
			byte* mem = nullptr;
			const size_t byteCount = sizeof(T) * count + headerSize2;

			if (search == FIRST_FIT)
				mem = firstFit(byteCount);
			else
				mem = bestFit(byteCount);

			if (!mem) //  || count * sizeof(T) + MyLast > bytes ||| We don't need commented out code here right? It's implicit?
				throw std::bad_alloc();

			return reinterpret_cast<T*>(mem);
		}

		byte* joinChunks(Header* header, Header* footer)
		{

		}

		void coalescence(Header*& header, Header* footer)
		{
			byte* byteHeader = reinterpret_cast<byte*>(header);

			// Look backwards
			if (byteHeader - headerSize > MyBegin)
			{
				auto* prevFoot = reinterpret_cast<Header*>(byteHeader - headerSize);
				if (prevFoot->free)
				{
					Header* newHeader = prevFoot - (prevFoot->size + headerSize);
					*newHeader = Header{ prevFoot->size + header->size + headerSize * 4, true };
				}
					byteHeader = joinChunks(prevFoot, header);
			}

			// WRONG
			// Look forwards
			if (byteHeader + header->size <= bytes)
			{
				auto* nextHeader = reinterpret_cast<Header*>(byteHeader + header->size);
				if (nextHeader->free)
					joinChunks(header, nextHeader);
			}
		}

		// TODO: Do we keep the smallest or largest chunks of memory first?
		// In first fit smallest is probably best? Opposite in best fit
		//
		// Should we keep a list in pred ordering and add a footer to allocations
		// denoting block's status/size so we can deallocate/merge in O(1) and still allocate O(N)
		template<class T>
		void deallocate(T* ptr)
		{
			Header* header = reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);
			Header* footer = reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) + header->size);

			// Coalesce ajacent blocks
			// Adjust header size and pointer if we joined blocks
			coalescence(header, footer);

			if (availible.empty())
			{
				availible.emplace_back(reinterpret_cast<byte*>(header), static_cast<size_type>(header->size));
				return;
			}

			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				if (pred()(header->size, it->second))
				{
					availible.emplace(it, reinterpret_cast<byte*>(header), static_cast<size_type>(header->size));
					return;
				}
			}
		}
	};

	template<class Type, size_t bytes = 0, 
		template<size_t, class> class Policy = ListPolicy, 
		template<class> class pred = std::less>
	class FreeList
	{
		Policy<bytes, pred<size_t>> storage;

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