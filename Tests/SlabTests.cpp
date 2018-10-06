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

		std::pair<int**, Large**> allocMem(std::vector<int>& order, int seed)
		{
			int* iptrs[count];
			Large* lptrs[count];
			order.resize(count);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(seed));

			for (int i = 0; i < count; ++i)
			{
				iptrs[i] = slab.allocateMem();
				*iptrs[i] = i;
				lptrs[i] = slab.allocateMem<Large>();
				*lptrs[i] = { i };
			}
			return { iptrs, lptrs };
		}

		void dealloc(int** iptrs, Large** lptrs, std::vector<int>& order)
		{
			for (auto idx : order)
			{
				Assert::IsTrue(*iptrs[idx] == idx, L"Failed to find an int");
				slab.deallocateMem(iptrs[idx]);

				for (auto i : (*lptrs[idx]).ar)
					Assert::IsTrue(i == idx, L"Failed finding a value in Large Struct");

				slab.deallocateMem(lptrs[idx]);
			}
		}

		TEST_METHOD(Alloc_Mem)
		{
			std::vector<int> order;
			auto [iptrs, lptrs] = allocMem(order, 1);

			auto infos = slab.memInfo();

			for (const auto& i : infos)
				Assert::IsTrue(i.size == count);

			dealloc(iptrs, lptrs, order);
		}

		TEST_METHOD(DeallocMem)
		{
			std::vector<int> order;
			auto[iptrs, lptrs] = allocMem(order, 2);

			auto infos = slab.memInfo();

			for(const auto& i : infos)
				Assert::IsTrue(i.size == count, L"Info incorrect!");

			dealloc(iptrs, lptrs, order);

			infos = slab.memInfo();

			for (const auto& i : infos)
				Assert::IsTrue(i.size == 0, L"Non-zero size after deallocation!");
		}
	};
}