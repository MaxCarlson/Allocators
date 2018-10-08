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
int main()
{
	alloc::Slab<int> slab;

	SlabObj::Interface itfc;

	auto ll = [](Large& lrg) {return 1; };


	itfc.addCache<Large>(10, ll);

	slab.addMemCache(sizeof(char), count);
	slab.addMemCache(sizeof(uint16_t), count);
	slab.addMemCache(sizeof(uint32_t), count);
	slab.addMemCache(sizeof(uint64_t), count);
	slab.addMemCache<Large>(count);


	return 0;
}