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
void alSpeedTest(Alloc&& alloc, Dealloc&& dealloc, std::string toPrint)
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
		dealloc(loc);
		loc = alloc();
	}

	auto end = Clock::now();

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " " << num << '\n';
}

// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// General TODO's:
// Thread Safety with allocators
// Slab Allocation
// Buddy Allocation
// Mix Slab Allocation with existing allocators
int main()
{
	alloc::Slab<int> slab;
	DefaultAlloc allocNew;
	alloc::FreeList<Large, (count + 8)* sizeof(Large)> flal;

	for(int i = 6; i < 13; ++i)
		slab.addMemCache(1 << i, count);
	slab.addMemCache<Large>(count);

	// Slab functions
	auto sAl = [&]()			{ return slab.allocateMem<Large>(); };
	auto sDe = [&](auto ptr)	{ slab.deallocateMem<Large>(ptr); };

	// FreeList funcs
	auto flAl = [&]()			{ return flal.allocate(1); };
	auto flDe = [&](auto ptr)	{ flal.deallocate(ptr); };

	// Wrapped default new/delete alloc 
	auto DefaultAl = [&]()			{ return allocNew.allocateMem<Large>(); };
	auto DefaultDe = [&](auto ptr)	{ allocNew.deallocateMem(ptr); };


	alSpeedTest<Large>(sAl, sDe, "Slab Test");
	alSpeedTest<Large>(flAl, flDe, "FreeList Test");
	alSpeedTest<Large>(DefaultAl, DefaultDe, "Default allocator");

	return 0;
}