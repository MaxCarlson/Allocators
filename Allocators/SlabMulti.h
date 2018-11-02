#pragma once

namespace SlabMultiImpl
{






template<class Type>
struct Interface
{
};



}// End SlabMultiImpl::

#include <numeric>

namespace alloc // Move this to Slab.h eventually
{

template<class Type>
class SlabMulti
{
public:
	using STD_Compatible	= std::true_type;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

	int threads;
	SlabMultiImpl::Interface<Type> interface;

	SlabMulti(int threads) : 
		threads{ threads }
	{

	}
};

}