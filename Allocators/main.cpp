#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"

#include <memory>
#include <chrono>
#include <iostream>

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
	alloc::Slab<int> slal;

	slal.addMemCache(1 << 6, 64);
	slal.allocateMem();

	struct ListTest
	{
		int a;
	};

	List<ListTest> ll;
	ll.emplace_back(ListTest{ 1 });

	std::vector<int>::iterator::operator++

	return 0;
}