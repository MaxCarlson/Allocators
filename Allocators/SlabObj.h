#pragma once
#include "SlabHelper.h"
#include <functional>

namespace SlabObjImpl
{
using byte = alloc::byte;

// TODO: Look into sorted vec so we can allocate more than one at a time!
// TODO: Slab Coloring
// TODO: Custom Alignement?
template<class T, class Cache, class Xtors>
struct Slab
{
	byte*								mem;
	size_t								count;
	std::vector<SlabImpl::IndexSizeT>	availible;
		
	Slab() : mem{ nullptr } {}
	Slab(size_t blockSize, size_t count) :
		mem{ reinterpret_cast<byte*>(operator new(sizeof(T) * count)) },
		count{ count },
		availible{ SlabImpl::vecMap[count] }
	{
		// TODO: Test coloring alingment here 
		// to prevent slabs from crowding cachelines
		for (auto i = 0; i < count; ++i)
			Cache::xtors->construct(reinterpret_cast<T*>(mem + sizeof(T) * i));
	}

	Slab(const Slab& other) = delete;

	Slab(Slab&& other) noexcept :
		mem{		other.mem },
		count{		other.count },
		availible{	std::move(other.availible) }
	{
		other.mem = nullptr;
	}

	Slab& operator=(Slab&& other) noexcept
	{
		mem			= other.mem;
		count		= other.count;
		availible	= std::move(other.availible);
		other.mem	= nullptr;
		return *this;
	}

	~Slab()
	{
		if (!mem)
			return;

		for (auto i = 0; i < count; ++i)
			reinterpret_cast<T*>(mem + sizeof(T) * i)->~T();

		operator delete(reinterpret_cast<void*>(mem));
	}

	bool full()			const noexcept { return availible.empty(); }
	size_t size()		const noexcept { return count - availible.size(); }
	bool empty()		const noexcept { return availible.size() == count; }

	std::pair<byte*, bool> allocate()
	{
		auto idx = availible.back();
		availible.pop_back();
		return { mem + (idx * sizeof(T)), availible.empty() };
	}

	template<class P>
	bool containsMem(P* ptr) const noexcept
	{
		return (reinterpret_cast<byte*>(ptr) >= mem
				&& reinterpret_cast<byte*>(ptr) < (mem + (sizeof(T) * count)));
	}

	// Destruct the object and return its index to the
	// list of availible memory. Re-initilize it to the required state
	void deallocate(T* ptr)
	{
		auto idx = static_cast<size_t>((reinterpret_cast<byte*>(ptr) - mem) / sizeof(T));
		availible.emplace_back(idx); 
		Cache::xtors->destruct(ptr); // This defaults to doing nothing, and will only do something if a dtor is passed
	}
};


template<class T, class Xtors>
struct Interface
{
	using size_type = size_t;
	using Cache		= SlabImpl::Cache<Slab<T, Interface<T, Xtors>, Xtors>>;
	inline static Cache storage;

	inline static Xtors* xtors = nullptr;

	// TODO: Should this also reconstruct all objects that 
	// haven't used this xtor and are availible? Yes, probably!
	static void setXtors(Xtors& tors) { xtors = &tors; }

	static void addCache(size_type count, Xtors& tors)
	{
		setXtors(tors);
		count	= alloc::nearestPageSz(count * sizeof(T)) / sizeof(T);
		SlabImpl::addToMap(count);
		storage = Cache{ sizeof(T), count };
	}

	static T* allocate()
	{
		return storage.allocate<T>();
	}

	static void deallocate(T* ptr)
	{
		storage.deallocate<T>(ptr);
	}

	static void freeAll()
	{
		storage.freeAll();
	}

	static void freeEmpty()
	{
		storage.freeEmpty();
	}

	static alloc::CacheInfo info()
	{
		return storage.info();
	}
};
}