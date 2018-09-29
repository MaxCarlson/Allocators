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

	template<size_t bytes, 
		class pred = std::less<size_t>>
	struct TreePolicy
	{

	};

	template<size_t bytes,
		class pred = std::less<size_t>>
	struct FlatPolicy
	{

	};

	template<size_t bytes, class size_type,
		class pred>
	struct ListPolicy
	{
		inline static std::list<std::pair<byte*, size_type>> availible; // TODO: These list nodes should be contained in memory before allocated blocks?

		using It		= typename std::list<std::pair<byte*, size_type>>::iterator;
		using Ib		= std::pair<It, bool>;
		using BytePair	= std::pair<byte*, byte*>;

		void add(byte* start, size_type size, BytePair bpair = { nullptr, nullptr })
		{
			if (availible.empty())
			{
				availible.emplace_back(start, size);
				return;
			}

			int mask = 0;
			if (!bpair.first)
				mask |= 2;
			if (!bpair.second)
				mask |= 4;

			auto processPair = [&mask](byte*& b, It& it, int flag)
			{
				if (b && b == it->first)
				{
					it = availible.erase(it);
					mask |= flag;
				}
			}

			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				if (pred()(size, it->second))
				{
					availible.emplace(it, std::pair{ start, size });
					mask |= 1;
				}

				processPair(bpair.first,  it, 2);
				processPair(bpair.second, it, 4);

				if (!(mask ^ 7))
					return;
			}
		}

		void erase(It it)
		{
			availible.erase(it);
		}

		Ib firstFit(size_t reqBytes)
		{
			for (It it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				// Found a section of memory large enough to hold
				// what we want to allocate
				if (it->second >= reqBytes)
					return { it, true };
			}
			return { It{}, false };
		}
	};

	template<size_t bytes, template<class> class pred,
		template<size_t, class, class> class Policy>
	struct PolicyInterface
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

		using OurPolicy = Policy<bytes, size_type, pred<size_type>>;
		using It		= typename OurPolicy::It;
		using Ib		= typename OurPolicy::Ib;
		using BytePair	= typename OurPolicy::BytePair;
		
		// Handles how we store, find, 
		// and emplace free memory information
		inline static OurPolicy policy;

		inline static byte* MyBegin;
		inline static byte* MyEnd;

		inline static bool init = 1;
		inline static AlSearch search = FIRST_FIT;
		inline static constexpr size_t headerSize = sizeof(Header);
		inline static constexpr size_t headerSize2 = headerSize * 2;

		static_assert(bytes > headerSize2 + 1, "Allocator size is smaller than minimum required.");

		PolicyInterface()
		{
			if (init)
			{
				// TODO: Should we just make this class use an  
				// array since it's size is compile time determined?
				init	= false;
				MyBegin = reinterpret_cast<byte*>(operator new (bytes));
				MyEnd	= MyBegin + bytes;

				// Zero on init. In the future we'll only need to zero 
				// headers
				zeroBlock(MyBegin, bytes);
				policy.add(MyBegin, bytes);
			}
		}

		template<class T>
		void zeroBlock(T* start, size_type size)
		{
			std::memset(start, 0, size);
		}

		byte* bestFit(const size_t byteCount)
		{
			return nullptr;
		}

		byte* firstFit(size_type reqBytes)
		{
			auto [it, found] = policy.firstFit(reqBytes);
			if (!found)
				return nullptr;

			auto* mem		= it->first;
			auto chunkBytes = std::move(it->second);
			policy.erase(it);

			// If memory section is larger than what we want
			// add a new list entry the reflects the remaining memory
			if (chunkBytes > reqBytes)
			{
				policy.add(mem + reqBytes, chunkBytes - reqBytes);

				// Zero out the area where we might look for a 
				// header later if this block is freed
				if (chunkBytes > reqBytes + headerSize)
					zeroBlock(mem + reqBytes, headerSize);
			}

			return writeHeader(mem, reqBytes - headerSize2);
		}

		// Writes the header and adjusts the returned 
		// pointer to user memory
		byte* writeHeader(byte* start, size_type size)
		{
			// TODO: Does the construction of another header make it less efficiant
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
		T* allocate(size_type count)
		{
			byte* mem = nullptr;
			size_type byteCount = sizeof(T) * count + headerSize2;

			if (search == FIRST_FIT)
				mem = firstFit(byteCount);
			else
				mem = bestFit(byteCount);

			if (!mem) //  || count * sizeof(T) + MyLast > bytes ||| We don't need commented out code here right? It's implicit?
				throw std::bad_alloc();

			return reinterpret_cast<T*>(mem);
		}

		BytePair coalesce(Header*& header)
		{
			BytePair blocks = { nullptr, nullptr };
			byte* byteHeader = reinterpret_cast<byte*>(header);

			// Look backward
			if (byteHeader - headerSize > MyBegin)
			{
				auto* prevFoot = reinterpret_cast<Header*>(byteHeader - headerSize);
				if (prevFoot->free)
				{
					auto newSize	= prevFoot->size + header->size + headerSize2; // TODO: I think this is correct? Check!
					header			= reinterpret_cast<Header*>(reinterpret_cast<byte*>(prevFoot) - (prevFoot->size + headerSize));
					header->size	= newSize;
					byteHeader		= reinterpret_cast<byte*>(header);
					blocks.first	= byteHeader;
				}
			}

			// Look forward
			byte* nextHeaderB = byteHeader + (headerSize + header->size + headerSize);
			if (nextHeaderB <= MyEnd)
			{
				auto* nextHeader = reinterpret_cast<Header*>(nextHeaderB);
				if (nextHeader->free)
				{
					blocks.second	= reinterpret_cast<byte*>(nextHeader);
					header->size	= nextHeader->size + header->size + headerSize2;
				}
			}

			return blocks;
		}

		template<class T>
		void deallocate(T* ptr)
		{
			Header* header		= reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);
			Header* footer		= reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) + header->size);
			Header* oldHeader	= header;

			// Coalesce ajacent blocks
			// Adjust header size and pointer if we joined blocks
			std::pair<byte*, byte*> bpair = coalesce(header);

			if (header != oldHeader)
				zeroBlock(oldHeader, headerSize);

			header->free = footer->free = true;

			policy.add(reinterpret_cast<byte*>(header), static_cast<size_type>(header->size), bpair);
		}
	};

	template<class Type, size_t bytes = 0, 
		template<size_t, class, class> class Policy = ListPolicy, 
		template<class> class pred = std::less>
	class FreeList
	{
		using OurPolicy = PolicyInterface<bytes, pred, Policy>;
		using size_type = typename OurPolicy::size_type;
		OurPolicy storage;


	public:


		FreeList() = default;

		Type* allocate(size_type count)
		{
			return storage.allocate<Type>(static_cast<size_type>(count));
		}

		void deallocate(Type* ptr)
		{
			storage.deallocate(ptr);
		}
	};
	
}