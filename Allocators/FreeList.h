#pragma once
#include <map>
#include <list>
#include <vector>
#include <algorithm>
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

	template<size_t bytes, class size_type,
		class Interface, class Storage>
	struct PolicyT
	{
		//using Storage	= std::vector<std::pair<byte*, size_type>>;
		using It		= typename Storage::iterator; 
		using Ib		= std::pair<It, bool>;
		using Header	= typename Interface::Header;

		inline static Storage availible;

		template<class K, class V, class ...Args>
		It emplace(std::map<K, V>& m, It it, Args&& ...args)
		{
			return m.emplace(std::forward<Args>(args)...).first;
		}

		template<class K, class V, class ...Args>
		It emplace(std::vector<std::pair<K, V>>& v, It it, Args&& ...args)
		{
			return v.emplace(it, std::forward<Args>(args)...);
		}

		template<class K, class V, class P>
		It lower_bound(std::vector<std::pair<K, V>>& v, P* ptr)
		{
			return std::lower_bound(
				std::begin(v), std::end(v),
				ptr, [](auto& it1, auto& it2)
			{
				return it1.first < it2;
			});
		}

		template<class K, class V, class P>
		It lower_bound(std::map<K, V>& m, P* ptr)
		{
			return m.lower_bound(ptr);
		}

		void add(byte* start, size_type size)
		{
			if (availible.empty())
			{
				emplace(availible, std::end(availible), start, size);
				return;
			}

			auto it = lower_bound(availible, start);

			if (it->first > start)
				it = emplace(availible, it, start, size + Interface::headerSize);

			// Perform coalescence of adjacent blocks
			It next = it, prev = it;
			++next;

			if (next != std::end(availible) &&
				it->first + it->second == next->first)
			{
				it->second += next->second;
				availible.erase(next);
			}
			if (prev != std::begin(availible) &&
				(--prev)->first + prev->second == it->first)
			{
				prev->second += it->second;
				availible.erase(it);
			}
		}

		Ib firstFit(size_t reqBytes)
		{
			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				// Found a section of memory large enough to hold
				// what we want to allocate
				if (it->second >= reqBytes)
					return { it, true };
			}
			return { It{}, false };
		}

		void erase(It it)
		{
			availible.erase(it);
		}

		void freeAll(byte* MyBegin)
		{
			availible.clear();
			emplace(availible, std::end(availible), MyBegin, bytes);
		}
	};

	template<size_t bytes, class size_type,
		class Interface>
		struct FlatPolicy 
		: public PolicyT<bytes, size_type, Interface, 
		  std::vector<std::pair<byte*, size_type>>>
	{
	};

	template<size_t bytes, class size_type,
		class Interface> 
		struct TreePolicy : public PolicyT<bytes, size_type, Interface,
		std::map<byte*, size_type>>
	{};

	template<size_t bytes, class size_type,
		class Interface>
	struct ListPolicy
	{
		using Storage	= std::list<std::pair<byte*, size_type>>;
		using It		= typename Storage::iterator;
		using Ib		= std::pair<It, bool>;
		using Header	= typename Interface::Header;

		inline static Storage availible;

		void add(byte* start, size_type size)
		{
			if (availible.empty())
			{
				availible.emplace_back(start, size);
				return;
			}

			// Add to linked list in memory
			// sorted order
			It it = std::begin(availible);
			for (it; it != std::end(availible); ++it)
				if (it->first > start)
				{
					it = availible.emplace(it, start, size + Interface::headerSize);
					break;
				}

			// Perform coalescence of adjacent blocks
			It next = it, prev = it;
			++next;

			if (next != std::end(availible) && 
				it->first + it->second == next->first)
			{
				it->second += next->second;
				availible.erase(next);
			}
			if (prev != std::begin(availible) &&
				(--prev)->first + prev->second == it->first)
			{
				prev->second += it->second;
				availible.erase(it);
			}
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

		void erase(It it)
		{
			availible.erase(it);
		}

		void freeAll(byte* MyBegin)
		{
			availible.clear();
			availible.emplace_front(MyBegin, bytes);
		}
	};

	template<size_t bytes,
		template<size_t, class, class> class Policy>
	struct PolicyInterface
	{
		// Detect the minimum size type we can use
		// (based on max number of bytes) and use that as the
		// size_type to keep overhead as low as possible
		using size_type = typename FindSizeT<bytes, 0>::size_type;

		// TODO: The footers size field can be removed and replaced
		// with 1bit denoting free/empty
		struct Header
		{
			size_type size;
			//size_type size : (sizeof(size_type) * 8) - 1;
			//size_type free : 1;
		};

		using OurType	= PolicyInterface<bytes, Policy>;
		using OurPolicy = Policy<bytes, size_type, OurType>;
		using It		= typename OurPolicy::It;
		using Ib		= typename OurPolicy::Ib;
		
		// Handles how we store, find, 
		// and emplace free memory information
		inline static OurPolicy policy;

		inline static byte* MyBegin;
		inline static byte* MyEnd;

		inline static bool init						= true;
		inline static AlSearch search				= FIRST_FIT;
		inline static size_type bytesFree			= bytes;
		inline static constexpr size_t headerSize	= sizeof(Header);

		static_assert(bytes > headerSize + 1, "Allocator size is smaller than minimum required.");

		PolicyInterface()
		{
			if (init)
			{
				// TODO: Should we just make this class use an  
				// array since it's size is compile time determined?
				init		= false;
				MyBegin		= reinterpret_cast<byte*>(operator new (bytes));
				MyEnd		= MyBegin + bytes;
				policy.add(MyBegin, bytes);
			}
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


			return writeHeader(mem, reqBytes - headerSize);
		}

		// Writes the header and adjusts the returned 
		// pointer to user memory
		byte* writeHeader(byte* start, size_type size)
		{
			*reinterpret_cast<Header*>(start) = Header{ size };
			return start + headerSize;
		}

		template<class T>
		T* allocate(size_type count)
		{
			byte* mem			= nullptr;
			size_type reqBytes = sizeof(T) * count + headerSize;

			if (search == FIRST_FIT)
				mem = firstFit(reqBytes);
			else
				mem = bestFit(reqBytes);

			if (!mem)
				throw std::bad_alloc();

			bytesFree -= reqBytes;

			return reinterpret_cast<T*>(mem);
		}

		template<class T>
		void deallocate(T* ptr)
		{
			Header* header = reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);
			bytesFree += header->size + headerSize;

			policy.add(reinterpret_cast<byte*>(header), header->size);
		}

		void freeAll()
		{
			policy.freeAll(MyBegin);
			bytesFree = bytes;
		}
	};

	template<class Type, size_t bytes, 
		template<size_t, class, class> class Policy = ListPolicy>
	class FreeList
	{
	public:
		using OurPolicy = PolicyInterface<bytes, Policy>;
		using size_type = typename OurPolicy::size_type;
		using OurHeader	= typename OurPolicy::Header;

	private:
		inline static OurPolicy storage;
		static constexpr size_t size = bytes;

	public:

		FreeList() = default;

		template<class U>
		bool operator==(const FreeList<U, bytes, Policy>& other) const noexcept
		{
			return true;
		}

		template<class U>
		bool operator!=(const FreeList<U, bytes, Policy>& other) const noexcept
		{
			return false;
		}

		template<class U>
		struct rebind { using other = FreeList<U, bytes, Policy>; };

		template<class T = Type>
		T* allocate(size_type count)
		{
			return storage.allocate<T>(static_cast<size_type>(count));
		}

		template<class T = Type>
		T* allocate()
		{
			return storage.allocate<T>(static_cast<size_type>(1));
		}

		template<class T = Type>
		void deallocate(T* ptr)
		{
			storage.deallocate(ptr);
		}

		void freeAll()
		{
			storage.freeAll();
		}
	};
	
}