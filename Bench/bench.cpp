#include "../Allocators/Slab.h"
#include "../Allocators/FreeList.h"

#include <memory>
#include "TestTypes.h"
#include "Tests.h"

// Allocators
alloc::SlabMem<int> slabM;
alloc::SlabObj<int> slabO;

DefaultAlloc defaultAl;
constexpr auto FreeListBytes = (sizeof(PartialInit) + sizeof(alloc::FreeList<PartialInit, 999999999>::OurHeader)) * (maxAllocs * 2);
alloc::FreeList<int, FreeListBytes> freeAl;

enum WrapIdx
{
	Default_Idx,
	FreeList_Idx,
	SlabMem_Idx,
	SlabObj_Idx,
	NUM_ALLOCATORS
};


// Create wrapper functions so we can call allocators
// easily with different types
template<class T, class SlabXtors>
decltype(auto) allocWrappers()
{
	// Wrapped default new/delete alloc 
	auto DefaultAl = [&]() { return defaultAl.allocate<T>(); };
	auto DefaultDe = [&](auto ptr) { defaultAl.deallocate<T>(ptr); };

	// FreeList funcs
	alloc::FreeList<T, FreeListBytes> flal;
	auto flAl	= [&]() { return flal.allocate(1); };
	auto flDe	= [&](auto ptr) { flal.deallocate(ptr); };

	// Slab mem functions
	auto sAlMem = [&]() { return slabM.allocate<T>(); };
	auto sDeMem = [&](auto ptr) { slabM.deallocate<T>(ptr); };

	// Slab obj functions 
	auto sAlObj = [&]() { return slabO.allocate<T, SlabXtors>(); };
	auto sDeObj = [&](auto ptr) { slabO.deallocate<T, SlabXtors>(ptr); };

	std::vector<std::pair<std::function<T*()>,
		std::function<void(T*)>>> allocs =
	{ 
		{DefaultAl, DefaultDe},
		{flAl, flDe},
		{sAlMem, sDeMem},
		{sAlObj, sDeObj}
	};

	return allocs;
}


template<class TestInit, class TestPtr, class Ctor>
void runTest(std::string testName, TestInit& init, TestPtr* testPtr, Ctor& ctor)
{
	std::cout << '\n' << testName << ' ' << typeid(TestInit::MyType).name() << '\n';

	std::default_random_engine re{ 1 };
	std::vector<size_t> order(maxAllocs);
	std::iota(std::begin(order), std::end(order), 0);
	std::shuffle(std::begin(order), std::end(order), re);
	init.order = std::move(order);

	
	for (int i = 0; i < WrapIdx::NUM_ALLOCATORS; ++i)
	{
		if (init.skip[i])
			continue;
		testPtr({ init, i, ctor, re });
	}
}

template<class T, class Ctor>
void callTests(TestInit<T>& init, Ctor& ctor)
{
	using FuncType = IdvTestInit<T, Ctor>;
	runTest("Basic Alloc", init, &basicAlloc<FuncType>, ctor);
	runTest("Basic Al/De", init, &basicAlDeal<FuncType>, ctor);
	runTest("Random Al/De", init, &randomAlDe<FuncType>, ctor);
	runTest("Random Access", init, &rMemAccess<FuncType>, ctor);
	runTest("Seq Access", init, &sMemAccess<FuncType>, ctor);
}

decltype(auto) defWrappers()
{
	auto al = [](auto t, auto cnt = 1) { return defaultAl.allocate<decltype(t)>(cnt); };
	auto de = [](auto ptr) { defaultAl.deallocate(ptr); };
	return std::pair(al, de);
}

decltype(auto) flWrappers()
{
	auto al = [&](auto t, auto cnt) { return freeAl.allocate<decltype(t)>(cnt); };
	auto de = [&](auto ptr) { freeAl.deallocate(ptr); };
	return std::pair(al, de);
}
decltype(auto) sMemWrappers()
{
	auto al = [&](auto t, auto cnt) { return slabM.allocate<decltype(t)>(cnt); };
	auto de = [&](auto ptr) { slabM.deallocate(ptr); };
	return std::pair(al, de);
}
template<class Xtors>
decltype(auto) sObjWrappers()
{
	// Slab obj functions 
	auto al = [&](auto t, auto cnt) { return slabO.allocate<decltype(t), Xtors>(); };
	auto de = [&](auto ptr) { slabO.deallocate<typename std::remove_pointer<decltype(ptr)>::type, Xtors>(ptr); };
	return std::pair(al, de);
}

