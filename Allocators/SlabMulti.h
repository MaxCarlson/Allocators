#pragma once
#include "ImplSlabMulti.h"

namespace ImplSlabMulti
{

// Interface class for SlabMulti so that
// we can have multiple SlabMulti copies pointing
// to same allocator
struct Interface
{
	using size_type	= size_t;
	
	template<class T>
	friend class alloc::SlabMulti;

	Interface() :
		buckets{},
		refCount{ 1 }
	{}

	~Interface()
	{}

private:
	void registerThread(std::thread::id id)
	{
		buckets.emplace(id, std::move(Bucket{ id, fDeallocs }));
	}

public:

	template<class T>
	T* allocate(size_t count)
	{
		const auto bytes	= sizeof(T) * count;
		const auto id		= std::this_thread::get_id();

		auto alloc = [&](auto it, auto& vec) -> byte*
		{
			if(it != std::end(vec))
				return it->second.allocate(bytes);
			return nullptr;
		};

		byte* mem = buckets.findDo(id, alloc);

		if (!mem)
		{
			registerThread(id);
			mem = buckets.findDo(id, alloc);
		}

		return reinterpret_cast<T*>(mem);
	}

	template<class T>
	void deallocate(T* ptr, size_type n)
	{
		const auto id			= std::this_thread::get_id();
		const size_type bytes	= sizeof(T) * n;

		bool registered		= false;
		auto findAndDealloc = [&](auto it, auto& cont)
		{
			if (it != std::end(cont))
			{
				registered = true;
				return it->second.deallocate(ptr, n);
			}
			return false;
		};

		// Look in this threads Cache first
		bool found = buckets.findDo(id, findAndDealloc);

		// If the thread hasn't been registered
		// register it and try again
		if (!registered)
		{
			registerThread(id);
			found = buckets.findDo(id, findAndDealloc);
		}

		// We'll now add the deallocation to the list
		// of other foregin thread deallocations 
		if (!found)
			fDeallocs.addPtr(ptr, n, bytes, id);

		// Handle foreign thread deallocations
		// (if a thread that is not this one has said it needs to dealloc
		// memory that doesn't belong to it)
		if (fDeallocs.hasDeallocs(id))
		{
			fDeallocs.handleDeallocs(id, buckets);
		}
		
	}

	inline void incRef()
	{
		refCount.fetch_add(1, std::memory_order_relaxed);
	}

private:
	
	using Key = std::thread::id;
	using Val = Bucket;

	using MyCont = alloc::SmpMap<Key, Val, std::vector<std::pair<Key, Val>>>;

	MyCont				buckets;
	std::atomic<int>	refCount;
	ForeignDeallocs		fDeallocs;
};

}// End SlabMultiImpl::

namespace alloc 
{

template<class Type>
class SlabMulti
{
public:
	using STD_Compatible	= std::true_type;
	using Thread_Safe		= std::true_type;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

private:
	ImplSlabMulti::Interface* interfacePtr;

public:

	template<class U>
	friend class SlabMulti;

	template<class U>
	struct rebind { using other = SlabMulti<U>; };

	template<class U>
	bool operator==(const SlabMulti<U>& other) const noexcept { return other.interfacePtr == interfacePtr; }

	template<class U>
	bool operator!=(const SlabMulti<U>& other) const noexcept { return !(*this == other); }

	SlabMulti() :
		interfacePtr{ new ImplSlabMulti::Interface{} }
	{}

private:
	inline void decRef() noexcept
	{
		// TODO: Make sure this memory ordering is correct, It's probably not!
		if (interfacePtr->refCount.fetch_sub(1, std::memory_order_release) < 1)
			delete interfacePtr;
	}
public:

	~SlabMulti() 
	{
		decRef();
	}

	SlabMulti(const SlabMulti& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->incRef();
	}

	template<class U>
	SlabMulti(const SlabMulti<U>& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->incRef();
	}

	SlabMulti(SlabMulti&& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->incRef();
	}

	template<class U>
	SlabMulti(SlabMulti<U>&& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->incRef();
	}

	SlabMulti& operator=(const SlabMulti& other) noexcept 
	{
		if (interfacePtr)
			decRef();
		interfacePtr = other.interfacePtr;
	}

	template<class U>
	SlabMulti& operator=(const SlabMulti<U>& other) noexcept
	{
		if (interfacePtr)
			decRef();
		interfacePtr = other.interfacePtr;
	}

	SlabMulti& operator=(SlabMulti&& other) noexcept
	{
		if (interfacePtr)
			decRef();
		interfacePtr = other.interfacePtr;
	}

	template<class U>
	SlabMulti& operator=(SlabMulti<U>&& other) noexcept
	{
		if (interfacePtr)
			decRef();
		interfacePtr = other.interfacePtr;
	}

	template<class T = Type>
	T* allocate(size_t count = 1)
	{
		return interfacePtr->allocate<T>(count);
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{
		interfacePtr->deallocate(ptr, n);
	}
};

} // End alloc::