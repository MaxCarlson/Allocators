#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"

#include <memory>
#include <chrono>
#include <iostream>
#include <random>

using Clock = std::chrono::high_resolution_clock;

constexpr auto max = 100000;
constexpr auto count = 6400;

struct Large
{
	Large(int val)
	{
	std:fill(std::begin(ar), std::end(ar), val);
	}
	std::array<int, count> ar;
};

struct New
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

template<class Alloc>
void allocationSpeed(Alloc& slab, std::string toPrint)
{
	auto start = Clock::now();

	Large* lptrs[count];

	size_t num = 0;
	size_t idx = 0;
	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
			idx = 0;

		lptrs[idx] = slab.allocateMem<Large>();
		lptrs[idx]->ar[0] = i;
		num += lptrs[idx]->ar[0];
		slab.deallocateMem(lptrs[idx]);
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
	New allocNew;

	slab.addMemCache(sizeof(char), count);
	slab.addMemCache(sizeof(uint16_t), count);
	slab.addMemCache(sizeof(uint32_t), count);
	slab.addMemCache(sizeof(uint64_t), count);
	slab.addMemCache<Large>(count);

	allocationSpeed(slab, "Slab Allocator");
	allocationSpeed(allocNew, "New Allocator");

	return 0;
}