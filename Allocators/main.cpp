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
	alloc::SlabMem<size_t>::addCache2(sizeof(size_t), 1 << 10, 512);
	alloc::FreeList<int, 50000, alloc::TreePolicy> al;

	
	size_t test = 0;
	size_t deallocT = 0;
	alloc::SlabObj<int>::addCache(100);

	constexpr int count = 100000000;

	int** ptrs = new int*[count];
	size_t idx = 0;

	auto start = Clock::now();
	for (size_t i = 0; i < 1000000000; ++i, ++idx)
	{
		if (idx >= count)
		{
			idx = 0;
			auto start = Clock::now();
			alloc::SlabObj<int>::freeAll();
			auto end = Clock::now();
			deallocT += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		}
		
		ptrs[idx] = alloc::SlabObj<int>::allocate();
		test += *ptrs[idx];
	}

	auto end = Clock::now();

	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() - deallocT << ' ' << test;

	return 0;
}