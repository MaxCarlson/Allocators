#pragma once
#include <memory>
#include <stdexcept>

namespace alloc
{
	template<size_t bytes>
	struct LStorage
	{
		using byte = unsigned char;

		inline static bool init = 1;
		inline static byte* MyBegin;
		inline static size_t MyLast;

		LStorage()
		{
			if (init)
			{
				MyBegin = reinterpret_cast<byte*>(operator new (bytes));
				MyLast	= init = 0;
			}
		}

		template<class T>
		T* allocate(size_t count)
		{
			// If we have static storage size set, make sure we don't exceed it
			if (count * sizeof(T) + MyLast > bytes)
				throw std::bad_alloc();
			if (init)
				throw std::runtime_error("Allocator not instantiated!");

			byte* begin = MyBegin + MyLast;
			MyLast += count * sizeof(T);
			
			return reinterpret_cast<T*>(begin);
		}

		void reset()
		{
			delete MyBegin;
			MyBegin = nullptr;

			MyLast	= 0;
			init	= true;
		}
	};

	template<class Type, size_t bytes>
	class Linear
	{
		using value_type	= Type;
		using pointer		= Type*;
		using reference		= Type&;
		using size_type		= size_t;

		static_assert(bytes > 0, "Linear allocators memory size cannot be < 1 byte");

		size_type size = bytes;
		LStorage<bytes> storage;

		friend class Linear;

	public:

		Linear() = default;

		pointer allocate(size_type count)
		{
			return storage.allocate<Type>(count);
		}

		void deallocate(Type* t)
		{
			throw std::runtime_error("Cannot deallocate specific memory in Linear allocator, use reset to deallocate all memory "
				"(and render any instatiated Linear allocator of the same bytes size useless until constructed again)");
		}

		void reset()
		{
			storage.reset();
		}

		template<class U>
		struct rebind { using other = Linear<U, bytes>; };

		template<class Other>
		bool operator==(const Other& other)
		{
			return other.size == bytes;
		}

		template<class Other>
		bool operator!=(const Other& other)
		{
			return !(other == *this);
		}
	};
}