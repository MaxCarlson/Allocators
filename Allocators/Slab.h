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
		// SlabMem requires all caches be added before allocations begin
		// 
		// Memory is divided into Caches, which are divided
		// into Slabs. Slabs are divided into n blocks of m size.
		// Vectors are used to store the list of empty indicies on a Slab
		// Caches will add a Slab when all previous Slabs have run out of space
		//
		// Overhead per Slab: (sizeof(IndexSizeT)) * count + sizeof(size_t) * 3 + sizeof(std::vector<IndexSizeT>)
		// Note IndexSizeT is found in SlabHelpers.h and depends on MAX_SLAB_SIZE

	public:
		using STD_Compatible	= std::true_type;

		using size_type			= size_t;
		using difference_type	= std::ptrdiff_t;
		using pointer			= Type*;
		using const_pointer		= const pointer;
		using reference			= Type&;
		using const_reference	= const reference;
		using value_type		= Type;

		SlabMem() = default;

		template<class U>
		SlabMem(const SlabMem<U>& other) {} // Note: Needed for debug mode to compile with std::containers

		template<class U>
		struct rebind { using other = SlabMem<U>; };

		template<class U>
		bool operator==(const SlabMem<U>& other) const noexcept { return true; }

		template<class U>
		bool operator!=(const SlabMem<U>& other) const noexcept { return false; }

		// Allocate space for count objects of type T
		template<class T = Type>
		static T* allocate(size_type count = 1)
		{
			return SlabMemImpl::Interface::allocate<T>(count);
		}

		template<class T>
		static void deallocate(T* ptr, size_type n)
		{
			SlabMemImpl::Interface::deallocate(ptr, n);
		}

		// Add a dynamic cache that stores count 
		// number of blockSize memory chunks
		static void addCache(size_type blockSize, size_type count)
		{
			SlabMemImpl::Interface::addCache(blockSize, count);
		}

		// Add a cache of memory == (sizeof(T) * count) bytes 
		// divided into sizeof(T) byte blocks
		template<class T = Type>
		static void addCache(size_type count)
		{
			SlabMemImpl::Interface::addCache(sizeof(T), count);
		}

		// Add Caches of count elements starting at 
		// startSize that double in size until <= maxSize
		static void addCache2(size_type startSize, size_type maxSize, size_type count)
		{
			SlabMemImpl::Interface::addCache2(startSize, maxSize, count);
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

		using size_type			= size_t;
		using STD_Compatible	= std::false_type;

		template<class U>
		struct rebind { using other = SlabObj<U>; };

		// TODO: I think this is right, since SlabObj can deallocate anything allocated by SlabObj,
		// it just has to be passed the right parameters in the deallocation
		template<class U>
		bool operator==(const SlabObj<U>& other) const noexcept { return true; }
		template<class U>
		bool operator!=(const SlabObj<U>& other) const noexcept { return false; }

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