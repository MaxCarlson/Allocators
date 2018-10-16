#pragma once
#include <memory>
#include <stdexcept>
#include <Windows.h>

#include <cstdlib>
#include <stdlib.h>

namespace alloc
{
	using byte = unsigned char;

	// TODO: Linux #ifdef
	inline size_t pageSize()
	{
		static SYSTEM_INFO systemInfo;
		auto l = [&]() 
		{
			GetSystemInfo(&systemInfo); 
			return systemInfo.dwPageSize;
		};
		static size_t pgSz = l();
		
		return pgSz;
	}

	template<class T>
	inline T* alignedAlloc(size_t size, size_t alignment)
	{
		if (alignment < alignof(void*))
			alignment = alignof(void*);

		size_t space	= size + alignment - 1;
		void* mem		= operator new(space + sizeof(void*));
		void* aligMem	= reinterpret_cast<void*>(reinterpret_cast<byte*>(mem) + sizeof(void*));

		std::align(alignment, size, aligMem, space);

		*(reinterpret_cast<void**>(aligMem) - 1) = mem;

		return reinterpret_cast<T*>(aligMem);
	}

	template<class T>
	inline void alignedFree(T* ptr)
	{
		operator delete(*(reinterpret_cast<void**>(ptr) - 1));
	}

	inline size_t nearestPageSz(size_t bytes)
	{
		static auto pgSz = pageSize();
		auto cnt = bytes / pgSz;
		if (cnt * pgSz < bytes)
			++cnt;
		return cnt * pgSz;
	}

	template<class T>
	inline T* allocatePage(size_t bytes)
	{
		static auto pgSize = pageSize();
		auto pgs		   = nearestPageSz(bytes);

		return alignedAlloc<T>(pgs, pgSize);
	}



	struct CacheInfo
	{
		CacheInfo(size_t size, size_t capacity, size_t objectSize, size_t objPerSlab)
			: size(size), capacity(capacity), objectSize(objectSize), objPerSlab(objPerSlab) {}

		size_t size;
		size_t capacity;
		size_t objectSize;
		size_t objPerSlab;
	};

	template<class T>
	class List
	{
	public:

		struct Node
		{
			Node() {};

			template<class... Args>
			Node(Args&&... args) : data{ std::forward<Args>(args)... } {}

			T		data;
			Node*	next = nullptr;
			Node*	prev = nullptr;
		};

		struct iterator
		{
			iterator() = default;
			iterator(Node* ptr) : ptr(ptr) {}

			T& operator*()
			{
				return ptr->data;
			}

			T* operator->()
			{
				return &ptr->data;
			}

			bool operator==(const iterator& it) const noexcept
			{
				return ptr == it.ptr;
			}

			bool operator!=(const iterator& it) const noexcept
			{
				return !(*this == it);
			}

			iterator& operator++()
			{
				if (ptr->next)
					ptr = ptr->next;
				return *this;
			}

			iterator operator++(int)
			{
				iterator tmp = *this;
				++*this;
				return tmp;
			}

			Node* ptr;
		};

	private:

		void otherMove(List&& other)
		{
			MySize			= other.MySize;
			MyHead			= other.MyHead;
			MyEnd			= other.MyEnd;
			other.MyHead	= nullptr;
			other.MyEnd		= nullptr;
		}

	public:

		List()
		{
			MyHead			= new Node{};
			MyEnd			= new Node{};
			MySize			= 0;
			MyHead->prev	= MyEnd->next = nullptr;
			MyHead->next	= MyEnd;
			MyEnd->prev		= MyHead;
		}

		List(List&& other)
		{
			otherMove(std::move(other));
		}

		List& operator=(List&& other)
		{
			otherMove(std::move(other));
			return *this;
		}
		
		//TODO:  How do we square this with keeping it in a vector in SlabMem?
		~List()
		{
			delete MyHead;
			delete MyEnd;
		}

	private:

		Node*	MyHead;
		Node*	MyEnd;
		size_t	MySize;

		template<class... Args>
		Node* constructNode(Args&& ...args)
		{
			return new Node{ std::forward<Args>(args)... };
		}

		iterator insertAt(Node* n, iterator it)
		{
			Node* prev = it.ptr->prev;
			prev->next = n;
			n->next = it.ptr;
			n->prev = prev;
			it.ptr->prev = n;

			++MySize;
			return iterator{ n };
		}

	public:

		iterator begin() { return iterator{ MyHead->next }; }
		iterator end() { return iterator{ MyEnd }; }
		T& back() { return MyEnd->prev->data; }
		size_t size() const noexcept { return MySize; }
		bool empty() const noexcept { return !MySize; }

		// Give another list our node and insert it
		// before pos. Don't deallocate the memory,
		// just pass it to another list to handle
		void giveNode(iterator& ourNode, List& other, iterator pos)
		{
			--MySize;
			Node* n			= ourNode.ptr;
			n->prev->next	= n->next;
			n->next->prev	= n->prev;

			other.insertAt(ourNode.ptr, pos);
		}

		template<class... Args>
		decltype(auto) emplace_back(Args&& ...args)
		{
			Node* n = constructNode(std::forward<Args>(args)...);
			return *insertAt(n, end());
		}

		template<class... Args>
		iterator emplace(iterator it, Args&& ...args)
		{
			Node* n = constructNode(std::forward<Args>(args)...);
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

		void clear()
		{
			for (auto it = begin(); it != MyEnd;)
			{
				auto* ptr = it.ptr;
				++it;
				delete ptr;
			}

			MySize			= 0;
			MyHead->next	= MyEnd;
			MyEnd->prev		= MyHead;
		}
	};
}