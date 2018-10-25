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
	Large()
	{
		std::fill(std::begin(ar), std::end(ar), 0);
	}

	Large(int val)
	{
	std::fill(std::begin(ar), std::end(ar), val);
	}

	Large(int a, int b, int c)
	{
		std::fill(std::begin(ar), std::end(ar), a * b * c);
	}

	~Large()
	{
		auto a = 0;
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
	alloc::FreeList<int, 50000, alloc::TreePolicy> al;

	auto ptr = al.allocate(1);

	constexpr int length = 64 * 1024 * 1024;
	int* arr = new int[length];

	// Loop 1
	auto st = Clock::now();
	for (int i = 0; i < length; i++) arr[i] *= 3;
	auto end = Clock::now();


	// Loop 2
	auto st2 = Clock::now();
	for (int i = 0; i < length; i += 4) arr[i] *= 3;
	auto end2 = Clock::now();

	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - st).count() <<
		" " << std::chrono::duration_cast<std::chrono::milliseconds>(end2 - st2).count();

	return 0;
}