#include "../Allocators/Slab.h"
#include "../Allocators/Slab.cpp"
#include "../Allocators/FreeList.h"

#include <memory>
#include <functional>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include "TestTypes.h"

using Clock = std::chrono::high_resolution_clock;

constexpr auto max = 100000;
constexpr auto count = 9400;

auto RandomEngine = std::default_random_engine(1);

struct DefaultAlloc
{
	template<class T>
	T* allocateMem() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T>
	void deallocateMem(T* ptr) { operator delete(ptr); }
};

template<class T>
struct TestInit
{
	TestInit(std::vector<std::string> names,
		std::vector<bool> construct,
		std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs)
		: names(names), construct(construct), allocs(allocs) {}

	using MyType = T;

	std::vector<size_t> order;
	std::vector<std::string> names;
	std::vector<bool> construct;
	std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs;
};

template<class T, class Ctor>
void basicAlloc(std::pair<std::function<T*()>, std::function<void(T*)>>& allocs, std::string toPrint, bool construct, Ctor& ctor)
{
	using TimeType = std::chrono::milliseconds;

	auto&[alloc, dealloc] = allocs;

	auto start = Clock::now();

	size_t num = 0;
	size_t idx = 0;
	size_t deallocTime = 0;
	std::vector<T*> ptrs;

	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
		{
			idx = 0;
			auto start = Clock::now();

			for (auto& ptr : ptrs)
				dealloc(ptr);
			ptrs.clear();

			auto end = Clock::now();
			deallocTime += std::chrono::duration_cast<TimeType>(end - start).count();
		}

		ptrs.emplace_back(alloc());

		if (construct)
			ctor.construct(ptrs[idx]);
	}

	auto end = Clock::now();

	for (auto ptr : ptrs)
		dealloc(ptr);

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime << " " << num << '\n';
}

template<class T, class Ctor>
void basicAlDeal(std::pair<std::function<T*()>, std::function<void(T*)>>& allocs, std::string toPrint, bool construct, Ctor& ctor)
{
	auto&[alloc, dealloc] = allocs;

	auto start = Clock::now();

	size_t num = 0;
	size_t idx = 0;
	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
			idx = 0;

		auto* loc = alloc();

		if (construct)
			ctor.construct(loc);

		dealloc(loc);
	}

	auto end = Clock::now();

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " " << num << '\n';
}

// Allocators
alloc::Slab<int> slab;
DefaultAlloc defaultAl;

constexpr auto FreeListBytes = (sizeof(PartialInit) + sizeof(alloc::FreeList<PartialInit, 999>::OurHeader)) * (count + 5550);

enum WrapIdx
{
	Default_Idx,
	FreeList_Idx,
	SlabMem_Idx,
	SlabObj_Idx,
	NUM_ALLOCS
};


// Create wrapper functions so we can call allocators
// easily with different types
template<class T, class SlabXtors>
decltype(auto) allocWrappers()
{
	// Wrapped default new/delete alloc 
	auto DefaultAl = [&]() { return defaultAl.allocateMem<T>(); };
	auto DefaultDe = [&](auto ptr) { defaultAl.deallocateMem<T>(ptr); };

	alloc::FreeList<T, FreeListBytes> flal;

	// FreeList funcs
	auto flAl = [&]() { return flal.allocate(1); };
	auto flDe = [&](auto ptr) { flal.deallocate(ptr); };

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

	std::vector<size_t> order(count);
	std::iota(std::begin(order), std::end(order), 0);
	std::shuffle(std::begin(order), std::end(order), RandomEngine);
	init.order = std::move(order);

	for (int i = 0; i < WrapIdx::NUM_ALLOCS; ++i)
		testPtr(init.allocs[i], init.names[i], init.construct[i], ctor);
}

int main()
{
	// Add caches for slab allocator
	// Note: Less caches will make it faster
	for(int i = 6; i < 13; ++i)
		slab.addMemCache(1 << i, count);
	slab.addMemCache<PartialInit>(count);

	alloc::CtorArgs lCtor(std::string("Init Name"));
	using lCtorT = decltype(lCtor);
	slab.addObjCache<PartialInit, lCtorT>(count, lCtor);

	// Some basic test properties
	std::vector<bool> construct		= { true, true, true, false };
	std::vector<std::string> names	= { "Default", "FreeList", "Slab Mem", "Slab Obj" };

	// Init ctor for PartialInit class testing
	alloc::CtorArgs testPiArg(std::string("Init Test String"));
	using PiCtorT = decltype(testPiArg);

	// Initilize test arguments/funcs/xtors
	TestInit<PartialInit> testPi(names, construct, allocWrappers<PartialInit, lCtorT>());

	runTest("Basic Alloc", testPi, &basicAlloc<PartialInit, PiCtorT>, testPiArg);
	runTest("Basic Alloc/Dealloc", testPi, &basicAlDeal<PartialInit, PiCtorT>, testPiArg);


	return 0;
}