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

	auto ptr = al.allocate(1);

	decltype(al)::rebind<size_t>::other all;

	std::allocator_traits<decltype(al)>::rebind_alloc<size_t> ll;

	std::vector<size_t, decltype(ll)> vec;
	//vec.emplace_back(1U);

	return 0;
}