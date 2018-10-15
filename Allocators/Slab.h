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

		// Note: When allocating memory you must remember that
		// no object of type T has been constructed here. Therfore you should
		// use placement new to create one!
		template<class T = Type>
		T* allocate()
		{
			return memStore.allocate<T>();
		}

		template<class T>
		void deallocate(T* ptr)
		{
			memStore.deallocate(ptr);
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

		void freeAll(size_type cacheSize)
		{
			memStore.freeAll(cacheSize);
		}

		void freeEmpty(size_type cacheSize)
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
		void addCache(size_type count, Xtors& xtors = defaultXtor)
		{
			SlabObjImpl::Interface::addCache<T, Xtors>(count, xtors);
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		T* allocate()
		{
			return SlabObjImpl::Interface::allocate<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		void deallocate(T* ptr)
		{
			SlabObjImpl::Interface::deallocate<T, Xtors>(ptr);
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		void freeAll()
		{
			SlabObjImpl::Interface::freeAll<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		void freeEmpty()
		{
			SlabObjImpl::Interface::freeEmpty<T, Xtors>();
		}

		template<class T = Type, class Xtors = SlabObjImpl::DefaultXtor<>>
		CacheInfo objInfo() const noexcept
		{
			return SlabObjImpl::Interface::info<T, Xtors>();
		}
	};
}