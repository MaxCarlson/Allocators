#include "../Allocators/Slab.h"
#include "../Allocators/Slab.cpp"
#include "../Allocators/FreeList.h"

#include <memory>
#include <chrono>
#include <iostream>
#include <random>

using Clock = std::chrono::high_resolution_clock;

constexpr auto max = 9000000;
constexpr auto count = 9400;

struct Large
{
	Large(int val)
	{
	std:fill(std::begin(ar), std::end(ar), val);
	}
	std::array<int, count> ar;
};

struct DefaultAlloc
{
	template<class T>
	T* allocateMem()
	{
		return reinterpret_cast<T*>(operator new(sizeof(T)));
	}

	template<class T>
	void deallocateMem(T* ptr)
	{
		operator delete(ptr);
	}
};

template<class T, class Alloc, class Dealloc>
void alSpeedTest(Alloc&& alloc, Dealloc&& dealloc, std::string toPrint, bool construct = true)
{

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
			new (loc) Large(1);

		dealloc(loc);
		loc = alloc();
	}

	auto end = Clock::now();

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " " << num << '\n';
}

int main()
{
	alloc::Slab<int> slab;
	DefaultAlloc defaultAl;
	alloc::FreeList<Large, (count + 8)* sizeof(Large)> flal;

	// Add caches for slab allocator
	//
	// Note: Less caches will make it faster
	for(int i = 6; i < 13; ++i)
		slab.addMemCache(1 << i, count);
	slab.addMemCache<Large>(count);

	alloc::CtorArgs lCtor(1);
	using lCtorT = decltype(lCtor);
	slab.addObjCache<Large, lCtorT>(count, lCtor);

	// Slab mem functions
	auto sAlMem = [&]()			{ return slab.allocateMem<Large>(); };
	auto sDeMem = [&](auto ptr)	{ slab.deallocateMem<Large>(ptr); };

	// Slab obj functions # NOT IMPLEMENTED YET
	auto sAlObj = [&]() { return slab.allocateObj<Large, lCtorT>(); };
	auto sDeObj = [&](auto ptr) { slab.deallocateObj<Large, lCtorT>(ptr); };

	// FreeList funcs
	auto flAl = [&]()			{ return flal.allocate(1); };
	auto flDe = [&](auto ptr)	{ flal.deallocate(ptr); };

	// Wrapped default new/delete alloc 
	auto DefaultAl = [&]()			{ return defaultAl.allocateMem<Large>(); };
	auto DefaultDe = [&](auto ptr)	{ defaultAl.deallocateMem(ptr); };


	alSpeedTest<Large>(DefaultAl, DefaultDe, "Default allocator");
	alSpeedTest<Large>(flAl, flDe, "FreeList Test");
	alSpeedTest<Large>(sAlMem, sDeMem, "Slab Test");
	alSpeedTest<Large>(sAlObj, sDeObj, "Slab Obj Test", false);


	return 0;
}