#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <functional>

namespace alloc
{
	// Holds a tuple of initilization arguments
// for constructing classes of a type in Slab Allocators
// object pool
// Can be replaced with a CtorFunction
	template<class... Args>
	struct CtorArgs
	{
		CtorArgs(Args ...args) : args{ std::forward<Args>(args)... } {}
		std::tuple<Args ...> args;

		template <class T, class Tuple, size_t... Is>
		T construct_from_tuple(Tuple&& tuple, std::index_sequence<Is...>) {
			return T{ std::get<Is>(std::forward<Tuple>(tuple))... };
		}

		template <class T, class Tuple>
		T construct_from_tuple(Tuple&& tuple) {
			return construct_from_tuple<T>(std::forward<Tuple>(tuple),
				std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{}
			);
		}

		// TODO: Lets avoid the copy constructor here, need to create in place!!
		template<class T>
		void operator()(T& t)
		{
			t = construct_from_tuple<T>(args);
		}
	};

	// For constructing objects in SlabObj's Cache
	// from llambdas or other functions. Must pass a function
	// that takes a reference to the type of object being constructed
	// [](T&){}
	template<class Func>
	struct XtorFunc
	{
		XtorFunc(Func& func) : func(func) {}
		Func& func;

		template<class T>
		void operator()(T& t)
		{
			func(t);
		}
	};

	// Handles default cases for Xtors and calls
	// T's destructor
	struct DefaultDtor
	{
		template<class T>
		void operator()(T& t) { t.~T(); }
	};

	// Default template parameter for Slab.addObjCache
	struct DefaultXtor
	{
		template<class T>
		void construct(T* ptr) { *ptr = T{}; }

		template<class T>
		void destruct(T* ptr) { ptr->~T(); }
	};

	inline static DefaultDtor defaultDtor; // Handles default destructions
	inline static DefaultXtor defaultXtor; // Handles default construction/destruction

	// TODO: Better Name!
	// TODO: Add const
	template<class Ctor, class Dtor = DefaultDtor>
	struct Xtors
	{
		//ObjPrimer() = default; // TODO: Need default versions of ctor/dtor that do nothing (pref with no ops)
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

	// TODO: Or just keep a Cache* to our parent?
	// TODO:+ How will we consntruct objects with the ctor if they
	// are not default constructable? 
	// TODO: Prefer to find some type of tuple/variadic init if possible
	// TODO: Slab Coloring
	// TODO: Custom Alignement?
	template<class T, class Cache, class Xtors>
	struct Slab
	{
		byte* mem;
		std::vector<uint16_t> availible;
		
		Slab() = default;
		Slab(size_t count) : availible(count)
		{
			mem = reinterpret_cast<byte*>(operator new(sizeof(T) * count));
			std::iota(std::rbegin(availible), std::rend(availible), 0);

			for (auto i = 0; i < count; ++i)
				Cache::xtors->construct(reinterpret_cast<T*>(mem + sizeof(T) * i));
		}

		std::pair<byte*, bool> allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * sizeof(T)), availible.empty() };
		}

		void deallocate(T* ptr)
		{

		}
	};

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T, class Xtors>
	struct Cache
	{
		using size_type = size_t;
		using Storage	= alloc::List<Slab<T, Cache, Xtors>>;
		using It		= typename Storage::iterator;

		inline static Storage slabsFull;
		inline static Storage slabsPart;
		inline static Storage slabsFree;

		inline static size_type mySize;		// Total objects
		inline static size_type perCache;	// Objects per cache
		inline static size_type myCapacity; // Total capacity for objects without more allocations

		inline static Xtors* xtors;

		static void addCache(size_type count, Xtors& tors)
		{
			// TODO: Should we only allow this function to change count on init?
			xtors		= &tors;
			perCache	= count;
			newSlab();
		}

		static void newSlab()
		{
			slabsFree.emplace_back(perCache);
			myCapacity += perCache;
		}

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

		static void deallocate(T* ptr)
		{

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
	};
}