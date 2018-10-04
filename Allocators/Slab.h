#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <array>

// A custom linked list class with a couple
// extra operations
template<class T>
class List
{
public:

	struct Node
	{
		Node() = default;

		template<class... Args>
		Node(Args&&... args) : data{ std::forward<Args>(args)... } {}

		T		data;
		Node*	next = nullptr;
		Node*	prev = nullptr;
	};

	struct iterator
	{
		iterator(Node* ptr = nullptr) : ptr(ptr) {}

		T& operator*()
		{
			return ptr->data;
		}

		T* operator->()
		{
			return &ptr->data;
		}

		iterator& operator++()
		{
			if (ptr != MyEnd)
				ptr = ptr->next;
		}

		iterator operator++(int)
		{
			iterator tmp = *this;
			++*this;
			return tmp;
		}

		Node* ptr;
	};

	List()
	{
		MyHead.prev = MyHead.next = nullptr;
		MyHead.next = &MyEnd;
		MyEnd.prev	= &MyHead;
		MySize		= 0;
	}

private:

	Node MyEnd;
	Node MyHead;
	size_t MySize;

	template<class... Args>
	Node* constructNode(Args&& ...args)
	{
		Node* n = new Node{ std::forward<Args>(args)... };
		return n;
	}

	iterator insertAt(Node* n, iterator it)
	{
		Node* prev	= &it.prev;
		prev->next	= n;
		n->next		= *it;
		n->prev		= prev;

		++MySize;
		return iterator{ n };
	}

public:

	iterator begin() { return iterator{ MyHead.next }; }
	iterator end() { return iterator{ &MyEnd }; }
	size_t size() const noexcept { return MySize; }
	bool empty() const noexcept { return !MySize; }

	// Give another list our node and insert it
	// before pos. Don't deallocate the memory,
	// just pass it to another list to handle
	void giveNode(iterator& ourNode, List& other, iterator pos)
	{
		--MySize;
		Node* n				= ourNode.ptr;
		ourNode.prev->next	= ourNode.next;
		ourNode.next->prev	= ourNode.prev;

		other.insertAt(ourNode.ptr, pos);
	}

	template<class... Args>
	decltype(auto) emplace_back(Args&& ...args)
	{
		Node* n		= constructNode(std::forward<Args>(args)...);
		iterator it = insertAt(n, end());
		return *it;
	}

	template<class... Args>
	iterator emplace(iterator it, Args&& ...args)
	{
		Node* n	= constructNode(std::forward<Args>(args)...);
		return insertAt(n, it);
	}

	iterator erase(iterator pos)
	{
		--MySize;
		pos.ptr->prev->next = pos.ptr->next;
		pos.ptr->next->prev = pos.ptr->prev;

		// TODO: Don't deallocate memory until asked to?
		// Add it to a chain after MyEnd so we don't need to allocate more later?
		delete pos.ptr;
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
		using size_type = size_t;

		byte* mem;
		size_type objSize;
		size_type count;
		std::vector<uint16_t> availible;
		
	public:

		SmallSlab() = default;
		SmallSlab(size_t objSize, size_t count) : objSize(objSize), count(count)
		{
			mem = reinterpret_cast<byte*>(operator new(objSize * count));
			availible.resize(count);
			for (auto i = 0; i < count; ++i)
				availible[i] = i;
		}

		//~SmallSlab(){ delete mem; }
		// TODO: Need a manual destroy func if using vec and moving between vecs?

		bool full() const noexcept { return availible.empty(); }
		size_type size() const noexcept { return count - availible.size(); }

		std::pair<byte*, bool> allocate()
		{
			if (availible.empty()) // TODO: This should never happen?
				return { nullptr, false };

			auto idx = availible.back();
			availible.pop_back();
			return { mem + (idx * objSize), availible.empty() };
		}

		void deallocate(byte* ptr)
		{
			auto idx = static_cast<size_t>(ptr - mem);
			availible.emplace_back(idx);
		}

		bool containsMem(byte* ptr) const noexcept
		{
			return (ptr >= mem && ptr < (mem + (objSize * count)));
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
		using Slab		= SmallSlab;
		using SlabStore = List<Slab>;
		using It		= SlabStore::iterator;

		size_type objSize;
		size_type count;
		SlabStore slabsFree;
		SlabStore slabsPart;
		SlabStore slabsFull;


		SmallCache() = default;
		SmallCache(size_type objSize, size_type count) : objSize(objSize), count(count)
		{
			newSlab();
		}

		bool operator<(const SmallCache& other) const noexcept
		{
			return objSize < other.objSize;
		}

		void newSlab()
		{
			slabsFree.emplace_back(objSize, count);
		}

		void freeEmpty()
		{

		}

		std::pair<SlabStore*, It> findFreeSlab() 
		{
			It slabIt;
			SlabStore* store = nullptr;
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

		template<class T>
		T* allocate()
		{
			auto [store, it] = findFreeSlab();
			auto [mem, full] = it->allocate();
			
			// Give the slab storage to the 
			// full list if it has no more room
			if (full)
			{
				store->giveNode(it, std::begin(slabsFull));
			}

			return reinterpret_cast<T*>(mem);
		}

		template<class T>
		void deallocate(T* ptr)
		{
			// Search slabs for one that holds the memory
			// ptr points to
			//
			// TODO: We should implement a coloring offset scheme
			// so that some Slabs store objects at address of address % 8 == 0
			// and others at addresses % 12 == 0 so we can search faster for the proper Slab
			auto searchSS = [&ptr](SlabStore& store) -> std::pair<SlabStore*, It>
			{
				for (auto it =  std::begin(store); 
						  it != std::end(store); ++it)
					if (it->containsMem(ptr))
						return it;
				return { &store, store.end() };
			};

			auto [store, it] = searchSS(slabsFull);
			// Need to move slab back into partials
			if (it != slabsFull.end())
			{
				slabsFull.giveNode(it, slabsPart.begin());
			}
			else
				auto [store, it] = searchSS(slabsPart);

			if (it == slabsPart.end())
				throw std::bad_alloc(); // TODO: Is this the right exception?

			it->deallocate(ptr);
			
			// Return slab to free list if it's empty
			if (it->empty())
				store->giveNode(it, std::begin(slabsFree));
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
			T* mem = nullptr;
			for (It it = std::begin(caches); it != std::end(caches); ++it)
				if (sizeof(T) < it->objSize)
				{
					mem = it->allocate<T>();
					return mem;
				}
			
			throw std::bad_alloc();
			return nullptr;
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