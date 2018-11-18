#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"
#include "SlabObj.h"
#include "SlabMulti.h"
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
// Allocator w/ thread private heaps like Intel's tbb::scalable_allocator<T>
int main()
{
	//alloc::SlabMem<size_t>::addCache2(sizeof(size_t), 1 << 10, 512);
	//alloc::FreeList<int, 50000, alloc::TreePolicy> al;
	
	constexpr int count = 1000;

	alloc::SlabMulti<size_t>						multi;
	std::vector<size_t, alloc::SlabMulti<size_t>>	vec(multi);

	SharedMutex tex;

	tex.lockShared();

	tex.unlockShared();

	tex.lock();
	tex.unlock();


	//auto start = Clock::now();


	return 0;
}