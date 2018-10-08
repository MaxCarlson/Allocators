#pragma once
#include "AllocHelpers.h"
#include <vector>
#include <functional>

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
			mem = reinterpret_cast<byte*>(operator new(sizeof(T) * count));
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

		inline static std::function<void(T&)> ctor;
		inline static std::function<void(T&)> dtor;

		template<class Ctor, class Dtor>
		static void addCache(size_type count, 
			Ctor&& nctor, Dtor&& ndtor)
		{
			// TODO: Do we want to allow different size caches in here?
			//if (size != count && init)
			//	return;
			//init = true;

			ctor = nctor;
			dtor = ndtor

			free.emplace_back(count);
		}

		static T* allocate()
		{

		}

		static void deallocate(T* ptr)
		{

		}
	};


	struct Interface
	{
		using size_type = size_t;

		template<class T, class Ctor>
		void addCache(size_type count, Ctor&& ctor)
		{
			Cache<T>::addCache(count, ctor, [](T& t) {});
		}

		template<class T>
		T* allocate()
		{
			return Cache<T>::allocate();
		}

		template<class T>
		void deallocate(T* ptr)
		{
			Cache<T>::deallocate(ptr);
		}
	};
}