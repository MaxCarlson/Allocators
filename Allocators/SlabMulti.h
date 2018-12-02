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

private:

	using Key			= std::thread::id;
	using Val			= Bucket;
	using BucketPair	= std::pair<Key, Val>;
	using MyCont		= alloc::SmpMap<Key, Val, std::vector<BucketPair>>;


	MyCont				buckets;
	std::atomic<int>	refCount;

public:

	Interface() :
		buckets{},
		refCount{ 1 }
	{}

	~Interface()
	{}

private:
	void registerThread(std::thread::id id)
	{
		buckets.emplace(id, std::move(Bucket{}));
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

		// Attempt to deallocate ptr in this threads Bucket first
		bool registered	= false;
		bool found		= buckets.findDo(id, [&](auto it, auto& cont)
		{
			if (it != std::end(cont))
			{
				registered = true;
				return it->second.deallocate(ptr, bytes, true);
			}
			return false;
		});

		// If we don't find the ptr, we now need to try and deallocate
		// the ptr in other threads (Buckets)
		if (!found)
			buckets.iterate([&](BucketPair& pair) -> bool
			{
				if(pair.first != id)
					found = pair.second.deallocate(ptr, bytes, false);
				return found;
			});

		// If the thread hasn't been registered
		// register it (It can't possibly have allocated the memory if it hasn't been registered)
		if (!registered)
			registerThread(id);
	}

	void incRef() 
	{
		refCount.fetch_add(1, std::memory_order_relaxed);
	}
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