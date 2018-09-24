#pragma once
#include <memory>


namespace alloc
{
	template<class Type, size_t bytes = 0>
	class Linear
	{
		using value_type	= Type;
		using pointer		= Type*;
		using reference		= Type&;
		using size_type		= size_t;

		static char* MyBegin;
		
		static size_type MyLast;
		static size_type MySize;

	public:

		Linear()
		{
			if constexpr (bytes)
			{
				MySize	= bytes;
				MyBegin = operator new (bytes)
			}
			else
				MySize	= 0;
		}

		pointer allocate(size_type count)
		{
			// If we have static storage size set, make sure we don't exceed it
			if (bytes && count * sizeof(type) + MyLast > MySize)
				throw std::bad_alloc;


		}

		template<class U>
		struct rebind { using other = Linear<U, bytes>; };

	};
}