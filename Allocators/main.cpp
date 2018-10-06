#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"

#include <memory>
#include <chrono>
#include <iostream>
#include <random>

using Clock = std::chrono::high_resolution_clock;

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
	static const int count = 64;

	struct Large
	{
		Large(int val)
		{
		std:fill(std::begin(ar), std::end(ar), val);
		}
		std::array<int, count> ar;
	};


	alloc::Slab<int> slab;
	slab.addMemCache(sizeof(int), count);
	slab.addMemCache<Large>(count);
	

	int* iptrs[count];
	Large* lptrs[count];

	std::vector<int> order(count);
	std::iota(std::begin(order), std::end(order), 0);
	std::shuffle(std::begin(order), std::end(order), std::default_random_engine(1));

	for (int i = 0; i < count; ++i)
	{
		iptrs[i] = slab.allocateMem();
		*iptrs[i] = i;
		lptrs[i] = slab.allocateMem<Large>();
		*lptrs[i] = { i };
	}

	for (auto idx : order)
	{
		slab.deallocateMem(iptrs[idx]);
		slab.deallocateMem(lptrs[idx]);
		}
	//std::vector<int>::iterator::operator++

	return 0;
}