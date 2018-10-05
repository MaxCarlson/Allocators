#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Slab.h"
#include <random>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

	TEST_CLASS(SlabTests)
	{
	public:
		
		alloc::Slab<int> slab;
		static constexpr int count = 64;

		struct Large
		{
			int a[64] = { 0 };
		};

		TEST_CLASS_INITIALIZE(init)
		{
			alloc::Slab<int> slab;
			slab.addMemCache(sizeof(int), count);
			slab.addMemCache(sizeof(Large), count);
		}



		TEST_METHOD(AllocationSlabMem)
		{
			int* iptrs[count];

			std::vector<int> order(count);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(1));


			for (int i = 0; i < count; ++i)
			{
				iptrs[i] = slab.allocateMem();
				*iptrs[i] = i;
			}

			for (auto idx : order)
			{
				Assert::IsTrue(*iptrs[idx] == idx);
				slab.deallocateMem(iptrs[idx]);
			}
		}

	};
}