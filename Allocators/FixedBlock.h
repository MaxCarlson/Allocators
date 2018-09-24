#pragma once
#include <memory>

namespace alloc
{
	// Storage container for FixedBlock. Allows
	// FB's of different types to retain the same memory pool if they
	// have identical instatiation parameters (aside from type)
	//
	// If bytes != 0 max_size is bytes
	// TODO: Alignment specifier
	template<size_t blockSize, size_t bytes>
	struct FBStorage
	{
		static char* MyBegin;
		static size_t MySize;

		FBStorage()
		{
			if constexpr (bytes)
			{
				// TODO: Add alignement here ( new(bytes, alignment) )
				MyBegin = operator new(bytes);
				MySize	= bytes;
			}
			else
				MySize	= 0;
		}

		template<class Type>
		Type* findMemory(size_t size)
		{

		}
	};

	template<class Type, size_t blockSize, size_t bytes = 0>
	class FixedBlock 
	{
		using value_type	= Type;
		using pointer		= Type*;
		using reference		= Type&;
		using size_type		= size_t;

		FBStorage<blockSize, bytes> storage;

	public:
		FixedBlock() = default;

		pointer allocate(size_type count)
		{

		}

		template<class U>
		struct rebind { using other = FixedBlock<U, blockSize, bytes>; };
	};
}
