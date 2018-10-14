#include "../Allocators/Slab.h"
#include "../Allocators/Slab.cpp"
#include "../Allocators/FreeList.h"

#include <memory>
#include "TestTypes.h"
#include "Tests.h"

// Allocators
alloc::Slab<int> slab;
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
	auto DefaultAl = [&]() { return defaultAl.allocateMem<T>(); };
	auto DefaultDe = [&](auto ptr) { defaultAl.deallocateMem<T>(ptr); };

	// FreeList funcs
	alloc::FreeList<T, FreeListBytes> flal;
	auto flAl	= [&]() { return flal.allocate(1); };
	auto flDe	= [&](auto ptr) { flal.deallocate(ptr); };

	// Slab mem functions
	auto sAlMem = [&]() { return slab.allocateMem<T>(); };
	auto sDeMem = [&](auto ptr) { slab.deallocateMem<T>(ptr); };

	// Slab obj functions 
	auto sAlObj = [&]() { return slab.allocateObj<T, SlabXtors>(); };
	auto sDeObj = [&](auto ptr) { slab.deallocateObj<T, SlabXtors>(ptr); };

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

	std::vector<size_t> order(maxAllocs);
	std::iota(std::begin(order), std::end(order), 0);
	std::shuffle(std::begin(order), std::end(order), RandomEngine);
	init.order = std::move(order);

	for (int i = 0; i < WrapIdx::NUM_ALLOCATORS; ++i)
	{
		if (init.skip[i])
			continue;
		testPtr({ init, i, ctor });
	}
}

template<class T, class Ctor>
void callTests(TestInit<T>& init, Ctor& ctor)
{
	using FuncType = IdvTestInit<T, Ctor>;
	runTest("Basic Alloc", init,		 &basicAlloc<FuncType>,  ctor);
	runTest("Basic Alloc/Dealloc", init, &basicAlDeal<FuncType>, ctor);
	runTest("Random Al/De", init,		 &randomAlDe<FuncType>,  ctor);
}

int main()
{
	// Add caches for slab allocator
	// Note: Less caches will make it faster
	for(int i = 6; i < 13; ++i)
		slab.addMemCache(1 << i, cacheSz);
	slab.addMemCache<PartialInit>(cacheSz);

	// Custom ctor for slabObj PartilInit
	alloc::CtorArgs lCtor(std::string("Init Test String"));
	using lCtorT = decltype(lCtor);
	slab.addObjCache<PartialInit, lCtorT>(maxAllocs, lCtor);

	// Some basic test properties
	std::vector<bool> defSkips(4, false); // TODO: Not functioning correctly with new test format
	std::vector<bool> construct		= { true, true, true, false };
	std::vector<std::string> names	= { "Default", "FreeList", "SlabMem", "SlabObj" };

	// Initilize test arguments/funcs/xtors
	TestInit<PartialInit> testPi(names, construct, defSkips, allocWrappers<PartialInit, lCtorT>());

	// Init ctor for PartialInit class testing
	alloc::CtorArgs piArgs(std::string("Init Test String"));

	callTests<PartialInit, decltype(piArgs)>(testPi, piArgs);



	return 0;
}