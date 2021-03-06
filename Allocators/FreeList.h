#pragma once
#include <map>
#include <list>
#include <vector>
#include <algorithm>
#include "AllocHelpers.h"

namespace FreeListImpl
{
enum AlSearch : byte
{
	BEST_FIT,
	FIRST_FIT
};

template<size_t bytes, class size_type,
	class Interface, class Storage>
	struct PolicyT
{
	//using Storage	= std::vector<std::pair<byte*, size_type>>;
	using It = typename Storage::iterator;
	using Ib = std::pair<It, bool>;
	using Header = typename Interface::Header;

	inline static Storage availible;

	template<class K, class V, class ...Args>
	It emplace(std::vector<std::pair<K, V>>& v, It it, Args&& ...args)
	{
		return v.emplace(it, std::forward<Args>(args)...);
	}

	template<class K, class V, class ...Args>
	It emplace(std::list<std::pair<K, V>>& l, It it, Args&& ...args)
	{
		return l.emplace(it, std::forward<Args>(args)...);
	}

	template<class K, class V, class ...Args>
	It emplace(std::map<K, V>& m, It it, Args&& ...args)
	{
		return m.emplace(std::forward<Args>(args)...).first;
	}

	// Lower bound impl for linear containers (vec/list)
	template<class C, class P>
	It lower_bound_linear(C& c, P* ptr)
	{
		return std::lower_bound(
			std::begin(c), std::end(c),
			ptr, [](auto& it1, auto& it2)
		{
			return it1.first < it2;
		});
	}

	template<class K, class V, class P>
	It lower_bound(std::vector<std::pair<K, V>>& v, P* ptr)
	{
		return lower_bound_linear(v, ptr);
	}

	template<class K, class V, class P>
	It lower_bound(std::list<std::pair<K, V>>& l, P* ptr)
	{
		return lower_bound_linear(l, ptr);
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

		if (it == std::end(availible) || it->first > start)
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
		for (auto it = std::begin(availible), 
			E = std::end(availible);
			it != E; ++it)
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

template<size_t bytes,
	template<size_t, class, class> class Policy>
struct PolicyInterface
{
	// Detect the minimum size type we can use
	// (based on max number of bytes) and use that as the
	// size_type to keep overhead as low as possible
	using size_type = typename alloc::FindSizeT<bytes, 0>::size_type;

	struct Header
	{
		size_type size;
		//size_type size : (sizeof(size_type) * 8) - 1;
		//size_type free : 1;
	};

	using OurType		= PolicyInterface<bytes, Policy>;
	using OurPolicy		= Policy<bytes, size_type, OurType>;
	using It			= typename OurPolicy::It;
	using Ib			= typename OurPolicy::Ib;

	// Handles how we store, find, 
	// and emplace free memory information
	inline static OurPolicy policy;

	inline static byte* MyBegin;
	inline static byte* MyEnd;

	inline static bool init						= true;
	inline static AlSearch search				= FIRST_FIT; // TODO: Get rid of?
	inline static size_type bytesFree			= bytes;
	inline static constexpr size_t headerSize	= sizeof(Header);

	static_assert(bytes > headerSize + 1, "Allocator size is smaller than minimum required.");

	PolicyInterface()
	{
		if (init)
		{
			// TODO: Should we just make this class use an  
			// array since it's size is compile time determined?
			init	= false;
			MyBegin	= reinterpret_cast<byte*>(operator new (bytes));
			MyEnd	= MyBegin + bytes;
			policy.add(MyBegin, bytes);
		}
	}

	byte* bestFit(const size_t byteCount)
	{
		return nullptr;
	}

	byte* firstFit(size_type reqBytes)
	{
		auto[it, found] = policy.firstFit(reqBytes);
		if (!found)
			return nullptr;

		auto* mem = it->first;
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
		byte* mem = nullptr;
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
		Header* header	= reinterpret_cast<Header*>(reinterpret_cast<byte*>(ptr) - headerSize);
		bytesFree		+= header->size + headerSize;

		policy.add(reinterpret_cast<byte*>(header), header->size);
	}

	void freeAll()
	{
		policy.freeAll(MyBegin);
		bytesFree = bytes;
	}
};
}

namespace alloc
{
// TODO: For all three policies, integrate the nodes
// storing info about the free memory blocks into the blocks
// themselves. Removing the need to the std::containers
//

// Store the 'list' of free nods in std::vector
template<size_t bytes, class size_type,
	class Interface>
	struct FlatPolicy 
	: public FreeListImpl::PolicyT<bytes, size_type, Interface, 
		std::vector<std::pair<byte*, size_type>>>
{};

// Store the 'list' of free nods in std::map
template<size_t bytes, class size_type,
	class Interface> 
	struct TreePolicy : public FreeListImpl::PolicyT<bytes, size_type, Interface,
	std::map<byte*, size_type>>
{};

template<size_t bytes, class size_type,
	class Interface>
	struct ListPolicy : public FreeListImpl::PolicyT<bytes, size_type, Interface,
	std::list<std::pair<byte*, size_type>>>
{};

template<class Type, size_t bytes, 
	template<size_t, class, class> class Policy = ListPolicy>
class FreeList
{
public:
	using STD_Compatible	= std::true_type;
	using Thread_Safe		= std::false_type;

	using OurPolicy			= FreeListImpl::PolicyInterface<bytes, Policy>;
	using size_type			= typename OurPolicy::size_type;
	using OurHeader			= typename OurPolicy::Header;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

private:
	inline static OurPolicy storage;
	static constexpr size_t size = bytes;

public:

	FreeList() = default;

	template<class U>
	FreeList(const FreeList<U, bytes, Policy>& other) {} // Note: Needed for debug mode to compile with std::containers

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
	T* allocate(size_type count = 1)
	{
		return storage.allocate<T>(static_cast<size_type>(count));
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{
		storage.deallocate(ptr);
	}

	void freeAll()
	{
		storage.freeAll();
	}
};
	
} // End alloc::