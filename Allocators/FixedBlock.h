#pragma once
#include <memory>

namespace alloc
{
	// Storage container for FixedBlock. Allows
	// FB's of different types to retain the same memory pool if they
	// have identical instatiation parameters
	//
	// If bytes != 0 max_size is bytes
	template<size_t blockSize, size_t bytes>
	struct FBStorage
	{

	};

	template<class Type, size_t blockSize, size_t bytes = 0>
	class FixedBlock 
	{
		using value_type = Type;

		FBStorage<bytes, blockSize> storage;

	public:
		FixedBlock()
		{

		}

		template<class U>
		struct rebind { using other = FixedBlock<U, blockSize, bytes>; };
	};
}
