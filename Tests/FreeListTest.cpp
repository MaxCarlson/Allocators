#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/FreeList.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

	TEST_CLASS(FreeListTest)
	{
	public:
		using type1 = int;
		using Allocator = alloc::FreeList<int, 512, alloc::ListPolicy>;
		Allocator allist;

		// Policy is similar to an allocator in that
		// it has all statically defined variables (so this is identical to
		// the policy inside our above allocator)
		Allocator::OurPolicy policy;

		using Header = typename Allocator::OurPolicy::Header;

		TEST_METHOD(Allocation)
		{
			// Allocate 2 bytes (+ sizeof(header))
			constexpr int count = 10;
			type1* ptrs[count];

			for (int i = 0; i < count; ++i)
			{

			}
		}

		TEST_METHOD(Deallocation)
		{
		}
	};

}