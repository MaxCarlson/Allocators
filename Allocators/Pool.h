#pragma once
#include <list>
#include "AllocHelpers.h"

namespace alloc
{
	template<size_t bytes>
	struct ListPolicy
	{

	};

	template<size_t bytes>
	struct TreePolicy
	{

	};

	template<class Type, size_t bytes = 0, template<size_t> class Policy = ListPolicy>
	class Pool
	{
		Policy<bytes> storage;
	};
}