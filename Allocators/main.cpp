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

constexpr int count = 1024;

struct Large
{
	Large() : ar{100}
	{
		std::fill(std::begin(ar), std::end(ar), 0);
	}

	Large(int sz, int val) : ar{sz}
	{
	std::fill(std::begin(ar), std::end(ar), val);
	}

	Large(Large&& other) noexcept :
		ar{ std::move(other.ar) }
	{}

	//Large(Large&& other) = delete;

	Large(const Large& other) :
		ar{ other.ar }
	{
	}

	~Large()
	{
		auto a = 0;
	}

	std::vector<int> ar;
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
	//alloc::SlabMem<size_t>::addCache2(sizeof(size_t), 1 << 10, 512);
	//alloc::FreeList<int, 50000, alloc::TreePolicy> al;
	

	alloc::SlabMulti<size_t> multi;
	
	std::vector<size_t, alloc::SlabMulti<size_t>> vec(multi);
	vec.reserve(10);

	auto p = multi.allocate(2);

	for (int num = 1024, i = 0;
		i < 16; num *= 2, ++i)
	{
		auto start = Clock::now();

		std::vector<Large> ar;
		for (size_t j = 0; j < num; ++j)
		{
			ar.emplace_back(50000, 1);
		}

		auto end = Clock::now();
		std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << '\n';
		ar.clear();
		ar.shrink_to_fit();
	}


	/*
	size_t test = 0;
	size_t deallocT = 0;
	alloc::SlabObj<int>::addCache(100);

	constexpr int count = 100000000;

	size_t** ptrs = new size_t*[count];
	size_t idx = 0;

	auto start = Clock::now();
	for (size_t i = 0; i < 1000000000; ++i, ++idx)
	{
		if (idx >= count)
		{
			idx = 0;
			auto start = Clock::now();
			alloc::SlabMem<size_t>::freeAll(sizeof(size_t));
			//alloc::SlabObj<int>::freeAll();
			auto end = Clock::now();
			deallocT += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		}
		
		//ptrs[idx] = alloc::SlabObj<int>::allocate();
		ptrs[idx] = alloc::SlabMem<size_t>::allocate();
		test += *ptrs[idx];
	}

	auto end = Clock::now();

	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() - deallocT << ' ' << test;
	*/

	return 0;
}