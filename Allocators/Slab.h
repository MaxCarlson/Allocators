#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <array>

// A custom linked list class with a couple
// extra operations
template<class T>
class List
{
	struct Node
	{
		T		data;
		Node*	next = nullptr;
		Node*	prev = nullptr;
	};

	struct iterator
	{

	};

	Node* head;
	Node* tail;

	template<class... Args>
	Node* constructNode(Args&& ...args)
	{
		Node* n = new Node;
		new (&n->data) T(std::forward<Args>(args)...);
		return n;
	}

public:

	template<class... Args>
	iterator emplace_back(Args&& ...args)
	{
		Node* n = constructNode(std::forward<Args>(args)...);
		//std::allocator<int>::construct()
		return iterator{};
	}
};

namespace alloc
{
	template<class T>
	struct ObjSlab
	{

	};

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T>
	struct ObjCache
	{
		using Storage = std::list<ObjSlab<T>>;

		inline static Storage full;
		inline static Storage free;

	};

	struct SmallSlab
	{
	private:
		byte* mem;
		byte* end;
		std::vector<uint16_t> availible;
		
	public:

		SmallSlab() = default;
		SmallSlab(size_t objSize, size_t count)
		{
			mem = reinterpret_cast<byte*>(operator new(objSize * count));
			end = mem + count;
			availible.resize(count);
			for (auto i = 0; i < count; ++i)
				availible[i] = i;
		}

		//~SmallSlab(){ delete mem; }
		// TODO: Need a manual destroy func if using vec and moving between vecs?

		bool full() const noexcept { return availible.empty(); }

		std::pair<byte*, bool> allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + idx, availible.empty() };
		}

		void deallocate(byte* ptr)
		{
			auto idx = static_cast<size_t>(ptr - mem);
			availible.emplace_back(idx);
		}

		bool containsMem(byte* ptr) const noexcept
		{
			return (ptr >= mem && ptr < end);
		}
	};


	// Cache's based on memory size
	// Not designed around particular objects
	struct SmallCache
	{
		// TODOLIST:
		// TODO: Locking mechanism
		// TODO: Page alignemnt/Page Sizes for slabs? (possible on windows?)
		
		using size_type = size_t;
		using SlabStore = std::list<SmallSlab>;

		size_type objSize;
		size_type count;
		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		SmallCache() = default;
		SmallCache(size_type objSize, size_type count) : objSize(objSize), count(count)
		{
			newSlab(objSize, count);
		}

		bool operator<(const SmallCache& other) const noexcept
		{
			return objSize < other.objSize;
		}

		void newSlab(size_type objSize, size_type count)
		{
			slabsFree.emplace_back(SmallSlab{ objSize, count });
		}

		void freeEmpty()
		{

		}

		template<class T>
		T* allocate()
		{
			auto [mem, full] = slabsFree.back().allocate();

			if (full)
			{
				slabsFull.emplace_back(slabsFree.back());
				slabsFree.pop_back(); // TODO: This is an issue, it calls destructor and deletes mem which we don't want
			}
		}

		template<class T>
		void deallocate(T* ptr)
		{
			// Search slabs for one that holds the memory
			// ptr points to
			// Look backwards, where the likely most recent items are
			auto searchSS = [&ptr](SlabStore& store) -> SmallSlab*
			{
				for (auto it =  std::rbegin(store); 
						  it != std::rend(store); ++it)
					if (it->containsMem(ptr))
						return &(*it);
				return nullptr;
			};

			auto* ssb = searchSS(slabsFull);
			if (!ssb)
				ssb = searchSS(slabsFree);
			if (!ssb)
				throw std::bad_alloc(); // TODO: Is this the right exception?

			ssb->deallocate(ptr);
		}
	};


	struct SlabMemInterface
	{
		using size_type		= size_t;
		using SmallStore	= std::vector<SmallCache>;
		using It			= typename SmallStore::iterator;

		inline static SmallStore caches;

		void addCache(size_type objSize, size_type count)
		{
			SmallCache ch{ objSize, count };
			if (caches.empty())
			{
				caches.emplace_back(ch);
				return;
			}

			for(It it = std::begin(caches); it != std::end(caches); ++it)
				if (ch < *it)
				{
					caches.emplace(it, ch);
					break;
				}
		}

		template<class T>
		T* allocate()
		{

		}

		template<class T>
		void deallocate(T* ptr)
		{

		}
	};

	struct SlabObjInterface
	{
		using size_type		= size_t;
		//using SmallStore	= std::vector<SmallCache>;
		//using It			= typename SmallStore::iterator;

		template<class T>
		void addCache(size_type count)
		{

		}
	};

	template<class Type>
	class Slab
	{

		inline static SlabMemInterface memStore;
		inline static SlabObjInterface objStore;

	public:

		using size_type = size_t;


		// Does not take a count argument because 
		// we can only allocate one object at a time
		template<class T = Type>
		T* allocateMem()
		{
			return memStore.allocate<T>();
		}

		template<class T = Type>
		void deallocateMem(T* ptr)
		{
			memStore.deallocate(ptr);
		}

		void addMemCache(size_type objSize, size_type count)
		{
			memStore.addCache(objSize, count);
		}

		template<class T = Type>
		void addMemCache(size_type count)
		{
			addCache(sizeof(T), count);
		}

		template<class T = Type>
		void addObjCache(size_type count)
		{
			objStore.addCache<T>(count);
		}
	};
}