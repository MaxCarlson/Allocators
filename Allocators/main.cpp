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

int A = 0;
int r1 = 0;
int r2 = 0;
std::atomic<int> ready;

void testFences(bool d)
{
	if (d)
	{
		A = 42;
		ready.store(1, std::memory_order_release);
	}
	else
	{
		r1 = ready.load(std::memory_order_acquire);
		r2 = A;
	}
}

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

	/*
	while (true)
	{
		auto t1 = std::thread{ []() {testFences(true); } };
		auto t2 = std::thread{ []() {testFences(false); } };

		t1.join(); t2.join();

		A = r1 = r2 = ready = 0;
		
	}
	*/


	constexpr int count = 1000;

	alloc::SlabMulti<size_t>						multi;
//	std::vector<size_t, alloc::SlabMulti<size_t>>	vec(multi);

	SharedMutex<0> tex;

	tex.lockShared();

	tex.unlockShared();

	tex.lock();
	tex.unlock();


	//auto start = Clock::now();


	return 0;
}