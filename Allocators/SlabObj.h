#pragma once
#include "AllocHelpers.h"

namespace SlabObj
{
	using byte = alloc::byte;

	template<class T>
	struct Slab
	{
		byte* mem;
		std::vector<uint16_t> availible;
		
		Slab() = default;
		Slab(size_t count) : availible(count)
		{
			mem = new T[count];
			std::iota(std::rbegin(availible), std::rend(availible), 0);
		}
	};

	// TODO: Use this a stateless (except
	// static vars) cache of objects so we can have
	// caches deduced by type
	template<class T>
	struct Cache
	{
		using size_type = size_t;
		using Storage	= alloc::List<Slab<T>>;

		inline static Storage full;
		inline static Storage part;
		inline static Storage free;

		ObjCache() = default;

		static void addCache(size_type count)
		{
			// TODO: Do we want to allow different size caches in here?
			//if (size != count && init)
			//	return;
			
			init = true;
			free.emplace_back(count);
		}
	};


	struct Interface
	{
		using size_type = size_t;

		template<class T>
		void addCache(size_type count)
		{
			using Cache = Cache<T>;
			
			Cache::addCache(count);
		}

		template<class T>
		void allocate()
		{

		}

		template<class T>
		void deallocate(T* ptr)
		{

		}
	};
}