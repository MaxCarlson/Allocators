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
		// NOTE: as it stands, all of memstores caches need to be instantiated
		// BEFORE any memory is allocated due the way deallocation works. If any smaller 
		// caches are added after an object has been allocated that the allocated object fits
		// in it will result in undefined behavior on attempted deallocation!
		//
		// Useable for memory sizes up to... (per cache) count <= std::numeric_limits<uint16_t>::max()
		inline static SlabMemImpl::Interface memStore;

	public:

		using size_type = size_t;

		template<class T = Type>
		T* allocate()
		{
			return memStore.allocate<T>(1);
		}

		// TODO: Add an allocate by bytes?

		// Allocate space for count objects of type T
		template<class T = Type>
		T* allocate(size_t count)
		{
			return memStore.allocate<T>(count);
		}

		template<class T>
		void deallocate(T* ptr, size_t cnt = 1)
		{
			memStore.deallocate(ptr, cnt);
		}

		void addCache(size_type objSize, size_type count)
		{
			memStore.addCache(objSize, count);
		}

		template<class T = Type>
		void addCache(size_type count)
		{
			memStore.addCache(sizeof(T), count);
		}

		// Free all memory of all caches
		// If cacheSize is specified, only 
		// free the memory of that cache (if it exists)
		//
		// Note: Do NOT call this if you have objects
		// that haven't been destructed that need to be,
		// you'll have a memory leak if you do.
		void freeAll(size_type cacheSize = 0)
		{
			memStore.freeAll(cacheSize);
		}

		void freeEmpty(size_type cacheSize = 0)
		{
			memStore.freeEmpty(cacheSize);
		}

		std::vector<CacheInfo> info() const noexcept
		{
			return memStore.info();
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