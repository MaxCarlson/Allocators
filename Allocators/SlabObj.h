#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <functional>

namespace SlabObj
{
	template<class... Args>
	struct DefaultXtor
	{
		template<class T>
		void construct(T* ptr) { new (ptr) T(); }

		template<class T>
		void destruct(T* ptr) { ptr->~T(); }
	};
}

// TODO: Better naming for these!
namespace alloc
{
	// Holds a tuple of initilization arguments
	// for constructing classes of a type T in Slab Allocators
	// object pool
	// Can be replaced with a XtorFunc (with a function that takes T&)
	template<class... Args>
	struct CtorArgs : public SlabObj::DefaultXtor<Args...>
	{
		CtorArgs(Args ...args) : args{ std::forward<Args>(args)... } {}
		std::tuple<Args ...> args;

		// Build T in place with tuple args expanded into
		// individual arguments for T's Ctor
		template <class T, class Tuple, size_t... Is>
		void construct_from_tuple(T* ptr, Tuple&& tuple, std::index_sequence<Is...>) {
			new (ptr) T( std::get<Is>(std::forward<Tuple>(tuple))... );
		}

		template <class T, class Tuple>
		void construct_from_tuple(T* ptr, Tuple&& tuple) {
			construct_from_tuple(ptr, std::forward<Tuple>(tuple),
				std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{}
			);
		}

		template<class T>
		void operator()(T& t) { construct_from_tuple(&t, args); }

		template<class T>
		void construct(T* ptr) { this->operator()(*ptr); }
	};

	// For constructing objects in SlabObj's Cache
	// from llambdas or other functions. Must pass a function
	// that takes a reference to the type of object being constructed
	// [](T&){}
	// Can be used as both a Ctor & Dtor
	template<class Func>
	struct XtorFunc : SlabObj::DefaultXtor<Func>
	{
		XtorFunc(Func& func) : func(func) {}
		Func& func;

		template<class T>
		void operator()(T& t) { func(t); }

		template<class T>
		void construct(T* ptr) { this->operator()(*ptr); }
	};

	// Handles default cases for Xtors and calls
	// T's destructor
	struct DefaultDtor 
	{
		template<class T>
		void operator()(T& t) { t.~T(); }
	};

	inline static DefaultDtor			defaultDtor; // Handles default destructions
	inline static SlabObj::DefaultXtor	defaultXtor; // Handles default construction/destruction

	// TODO: Add const?
	// Wrapper struct for when you need both a Ctor &
	// a Dtor for Slab allocators object caches
	// Ctor: CtorArgs or XtorFunc
	// Dtor: XtorFunc wrapping a llamda (defaults to default dtor)
	template<class Ctor, class Dtor = DefaultDtor>
	struct Xtors
	{
		Xtors(Ctor& ctor, Dtor& dtor = defaultDtor) : ctor(ctor), dtor(dtor) {}
		Ctor& ctor;
		Dtor& dtor;

		template<class T>
		void construct(T* ptr) { ctor(*ptr); }

		template<class T>
		void destruct(T* ptr) { dtor(*ptr); }
	};
}

namespace SlabObj
{
	using byte = alloc::byte;

	// TODO: Look into sorted vec so we can allocate more than one at a time!
	// TODO: Slab Coloring
	// TODO: Custom Alignement?
	template<class T, class Cache, class Xtors>
	struct Slab
	{
		byte* mem;
		size_t count;
		std::vector<uint16_t> availible;
		
		Slab() = default;
		Slab(size_t count) : count(count), availible(count)
		{
			//mem = alloc::allocatePage<byte>(count);
			mem = reinterpret_cast<byte*>(operator new(sizeof(T) * count));
			std::iota(std::rbegin(availible), std::rend(availible), 0);

			for (auto i = 0; i < count; ++i)
				Cache::xtors->construct(reinterpret_cast<T*>(mem + sizeof(T) * i));
		}

		bool full()			const noexcept { return availible.empty(); }
		size_t size()		const noexcept { return count - availible.size(); }
		bool empty()		const noexcept { return availible.size() == count; }

