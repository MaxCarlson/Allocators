#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Slab.h"
#include "../Allocators/Slab.cpp"
#include <random>
#include <array>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{

	TEST_CLASS(SlabTests)
	{
	public:
		
		alloc::Slab<int> slab;
		static const int count = 64;

		struct Large
		{
			Large(int val)
			{
				std:fill(std::begin(ar), std::end(ar), val);
			}
			std::array<int, count> ar;
		};

		TEST_CLASS_INITIALIZE(init)
		{
			alloc::Slab<int> slab;
			slab.addMemCache(sizeof(int), count);
			slab.addMemCache<Large>(count);
		}

		TEST_METHOD(AllocationSlabMem)
		{
			int* iptrs[count];
			Large* lptrs[count];

			std::vector<int> order(count);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(1));

			for (int i = 0; i < count; ++i)
			{
				iptrs[i]	= slab.allocateMem();
				*iptrs[i]	= i;
				lptrs[i]	= slab.allocateMem<Large>();
				*lptrs[i]	= { i };
			}

			for (auto idx : order)
			{
				Assert::IsTrue(*iptrs[idx] == idx, L"Failed to find an int");
				slab.deallocateMem(iptrs[idx]);

				for(auto i : (*lptrs[idx]).ar)
					Assert::IsTrue(i == idx, L"Failed finding a value in Large Struct");

				slab.deallocateMem(lptrs[idx]);
			}

			//auto infos = slab.memInfo();

			//for(const auto& i : infos)
			//	Assert::IsTrue(i.size == 0);
			
		}

	};
}