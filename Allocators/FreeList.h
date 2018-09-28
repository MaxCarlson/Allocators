#pragma once
#include <list>
#include "AllocHelpers.h"



namespace alloc
{

	enum AlSearch : byte
	{
		BEST_FIT,
		FIRST_FIT
	};

	template<size_t bytes, size_t takenBits>
	struct FindSizeT
	{
		// This finds the smallest number of bits
		// required (within the constraints of the types availible)
		// TODO: ? This can be condensed into just conditionals if we want
		enum 
		{
			bits =		bytes <= 0xff		>> takenBits ? 8  :
						bytes <= 0xffff		>> takenBits ? 16 :
						bytes <= 0xffffffff	>> takenBits ? 32 :
														   64
		};
		using size_type =	std::conditional_t<bits == 8,	uint8_t, 
							std::conditional_t<bits == 16,	uint16_t, 
							std::conditional_t<bits == 32,	uint32_t, 
															uint64_t >>>;
	};

	template<size_t bytes, class pred = std::less<size_t>>
	struct TreePolicy
	{

	};

	template<size_t bytes, 
		class pred = std::less<size_t>>
	struct ListPolicy
	{

		// Detect the minimum size type we can use
		// (based on max number of bytes) and use that as the
		// size_type to keep overhead as low as possible
		using size_type = typename FindSizeT<bytes, 1>::size_type;

		struct Header
		{
			size_type size : (sizeof(size_type) * 8) - 1; 
			size_type free : 1;
		};

		inline static byte* MyBegin;
		inline static byte* MyEnd;
		inline static std::list<std::pair<byte*, size_type>> availible; // TODO: These list nodes should be contained in memory before allocated blocks?

		inline static bool init			= 1;
		inline static AlSearch search	= FIRST_FIT;
		inline static constexpr size_t headerSize = sizeof(Header);
		inline static constexpr size_t headerSize2 = headerSize * 2;

		static_assert(bytes > headerSize2, "Allocator size is smaller than minimum required.");

		ListPolicy()
		{
			if (init)
			{
				init	= false;
				MyBegin = reinterpret_cast<byte*>(operator new (bytes));
				MyEnd	= MyBegin + bytes;

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

		// Shift by bytes // I see issues with S unsinged types and attempting to shift down
		// TODO: Fix?
		//template<class T, class S>
		//T* shByte(T* t, const S b)
		//{
		//	return reinterpret_cast<T*>(reinterpret_cast<byte*>(t + b));
		//}

		void coalesce(Header*& header)
		{
			byte* byteHeader = reinterpret_cast<byte*>(header);

			// TODO: After merging ajacent blocks we need to scan through list to find them still, oops...!
			// Still have O(N) time if we don't keep list in memory sorted order :(
			// TODO: Decide what to do!

			// Look backward
			if (byteHeader - headerSize > MyBegin)
			{
				auto* prevFoot = reinterpret_cast<Header*>(byteHeader - headerSize);
				if (prevFoot->free)
				{
					auto newSize	= prevFoot->size + header->size + headerSize2; // TODO: I think this is correct? Check!
					header			= reinterpret_cast<Header*>(reinterpret_cast<byte*>(prevFoot) - (prevFoot->size + headerSize));
					header->size	= newSize;
					byteHeader		= reinterpret_cast<byte*>(header); // TODO: This is needed right?
				}
			}

			// Look forward
			if (byteHeader + header->size <= MyEnd)
			{
				auto* nextHeader = reinterpret_cast<Header*>(byteHeader + header->size + headerSize);
				if (nextHeader->free)
					header->size = nextHeader->size + header->size + headerSize2;
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
			coalesce(header);

			header->free = footer->free = true;

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