		std::pair<byte*, bool> allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

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
			Cache::xtors->destruct( ptr);
			Cache::xtors->construct(ptr);
		}
	};

	template<class T, class Xtors>
	struct Cache
	{
		using size_type = size_t;
		using Storage	= alloc::List<Slab<T, Cache, Xtors>>;
		using It		= typename Storage::iterator;

		inline static Storage slabsFull;
		inline static Storage slabsPart;
		inline static Storage slabsFree;

		inline static size_type mySize		= 0;	// Total objects
		inline static size_type perCache	= 0;	// Objects per cache
		inline static size_type myCapacity	= 0;	// Total capacity for objects without more allocations

		inline static Xtors* xtors = nullptr;

		// TODO: Should this also reconstruct all objects that 
		// haven't used this ctor and are availible? Yes, probably!
		static void setXtors(Xtors& tors) { xtors = &tors; }

		// TODO: Should we only allow this function to change count on init?
		static void addCache(size_type count, Xtors& tors)
		{
			setXtors(tors);

			perCache	= count;
			newSlab();
		}

		static void newSlab()
		{
			slabsFree.emplace_back(perCache);
			myCapacity += perCache;
		}

		// TODO: Right now this code is basically identical to SlabMem's
		// Figure out if it's worth it to combine the functionality (it may start to split off as I add features)
		//
		static std::pair<Storage*, It> findSlab()
		{
			It slabIt;
			Storage* store = nullptr;
			if (!slabsPart.empty())
			{
				slabIt	= std::begin(slabsPart);
				store	= &slabsPart;
			}
			else
			{
				// No empty slabs, need to create one! (TODO: If allowed to create?)
				if (slabsFree.empty())
					newSlab();

				slabIt	= std::begin(slabsFree);
				store	= &slabsFree;
			}

			return { store, slabIt };
		}

		static T* allocate()
		{
			auto[store, it] = findSlab();
			auto[mem, full] = it->allocate();

			// If we're taking memory from a free slab
			// add it to the list of partially full slabs
			if (store == &slabsFree)
				store->giveNode(it, slabsPart, std::begin(slabsPart));

			// Give the slab storage to the 
			// full list if it has no more room
			if (full)
				store->giveNode(it, slabsFull, std::begin(slabsFull));

			++mySize;
			return reinterpret_cast<T*>(mem);
		}

		template<class P>
		static std::pair<Storage*, It> searchStore(Storage& store, P* ptr)
		{
			for (auto it = std::begin(store);
				it != std::end(store); ++it)
				if (it->containsMem(ptr))
					return { &store, it };
			return { &store, store.end() };
		}

		static void deallocate(T* ptr)
		{
			auto[store, it] = searchStore(slabsFull, ptr);
			// Need to move slab back into partials
			if (it != slabsFull.end())
				slabsFull.giveNode(it, slabsPart, slabsPart.begin());
			else
			{
				// TODO: Super ugly. Due to not being able to structured bind already initlized variables
				auto[s, i] = searchStore(slabsPart, ptr);
				store = s;
				it = i;
			}

			if (it == slabsPart.end())
				throw std::bad_alloc(); // TODO: Is this the right exception?

			it->deallocate(ptr);

			// Return slab to free list if it's empty
			if (it->empty())
				store->giveNode(it, slabsFree, std::begin(slabsFree));

			--mySize;
		}

		static alloc::CacheInfo info() noexcept
		{
			return { mySize, myCapacity, sizeof(T), perCache };
		}
	};

	struct Interface
	{
		using size_type = size_t;

		template<class T, class Xtors>
		void addCache(size_type count, Xtors& tors)
		{
			Cache<T, Xtors>::addCache(count, tors);
		}

		template<class T, class Xtors>
		T* allocate()
		{
			return Cache<T, Xtors>::allocate();
		}

		template<class T, class Xtors>
		void deallocate(T* ptr)
		{
			Cache<T, Xtors>::deallocate(ptr);
		}

		template<class T, class Xtors>
		alloc::CacheInfo info() const noexcept
		{
			return Cache<T, Xtors>::info();
		}
	};
}