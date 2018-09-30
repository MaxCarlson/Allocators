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

	struct ByteTrip
	{
		int count = 0;
		byte* ptrs[2];

		void add(byte* p)
		{
			ptrs[count++] = p;
		}

		void remove(int idx)
		{
			if (idx == 0)
				ptrs[0] = ptrs[1];
			--count;
		}
	};

	template<size_t bytes, class size_type,
		class pred>
	struct ListPolicy
	{
		inline static std::list<std::pair<byte*, size_type>> availible; 

		using It		= typename std::list<std::pair<byte*, size_type>>::iterator;
		using Ib		= std::pair<It, bool>;
		using BytePair	= std::pair<byte*, byte*>;

		void add(byte* start, size_type size, ByteTrip bpair = {})
		{
			if (availible.empty())
			{
				availible.emplace_back(start, size);
				return;
			}

			int count = 2 - bpair.count;
			//if (!bpair.first)
			//	++count;
			//if (!bpair.second)
			//	++count;

			// Make sure we erase any previously added
			// blocks that have already been joined
			auto processPair = [&count](byte*& b, It& it)
			{
				if (b && b == it->first)
				{
					++count;
					b	= nullptr;
					it	= availible.erase(it);
				}
			};

			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				//processPair(bpair.first,  it, 2);
				//processPair(bpair.second, it, 4);

				// TODO: Need to keep track of which blocks/memory size!!
				for (int i = 0; i < bpair.count; ++i)
				{
					if (bpair.ptrs[i] == it->first)
					{
						++count;
						bpair.remove(i);
						it = availible.erase(it);
					}
				}

				if (pred()(size, it->second))
				{
					availible.emplace(it, std::pair{ start, size });
					++count;
				}

				if (count == 3)
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
			// add a new list entry that reflects the remaining memory
			if (chunkBytes > reqBytes)
				policy.add(mem + reqBytes, chunkBytes - reqBytes);

			return writeHeader(mem, reqBytes - headerSize2);
		}

		// Writes the header and adjusts the returned 
		// pointer to user memory
		byte* writeHeader(byte* start, size_type size)
		{
			auto* header = reinterpret_cast<Header*>(start);
			*header = Header{ size, false };

			start += headerSize;

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

			if (!mem)
				throw std::bad_alloc();

			return reinterpret_cast<T*>(mem);
		}

		ByteTrip coalesce(Header*& header)
		{
			ByteTrip blocks;
			byte* byteHeader = reinterpret_cast<byte*>(header);

			/*
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
			byte* nextHeaderB = byteHeader + (header->size + headerSize2);
			if (nextHeaderB <= MyEnd)
			{
				auto* nextHeader = reinterpret_cast<Header*>(nextHeaderB);
				if (nextHeader->free)
				{
					blocks.second	= reinterpret_cast<byte*>(nextHeader);
					header->size	= nextHeader->size + header->size + headerSize2;
				}
			}
			*/

			auto* prevFoot = reinterpret_cast<Header*>(byteHeader - headerSize);
			if (reinterpret_cast<byte*>(prevFoot) > MyBegin && prevFoot->free)
			{
				blocks.add(reinterpret_cast<byte*>(prevFoot) - (prevFoot->size + headerSize));
			}

			auto* nextHeader = reinterpret_cast<Header*>(byteHeader + (header->size + headerSize2));
			if (reinterpret_cast<byte*>(nextHeader) <= MyEnd)
			{
				blocks.add(reinterpret_cast<byte*>(nextHeader));
			}

			return blocks;
		}

		template<class T>
		void deallocate(T* ptr)
		{
			Header* header		= reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);
			Header* footer		= reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) + header->size);

			// Coalesce ajacent blocks
			// Adjust header size and pointer if we joined blocks
			ByteTrip btrip = coalesce(header);

			header->free = footer->free = true;

			policy.add(reinterpret_cast<byte*>(header), static_cast<size_type>(header->size), btrip);
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