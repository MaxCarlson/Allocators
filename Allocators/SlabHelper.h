#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <map>

namespace SlabImpl
{
	static constexpr auto MAX_SLAB_SIZE = 65535; // Max number of memory blocks a Slab can be divided into 

	using IndexSizeT = typename alloc::FindSizeT<MAX_SLAB_SIZE>::size_type;
	
	
	static std::map<size_t, std::vector<IndexSizeT>> vecMap;
	inline void addToMap(size_t count)
	{
		auto find = vecMap.find(count);
		if (find == std::end(vecMap)) // Build the vector of free spots we use internally in the Slabs
		{
			std::vector<IndexSizeT> avail(count);
			std::iota(std::rbegin(avail), std::rend(avail), 0);
			vecMap.emplace(count, std::move(avail));
		}
	}

	template<class Slab>
	struct Cache
	{
		using size_type = size_t;
		using SlabStore = std::vector<Slab>;
		using It		= typename SlabStore::iterator;

		size_type count;
		size_type blockSize;
		size_type myCapacity;
		size_type mySize;

		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;

		Cache() = default;
		Cache(size_type blockSize, size_type count) : 
			count{		count },
			blockSize{	blockSize },
			myCapacity{ 0 },
			mySize{		0 }
		{
			newSlab();
		}

		Cache(Cache&& other) = default;
		Cache& operator=(Cache&& other) = default;

		size_type size()						const noexcept { return mySize; }
		size_type capacity()					const noexcept { return myCapacity; }
		bool operator<(const Cache& other)		const noexcept { return size() < other.size(); }
		alloc::CacheInfo info()					const noexcept { return { size(), capacity(), blockSize, count }; }

		void newSlab()
		{
			slabsFree.emplace_back(count);
			myCapacity += count;
		}

		std::pair<SlabStore*, It> findFreeSlab()
		{
			It slabIt;
			SlabStore* store = nullptr;
			if (!slabsPart.empty())
			{
				slabIt = std::begin(slabsPart);
				store = &slabsPart;
			}
			else
			{
				if (slabsFree.empty())
					newSlab();

				slabIt = std::begin(slabsFree);
				store = &slabsFree;
			}

			return { store, slabIt };
		}

		It splice(SlabStore& to, It pos, SlabStore& from, It it)
		{
			to.emplace_back(std::move(*it));
			std::swap(*it, from.back());
			from.pop_back();
			return std::end(to) - 1;
		}

		template<class T>
		T* allocate()
		{
			auto[store, it] = findFreeSlab();
			auto[mem, full] = it->allocate();

			// If we're taking memory from a free slab
			// add it to the list of partially full slabs
			if (store == &slabsFree)
				splice(slabsPart, std::end(slabsPart), *store, it);

			// Give the slab storage to the 
			// full list if it has no more room // TODO: Make this else if!!!!!
			if (full)
				splice(slabsFull, std::end(slabsFull), *store, it);


			++mySize;
			return reinterpret_cast<T*>(mem);
		}

		// Search slabs for one that holds the memory
		// ptr points to
		//
		// TODO: We should implement a coloring offset scheme
		// so that some Slabs store objects at address of address % 8 == 0
		// and others at addresses % 12 == 0 so we can search faster for the proper Slab
		template<class P>
		std::pair<SlabStore*, It> searchStore(SlabStore& store, P* ptr)
		{
			for (auto it = std::rbegin(store); // TODO: Look into keeping in sorted memory order so we can lower_bound here?
				it != std::rend(store); ++it)
				if (it->containsMem(ptr))
					return { &store, it.base() - 1 };
			return { &store, store.end() };
		}

		template<class T>
		void deallocate(T* ptr)
		{
			auto[store, it] = searchStore(slabsFull, ptr);
			// Need to move slab back into partials
			if (it != slabsFull.end())
				it = splice(slabsPart, std::end(slabsPart), slabsFull, it);

			else
			{
				// TODO: Super ugly. Due to not being able to structured bind already initlized variables
				auto[s, i] = searchStore(slabsPart, ptr);
				store = s;
				it = i;
			}

			it->deallocate(ptr);

			// Return slab to free list if it's empty
			if (it->empty())
				splice(slabsFree, std::end(slabsFree), *store, it);

			--mySize;
		}

		void freeAll()
		{
			slabsFree.clear();
			slabsPart.clear();
			slabsFull.clear();
			myCapacity = 0;
			mySize = 0;
		}

		void freeEmpty()
		{
			myCapacity -= slabsFree.size() * blockSize;
			slabsFree.clear();
		}
	};
}

// Below this line belongs only to SlabObj functionality
// as well as user templates for SlabObj

namespace SlabObjImpl
{
	template<class... Args>
	struct DefaultXtor
	{
		template<class T>
		static void construct(T* ptr) { new (ptr) T(); }

		template<class T>
		static void destruct(T* ptr) {}
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
	struct CtorArgs : public SlabObjImpl::DefaultXtor<Args...>
	{
		CtorArgs(Args ...args) : args{ std::forward<Args>(args)... } {}
		std::tuple<Args ...> args;

		// Build T in place with tuple args expanded into
		// individual arguments for T's Ctor
		template <class T, class Tuple, size_t... Is>
		void construct_from_tuple(T* ptr, Tuple&& tuple, std::index_sequence<Is...>) {
			new (ptr) T(std::get<Is>(std::forward<Tuple>(tuple))...);
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
	struct XtorFunc : SlabObjImpl::DefaultXtor<Func>
	{
		XtorFunc(Func& func) : func(func) {}
		Func& func;

		template<class T>
		void operator()(T& t) { func(t); }

		template<class T>
		void construct(T* ptr) { this->operator()(*ptr); }
	};

	// Handles the default case in object pools
	// and does nothing when objects are given back
	// to a cache in the pool. When a cache is freed
	// the objects destructor is called.
	struct DefaultDtor
	{
		template<class T>
		void operator()(T& t) {}
	};

	inline static DefaultDtor				defaultDtor; // Handles default destructions (Does nothing)
	inline static SlabObjImpl::DefaultXtor	defaultXtor; // Handles default construction/destruction (Default ctor w/ placement new) 

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