template<class Init, class Alloc, class Ctor>
decltype(auto) benchAlT(Init& init, Alloc& al, Ctor& ctor, int count)
{
	// TODO: make vec of pair<string, double> to name tests
	std::vector<double> averages{ 1, 0.0 };

	int i;
	for (i = 0; i < count; ++i)
	{
		averages[0] += basicAlloc(init, al);

	}
	
	for (auto& s : averages)
		s /= i;

	return averages;
}

// TODO: Label scores
void printScores(std::vector<std::vector<double>>& scores)
{
	for (auto& v : scores)
	{
		for (const auto& s : v)
			std::cout << std::setw(6) << std::fixed << std::setprecision(1) << s;
		std::cout << '\n';
	}
}

template<class T, class Ctor>
void benchAllocs(Ctor& ctor, int count)
{
	std::vector<std::vector<double>> scores;

	auto[dAl, dDe] = defWrappers();
	TestT initD(T{}, ctor, dAl, dDe);
	scores.emplace_back(benchAlT(initD, defaultAl, ctor, count));

	auto[fAl, fDe] = flWrappers();
	TestT initF(T{}, ctor, fAl, fDe);
	scores.emplace_back(benchAlT(initF, freeAl, ctor, count));

	auto[memAl, memDe] = sMemWrappers();
	TestT initM(T{}, ctor, memAl, memDe);
	scores.emplace_back(benchAlT(initM, slabM, ctor, count));

	auto[objAl, objDe] = sObjWrappers<Ctor>();
	TestT initO(T{}, ctor, objAl, objDe);
	scores.emplace_back(benchAlT(initO, slabO, ctor, count));

	printScores(scores);
}

// TOP TODO: NEED to redo bench format so allocators can be rebound
// so we can test multiple type/size allocations at once
//
// TODO: NEED to clear caches after each test/type
// to free memory from Allocs that pool mem/objects
//
// TODO: Reduce test complexity, especially init objects
//
int main()
{
	// Add caches for slab allocator
	// Note: Less caches will make it faster
	for(int i = 5; i < 13; ++i)
		slabM.addCache(1 << i, cacheSz); 

	// Custom ctor for slabObj test structs
	alloc::CtorArgs piCtor(std::string("Init Test String"));
	alloc::CtorArgs ssCtor(1, 2, 3ULL, 4ULL);
	
	using piCtorT = decltype(piCtor);
	using ssCtorT = decltype(ssCtor);

	slabO.addCache<PartialInit, piCtorT>(cacheSz, piCtor);
	slabO.addCache<SimpleStruct, ssCtorT>(cacheSz, ssCtor);

	benchAllocs<SimpleStruct, ssCtorT>(ssCtor, 10);

	// Some basic test properties
	std::vector<bool> defSkips(4, false); // TODO: Not functioning correctly with new test format
	std::vector<bool> construct		= { true, true, true, false };
	std::vector<std::string> names	= { "Default", "FreeList", "SlabMem", "SlabObj" };

	// Initilize test arguments/funcs/xtors
	TestInit<PartialInit> testPi(names, construct, defSkips, allocWrappers<PartialInit, piCtorT>());
	callTests<PartialInit, piCtorT>(testPi, piCtor);

	TestInit<SimpleStruct> testSS(names, construct, defSkips, allocWrappers<SimpleStruct, ssCtorT>());
	callTests<SimpleStruct, ssCtorT>(testSS, ssCtor);


	//alloc::SlabObj<PartialInit>::freeAll();
	//alloc::SlabObj<SimpleStruct>::freeAll();
	//slabM.freeAll();

	std::cout << "\nOptimization var: " << TestV << '\n';
	return 0;
}