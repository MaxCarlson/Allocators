#pragma once
#include <memory>
#include <stdexcept>

#define NOMINMAX = 1
#include <Windows.h>
#include <list>
#include <cstdlib>
#include <stdlib.h>


namespace alloc
{
	using byte			= unsigned char;
	using bad_dealloc	= std::bad_alloc;

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


	template<size_t bytes, size_t takenBits = 0>
	struct FindSizeT
	{
		// This finds the smallest number of bits
		// required (within the constraints of the types availible)
		// TODO: ? This can be condensed into just conditionals if we want
		enum
		{
			bits	=	bytes <= 0xff		>> takenBits ?	8  :
						bytes <= 0xffff		>> takenBits ?	16 :
						bytes <= 0xffffffff	>> takenBits ?	32 :
															64
		};
		using size_type =	std::conditional_t<bits == 8, uint8_t,
							std::conditional_t<bits == 16, uint16_t,
							std::conditional_t<bits == 32, uint32_t,
							uint64_t >>>;
	};

	inline void closestFibs(size_t num)
	{
		/*
		auto calc = []()
		{
			std::vector<size_t> nums(1, 0);
			size_t prev = 0;
			size_t i = 1;
			while (i < std::numeric_limits<size_t>::max() / 2) 
			{
				nums.emplace_back(i + prev);
				prev = i;
			}
			return nums;
		};
		static std::vector<size_t> nums = calc();
		*/
	}

	// TODO: Write tests for this list
	// TODO: std::list actually has splice,
	// think about switching?
	template<class T>
	class ListT
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

		void otherMove(ListT&& other) noexcept
		{
			MySize			= std::move(other.MySize);
			MyHead			= std::move(other.MyHead);
			MyEnd			= std::move(other.MyEnd);
			other.MyHead	= nullptr;
			other.MyEnd		= nullptr;
		}

	public:

		ListT()
		{
			MyHead			= new Node{};
			MyEnd			= new Node{};
			MySize			= 0;
			MyHead->prev	= MyEnd->next = nullptr;
			MyHead->next	= MyEnd;
			MyEnd->prev		= MyHead;
		}

		ListT(ListT&& other) noexcept
		{
			otherMove(std::move(other));
		}

		ListT& operator=(ListT&& other) noexcept
		{
			otherMove(std::move(other));
			return *this;
		}
		
		~ListT()
		{
			if (MyHead && MySize)
			{
				for (iterator it = begin(); it != end();)
				{
					Node* p = it.ptr;
					++it;
					delete p;
				}
			}
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

		iterator insertAt(Node* n, iterator it) noexcept
		{
			Node* prev		= it.ptr->prev;
			prev->next		= n;
			n->next			= it.ptr;
			n->prev			= prev;
			it.ptr->prev	= n;

			++MySize;
			return iterator{ n };
		}

	public:

		iterator begin() { return iterator{ MyHead->next }; }
		iterator end() { return iterator{ MyEnd }; }
		T& back() { return MyEnd->prev->data; }
		size_t size() const noexcept { return MySize; }
		bool empty() const noexcept { return !MySize; }

		
		void splice(iterator pos, ListT& other, iterator it) noexcept
		{
			Node* n = it.ptr;
			n->prev->next = n->next;
			n->next->prev = n->prev;
			--other.MySize;

			insertAt(it.ptr, pos);
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

	// TODO: Test speed diff
	//template<class T>
	//using List = std::list<T, std::allocator<T>>; 

	template<class T>
	using List = ListT<T>;
}