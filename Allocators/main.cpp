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
	alloc::Slab<int> slab;

	//int* iptr = alloc::allocatePage<int>(50000);
	//alloc::alignedFree(iptr);


	SlabObj::Interface itfc;

	auto ll = [&](Large& l) { l = { 12 }; };
	auto ld = [&]() { return 'c'; };

	alloc::CtorArgs ctorA(1, 2, 3);
	alloc::XtorFunc ctorL(ll);

	alloc::Xtors prime(ctorL);

	slab.addObjCache<Large, decltype(prime)>(142, prime);

	//Large* lp = reinterpret_cast<Large*>( operator new(sizeof(Large)));
	//prime.construct(lp);

	/*
	auto ll = [&]() { return 1; };

	SlabObj::CtorFunc ctor(ll);

	auto rt = ctor.func();

	int a = 5;
	SlabObj::CtorArgs tor(1, 3, 4);

	Large aa(tor.construct<Large>());
	*/

	itfc.addCache<Large>(128, prime);

	slab.addMemCache(sizeof(char), count);
	slab.addMemCache(sizeof(uint16_t), count);
	slab.addMemCache(sizeof(uint32_t), count);
	slab.addMemCache(sizeof(uint64_t), count);
	slab.addMemCache<Large>(count);


	return 0;
}