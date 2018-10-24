#pragma once
#include "AllocHelpers.h"
#include "SlabMem.h"
#include "SlabObj.h"
#include <vector>
#include <array>
#include <numeric>

namespace alloc
{
	template<class Type>
	class SlabMem
	{
	public:

		using size_type = size_t;

		template<class T = Type>
		static T* allocate()
		{
			return SlabMemImpl::Interface::allocate<T>(1);
		}

		// Allocate space for count objects of type T
		template<class T = Type>
		static T* allocate(size_t count)
		{
			return SlabMemImpl::Interface::allocate<T>(count);
		}

		template<class T>
		static void deallocate(T* ptr)
		{
			SlabMemImpl::Interface::deallocate(ptr);
		}

		static void addCache(size_type blockSize, size_type count)
		{
			SlabMemImpl::Interface::addCache(blockSize, count);
		}

		template<class T = Type>
		static void addCache(size_type count)
		{
			SlabMemImpl::Interface::addCache(sizeof(T), count);
		}

		// Free all memory of all caches
		// If cacheSize is specified, only 
		// free the memory of that cache (if it exists)
		//
		// Note: Do NOT call this if you have objects
		// that haven't been destructed that need to be,
		// you'll have a memory leak if you do.
		static void freeAll(size_type cacheSize = 0)
		{
			SlabMemImpl::Interface::freeAll(cacheSize);
		}

		static void freeEmpty(size_type cacheSize = 0)
		{
			SlabMemImpl::Interface::freeEmpty(cacheSize);
		}

		static std::vector<CacheInfo> info() noexcept
		{
			return SlabMemImpl::Interface::info();
		}
	};

	template<class Type>
	class SlabObj
	{
	public:

		using size_type = size_t;

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static void addCache(size_type count, Xtors& xtors = defaultXtor)
		{
			SlabObjImpl::Interface::addCache<T, Xtors>(count, xtors);
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static T* allocate()
		{
			return SlabObjImpl::Interface::allocate<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static void deallocate(T* ptr)
		{
			SlabObjImpl::Interface::deallocate<T, Xtors>(ptr);
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static void freeAll()
		{
			SlabObjImpl::Interface::freeAll<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static void freeEmpty()
		{
			SlabObjImpl::Interface::freeEmpty<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		static CacheInfo objInfo() 
		{
			return SlabObjImpl::Interface::info<T, Xtors>();
		}
	};
}