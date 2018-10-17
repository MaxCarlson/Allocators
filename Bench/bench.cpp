#include "../Allocators/Slab.h"
#include "../Allocators/FreeList.h"

#include <memory>
#include "TestTypes.h"
#include "Tests.h"

// Allocators
alloc::SlabMem<int> slabM;
alloc::SlabObj<int> slabO;

DefaultAlloc defaultAl;
constexpr auto FreeListBytes = (sizeof(PartialInit) + sizeof(alloc::FreeList<PartialInit, 999999999>::OurHeader)) * (maxAllocs + 5550);

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
	runTest("Basic Alloc", init,	&basicAlloc<FuncType>,  ctor);
	runTest("Basic Al/De", init,	&basicAlDeal<FuncType>, ctor);
	runTest("Random Al/De", init,	&randomAlDe<FuncType>,  ctor);
}

//
// TODO: NEED to clear caches after each test/type
// to free memory from Allocs that pool mem/objects
//
int main()
{
	// Add caches for slab allocator
	// Note: Less caches will make it faster
	for(int i = 5; i < 13; ++i)
		slabM.addCache(1 << i, cacheSz); 

	// Custom ctor for slabObj PartilInit
	alloc::CtorArgs piCtor(std::string("Init Test String"));
	using piCtorT = decltype(piCtor);
	alloc::CtorArgs ssCtor(1, 2, 3ULL, 4ULL);
	using ssCtorT = decltype(ssCtor);

	slabO.addCache<PartialInit, piCtorT>(cacheSz, piCtor);
	slabO.addCache<SimpleStruct, ssCtorT>(cacheSz, ssCtor);

	// Some basic test properties
	std::vector<bool> defSkips(4, false); // TODO: Not functioning correctly with new test format
	std::vector<bool> construct		= { true, true, true, false };
	std::vector<std::string> names	= { "Default", "FreeList", "SlabMem", "SlabObj" };

	// Initilize test arguments/funcs/xtors
	TestInit<PartialInit> testPi(names, construct, defSkips, allocWrappers<PartialInit, piCtorT>());
	callTests<PartialInit, piCtorT>(testPi, piCtor);

	TestInit<SimpleStruct> testSS(names, construct, defSkips, allocWrappers<SimpleStruct, ssCtorT>());
	callTests<SimpleStruct, ssCtorT>(testSS, ssCtor);


	alloc::SlabObj<PartialInit>::freeAll();
	alloc::SlabObj<SimpleStruct>::freeAll();
	slabM.freeAll();

	std::cout << "\nOptimization var: " << TestV << '\n';
	return 0;
}