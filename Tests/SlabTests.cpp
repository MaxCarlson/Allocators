#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Slab.h"
#include <random>
#include <array>

// Note: All this stuff needs to be done here
// as we need access to Xtor stuff throughout the tests
static const int maxAllocs = 64;
static constexpr int LargeDefaultCtorVal = 1;

static int LargeDtorCounter = 0;

struct Large
{
	Large()										: ar(maxAllocs, LargeDefaultCtorVal) {}
	Large(int val)								: ar(maxAllocs, val) {}
	Large(int a, char b, size_t n = maxAllocs)	: ar(n, a * b) {}
	~Large()
	{
		++LargeDtorCounter;
	}

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
		
		alloc::SlabMem<int> slabM;
		alloc::SlabObj<Large> slabO;

		static const int MEM_CACHES = 3;

		TEST_CLASS_INITIALIZE(init)
		{
			// SlabMem Init
			alloc::SlabMem<int> slabM;
			slabM.addCache(sizeof(int), maxAllocs);
			slabM.addCache<Large>(maxAllocs);
			slabM.addCache(sizeof(Large) * 100, 100); 

			// SlabObj Init
			alloc::SlabObj<Large> slabO;
			slabO.addCache<Large>(maxAllocs);						// Default Constructor/dtor
			slabO.addCache<Large, XtorType>(maxAllocs, xtors);	// Add custom cache with custom ctors/dtors
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
				iptrs[i]	= slabM.allocate();
				*iptrs[i]	= i;
				lptrs[i]	= slabM.allocate<Large>();
				new (lptrs[i]) Large(i);
			}
			return { iptrs, lptrs };
		}

		void deallocMem(std::vector<int*> iptrs, std::vector<Large*> lptrs, std::vector<int>& order)
		{
			for (auto idx : order)
			{
				Assert::IsTrue(*iptrs[idx] == idx, L"Failed to find an int");
				slabM.deallocate(iptrs[idx], 1);

				for (auto i : (*lptrs[idx]).ar)
					Assert::IsTrue(i == idx, L"Failed finding a value in Large Struct");

				slabM.deallocate(lptrs[idx], 1);
			}
		}

		template<class Al>
		void checkMemStats(Al& al, int cmp, const wchar_t * str = nullptr)
		{
			const auto info = al.info();
			for (int i = 0; i < MEM_CACHES - 1; ++i)
				Assert::IsTrue(info[i].size == cmp, str);
		}

		TEST_METHOD(Alloc_Mem)
		{
			std::vector<int> order;
			auto [iptrs, lptrs] = allocMem(order, 1);

			checkMemStats(slabM, maxAllocs);

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(22));
			deallocMem(iptrs, lptrs, order);
		}

		TEST_METHOD(Alloc_MultiMem) // TODO: Test for multiple count of object allocations
		{
			Large* ptrs[101];
			std::vector<int> order(101, 0);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(22));

			for (int i = 0; i < 101; ++i)
			{
				ptrs[i] = slabM.allocate<Large>(10);
				for (int j = 0; j < 10; ++j)
					new (&ptrs[i][j]) Large{ 2, 6, 3 }; // Place Large's
			}
			
			Assert::IsTrue(slabM.info().at(3).size == 101);

			for (int i = 0; i < 101; ++i)
				for (int j = 0; j < 10; ++j)
				{
					for (int h = 0; h < 3; ++h)
					{
						Assert::IsTrue(ptrs[order[i]][j].ar[h] == 12);
					}
					slabM.deallocate(&ptrs[order[i]][j], 10);
				}
		}

		TEST_METHOD(Mem_STD_Allocator)
		{
			static constexpr int count = 50;
			std::vector<int> order(101, 0);
			std::iota(std::begin(order), std::end(order), 0);
			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(22));

			std::vector<Large, alloc::SlabMem<Large>> vec;
			vec.reserve(count+1);

			for (int i = 0; i < count; ++i)
				vec.emplace_back(2, 6, 3);

			for (int i = 0; i < count; ++i)
				for (int j = 0; j < 3; ++j)
					Assert::IsTrue(vec[i].ar[j] == 12);

			auto preClear = LargeDtorCounter;
			vec.clear();

			Assert::IsTrue(LargeDtorCounter == preClear + count);
		}

		TEST_METHOD(Dealloc_Mem)
		{
			std::vector<int> order;
			auto[iptrs, lptrs] = allocMem(order, 2);

			checkMemStats(slabM, maxAllocs, L"Info incorrect!");

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(88));
			deallocMem(iptrs, lptrs, order);

			checkMemStats(slabM, 0, L"Info incorrect!");
		}

		TEST_METHOD(FreeAll_Mem)
		{
			std::vector<int> order;
			auto[iptrs, lptrs] = allocMem(order, 2);

			checkMemStats(slabM, maxAllocs, L"Info incorrect!");

			auto preDeallocCnt = LargeDtorCounter;
			slabM.freeAll(sizeof(int));
			slabM.freeAll(sizeof(Large));

			// SlabMem.freeAll does NOT call dtors (how could it know what fills what?)
			Assert::IsTrue(LargeDtorCounter == preDeallocCnt);

			auto infos = slabM.info();
			Assert::IsTrue(infos[0].size == 0);
			Assert::IsTrue(infos[1].size == 0);
			Assert::IsTrue(infos[0].capacity == 0);
			Assert::IsTrue(infos[1].capacity == 0);
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
				def.emplace_back(slabO.allocate<Large>());
				custom.emplace_back(slabO.allocate<Large, XtorType>());
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
				slabO.deallocate<Large>(def[idx]);

				// Lambda Dtor test vals ( Only changed during Dealloc_Objs)
				auto prevCount	= DtorCount;
				auto prevNum	= cus[idx]->ar[0];
				
				testLargeForV(cus[idx], 15 * 'a');
				slabO.deallocate<Large, XtorType>(cus[idx]);

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

			auto infoDef = slabO.objInfo<Large>();
			auto infoCus = slabO.objInfo<Large, XtorType>();

			Assert::IsTrue(infoDef.size == maxAllocs);
			Assert::IsTrue(infoCus.size == maxAllocs);

			deallocObjs(def, custom, order);
		}

		TEST_METHOD(Dealloc_Objs)
		{
			std::vector<int> order(maxAllocs);
			auto[def, custom] = allocateObjs();

			std::shuffle(std::begin(order), std::end(order), std::default_random_engine(111));

			auto infoDef = slabO.objInfo<Large>();
			auto infoCus = slabO.objInfo<Large, XtorType>();

			Assert::IsTrue(infoDef.size == maxAllocs);
			Assert::IsTrue(infoCus.size == maxAllocs);

			deallocObjs(def, custom, order);
		}

		TEST_METHOD(FreeAll_Objs)
		{
			auto[def, custom]	= allocateObjs();
			auto infoDef		= slabO.objInfo<Large>();
			auto infoCus		= slabO.objInfo<Large, XtorType>();
			Assert::IsTrue(infoDef.size == maxAllocs);
			Assert::IsTrue(infoCus.size == maxAllocs);

			// Measure the number of Large destructors called
			// to make sure the objects are being destroyed when freeing
			auto preDtorCount = LargeDtorCounter;

			slabO.freeAll<Large>();
			Assert::IsTrue(LargeDtorCounter == preDtorCount + infoDef.capacity);

			preDtorCount = LargeDtorCounter;
			slabO.freeAll<Large, XtorType>();
			Assert::IsTrue(LargeDtorCounter == preDtorCount + infoCus.capacity);


			infoDef = slabO.objInfo<Large>();
			infoCus = slabO.objInfo<Large, XtorType>();
			Assert::IsTrue(infoDef.size == 0);
			Assert::IsTrue(infoCus.size == 0);
			Assert::IsTrue(infoDef.capacity == 0);
			Assert::IsTrue(infoCus.capacity == 0);
		}
	};
}