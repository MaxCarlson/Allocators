#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Slab.h"
#include "../Allocators/Slab.cpp"
#include <random>
#include <array>

// Note: All this stuff needs to be done here
// as we need access to Xtor stuff throughout the tests
static const int maxAllocs = 64;
static constexpr int LargeDefaultCtorVal = 1;

struct Large
{
	Large()					: ar(maxAllocs, LargeDefaultCtorVal) {}
	Large(int val)			: ar(maxAllocs, val) {}
	Large(int a, char b)	: ar(maxAllocs, a * b) {}

	std::vector<int> ar; // TODO: Why does this fail with a vector? Figure it out because we need vec for dtor testing
};

int DtorCount = 0;
auto dtorL = [](Large& l)			// Custom llambda dtor
{
	DtorCount += l.ar[0];
};

alloc::CtorArgs ctorA(15, 'a');		// Custome variadic argument constructor
alloc::XtorFunc dtor(dtorL);
alloc::Xtors	xtors(ctorA, dtor); // Xtors wrapper

using XtorType = decltype(xtors); 


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

	TEST_CLASS(SlabTests)
	{
	public:
		
		alloc::Slab<int> slab;

		TEST_CLASS_INITIALIZE(init)
		{
			// SlabMem Init
			alloc::Slab<int> slab;
			slab.addMemCache(sizeof(int), maxAllocs);
			slab.addMemCache<Large>(maxAllocs);

			// SlabObj Init
			slab.addObjCache<Large>(maxAllocs);						// Default Constructor/dtor
			slab.addObjCache<Large, XtorType>(maxAllocs, xtors);	// Add custom cache with custom ctors/dtors
		}

		std::pair<std::vector<int*>, std::vector<Large*>> allocMem(std::vector<int>& order, int seed)
		{
			std::vector<int*> iptrs(maxAllocs);
			std::vector<Large*> lptrs(maxAllocs);
			order.resize(maxAllocs);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(seed));

			for (int i = 0; i < maxAllocs; ++i)
			{
				iptrs[i]	= slab.allocateMem();
				*iptrs[i]	= i;
				lptrs[i]	= slab.allocateMem<Large>();
				new (lptrs[i]) Large(i);
			}
			return { iptrs, lptrs };
		}

		void deallocMem(std::vector<int*> iptrs, std::vector<Large*> lptrs, std::vector<int>& order)
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
				Assert::IsTrue(i.size == maxAllocs);

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(22));
			deallocMem(iptrs, lptrs, order);
		}

		TEST_METHOD(Dealloc_Mem)
		{
			std::vector<int> order;
			auto[iptrs, lptrs] = allocMem(order, 2);

			auto infos = slab.memInfo();

			for(const auto& i : infos)
				Assert::IsTrue(i.size == maxAllocs, L"Info incorrect!");

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(88));
			deallocMem(iptrs, lptrs, order);

			infos = slab.memInfo();

			for (const auto& i : infos)
				Assert::IsTrue(i.size == 0, L"Non-zero size after deallocation!");
		}

		//								//
		// SlabObj test functions below //
		//								//

		std::pair<std::vector<Large*>, std::vector<Large*>> allocateObjs()
		{
			std::vector<Large*> def;
			std::vector<Large*> custom;

			for (int i = 0; i < maxAllocs; ++i)
			{
				def.emplace_back(slab.allocateObj<Large>());
				custom.emplace_back(slab.allocateObj<Large, XtorType>());
			}

			return { def, custom };
		}

		void testLargeForV(Large* l, int v)
		{
			for (auto& idx : l->ar)
				Assert::IsTrue(idx == v);
		}

		void deallocObjs(std::vector<Large*> def, std::vector<Large*> cus, std::vector<int>& order)
		{
			for (auto i = 0; i < maxAllocs; ++i)
			{
				auto idx = order[i];
				testLargeForV(def[idx], LargeDefaultCtorVal);
				slab.deallocateObj<Large>(def[idx]);

				// Lambda Dtor test vals ( Only changed during Dealloc_Objs)
				auto prevCount	= DtorCount;
				auto prevNum	= cus[idx]->ar[0];
				
				testLargeForV(cus[idx], 15 * 'a');
				slab.deallocateObj<Large, XtorType>(cus[idx]);

				Assert::IsTrue(prevCount + prevNum == DtorCount); // Make sure lambda destructor is being called)
			}
		}

		TEST_METHOD(Alloc_Objs)
		{
			auto[def, custom] = allocateObjs();

			std::vector<int> order(maxAllocs);
			std::iota(std::begin(order), std::end(order), 0);

			for (int i = 0; i < maxAllocs; ++i)
			{
				testLargeForV(def[i], LargeDefaultCtorVal);
				testLargeForV(custom[i], 15 * 'a'); 
			}

			auto infoDef = slab.objInfo<Large>();
			auto infoCus = slab.objInfo<Large, XtorType>();

			Assert::IsTrue(infoDef.size == maxAllocs);
			Assert::IsTrue(infoCus.size == maxAllocs);

			deallocObjs(def, custom, order);
		}

		TEST_METHOD(Dealloc_Objs)
		{
			std::vector<int> order(maxAllocs);
			auto[def, custom] = allocateObjs();

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(111));

			auto infoDef = slab.objInfo<Large>();
			auto infoCus = slab.objInfo<Large, XtorType>();

			Assert::IsTrue(infoDef.size == maxAllocs);
			Assert::IsTrue(infoCus.size == maxAllocs);

			deallocObjs(def, custom, order);
		}
	};
}