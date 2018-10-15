#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"
#include "SlabObj.h"
#include <memory>
#include <chrono>
#include <iostream>
#include <random>

using Clock = std::chrono::high_resolution_clock;

constexpr int count = 1024;

struct Large
{
	Large(int val)
	{
	std::fill(std::begin(ar), std::end(ar), val);
	}

	Large(int a, int b, int c)
	{
		std::fill(std::begin(ar), std::end(ar), a * b * c);
	}

	std::array<int, count> ar;
};


// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// General TODO's:
// Thread Safety with allocators
// Slab Allocation
// Buddy Allocation
// Mix Slab Allocation with existing allocators
// Allocator w/ thread private heaps like Intel's tbb::scalable_allocator<T>
int main()
{
	alloc::SlabMem<int> slabM;
	alloc::SlabObj<int> slabO;

	//int* iptr = alloc::allocatePage<int>(50000);
	//alloc::alignedFree(iptr);


	slabM.addCache(sizeof(char), count);
	slabM.addCache(sizeof(uint16_t), count);
	slabM.addCache(sizeof(uint32_t), count);
	slabM.addCache(sizeof(uint64_t), count);
	slabM.addCache<Large>(count);


	return 0;
}