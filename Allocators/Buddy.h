#pragma once
#include <memory>

namespace alloc
{
	template<class Type>
	class Buddy
	{
	public:
		using difference_type	= std::ptrdiff_t;
		using pointer			= Type * ;
		using const_pointer		= const pointer;
		using reference			= Type & ;
		using const_reference	= const reference;
		using value_type		= Type;
	};
}