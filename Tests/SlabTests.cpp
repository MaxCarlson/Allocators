#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Slab.h"
#include <random>
#include <array>

// Note: All this stuff needs to be done here
// as we need access to Xtor stuff throughout the tests
static const int MAX_CACHE_SZ	= 256;
static const int maxAllocs		= 2048;
static constexpr int LargeDefaultCtorVal = 1;

static size_t LargeDtorCounter = 0;

struct Large
{
	Large()										: ar(maxAllocs, LargeDefaultCtorVal) {}
	Large(int val)								: ar(maxAllocs, val) {}
	Large(int a, char b, size_t n = maxAllocs)	: ar(n, a * b) {}
	~Large()
	{
		++LargeDtorCounter;
	}

	std::vector<int> ar; 
};

static size_t DtorCount = 0;
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
		slabM.addCache(sizeof(int), MAX_CACHE_SZ);
		slabM.addCache<Large>(MAX_CACHE_SZ);
		slabM.addCache(sizeof(Large) * 100, 100); 

		// SlabObj Init
		alloc::SlabObj<Large> slabO;
		slabO.addCache<Large>(MAX_CACHE_SZ);						// Default Constructor/dtor
		slabO.addCache<Large, XtorType>(MAX_CACHE_SZ, xtors);	// Add custom cache with custom ctors/dtors
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

	std::vector<int> orderShuffle(size_t size, size_t seed)
	{
		std::vector<int> order(size);
		std::iota(std::begin(order), std::end(order), 0);
		std::shuffle(std::begin(order), std::end(order), std::default_random_engine(seed));
		return order;
	}

	TEST_METHOD(Alloc_Mem)
	{
		std::vector<int> order;
		auto [iptrs, lptrs] = allocMem(order, 1);

		checkMemStats(slabM, maxAllocs);

		std::shuffle(std::begin(order), std::end(order), std::default_random_engine(22));
		deallocMem(iptrs, lptrs, order);
	}

	//
	// FAILING
	//
	// Test for multiple count of object allocations
	TEST_METHOD(Alloc_MultiMem) 
	{
		Large* ptrs[101];
		auto order = orderShuffle(101, 72);

		for (int i = 0; i < 101; ++i)
		{
			ptrs[i] = slabM.allocate<Large>(10);
			for (int j = 0; j < 10; ++j)
				new (&ptrs[i][j]) Large{ 2, 6, 3 }; // Place Large's
		}
			
		Assert::IsTrue(slabM.info()[2].size == 101);

		for (int i = 0; i < 101; ++i)
		{
			for(int j = 0; j < 10; ++j) // Make sure each and every Slab allocated is correct
				for (int h = 0; h < 3; ++h)
					Assert::IsTrue(ptrs[order[i]][j].ar[h] == 12); 
			slabM.deallocate(ptrs[order[i]], 10);
		}
	}

	// Make sure SlabMem works with std::containers 
	// TODO: Should be a complex container for allocators like std::map
	TEST_METHOD(Mem_STD_Allocator)
	{
		static constexpr int count = 50;
		auto order = orderShuffle(101, 83);

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

	void deallocObjs(std::vector<Large*>& def, std::vector<Large*>& cus, std::vector<int>& order)
	{
		for (auto i = 0; i < maxAllocs; ++i)
		{
			const auto idx = order[i];
			testLargeForV(def[idx], LargeDefaultCtorVal);
			slabO.deallocate<Large>(def[idx]);

			// Lambda Dtor test vals ( Only changed during Dealloc_Objs)
			auto prevCount	= DtorCount;
			auto prevNum	= cus[idx]->ar[0];
				
			testLargeForV(cus[idx], 15 * 'a');
			slabO.deallocate<Large, XtorType>(cus[idx]);

			Assert::IsTrue(prevCount + prevNum == DtorCount, L"Dtor count fail"); // Make sure lambda destructor is being called)
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
		auto order = orderShuffle(maxAllocs, 111);
		auto[def, custom] = allocateObjs();

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