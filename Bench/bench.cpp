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

constexpr auto max = 400;//4000000;
constexpr auto count = 9400;



struct DefaultAlloc
{
	template<class T>
	T* allocateMem() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T>
	void deallocateMem(T* ptr) { operator delete(ptr); }
};

template<class T>
void basicAlDeTest(std::pair<std::function<T*()>, std::function<void(T*)>>& allocs, std::string toPrint, bool construct = true)
{
	auto&[alloc, dealloc] = allocs;

	T* ptrs[count];
	for (int i = 0; i < count; ++i)
		ptrs[i] = alloc();

	std::vector<size_t> order(count);
	std::shuffle(std::begin(order), std::end(order), std::default_random_engine(1));

	auto start = Clock::now();

	size_t num = 0;
	size_t idx = 0;
	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
			idx = 0;

		auto* loc = ptrs[order[idx]];

		if (construct)
			new (loc) T("Init Name");

		dealloc(loc);
		loc = alloc();
	}

	auto end = Clock::now();

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " " << num << '\n';
}

// Allocators
alloc::Slab<int> slab;
DefaultAlloc defaultAl;
constexpr auto FreeListBytes = sizeof(PartialInit) * count;

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
	auto DefaultAl = [&]() ->T* { return defaultAl.allocateMem<T>(); };
	auto DefaultDe = [&](T* ptr) { defaultAl.deallocateMem<T>(ptr); };

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

template<class AlVec, class TestPtr>
void runTest(std::string testName, std::vector<std::string> names, std::vector<bool> construct, AlVec&& alVec, TestPtr* testPtr)
{
	std::cout << '\n' << testName.c_str() << '\n';

	for (int i = 0; i < WrapIdx::NUM_ALLOCS; ++i)
		testPtr(alVec[i], names[i], construct[i]);
}

int main()
{


	// Add caches for slab allocator
	//
	// Note: Less caches will make it faster
	for(int i = 6; i < 13; ++i)
		slab.addMemCache(1 << i, count);
	slab.addMemCache<PartialInit>(count);

	alloc::CtorArgs lCtor(std::string("Init Name"));
	using lCtorT = decltype(lCtor);
	slab.addObjCache<PartialInit, lCtorT>(count, lCtor);



	std::vector<bool> construct		= { true, true, true, false };
	std::vector<std::string> names	= { "Default", "FreeList", "Slab Mem", "Slab Obj" };

	auto wrappers = allocWrappers<PartialInit, lCtorT>();
	runTest("Basic Al/De Test", names, construct, wrappers, &basicAlDeTest<PartialInit>);



	return 0;
}