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
	
	constexpr int count = 100;

	using namespace SlabMultiImpl;

	alloc::SlabMulti<size_t> multi;

	std::vector<Slab> sls;
	
	std::vector<size_t, alloc::SlabMulti<size_t>> vec(multi);
	vec.reserve(10);

	std::vector<int> order(count);
	//std::uniform_int_distribution<int> dis(0, count / 4);
	std::default_random_engine re;

	std::iota(		std::begin(order), std::end(order), 0);
	std::shuffle(	std::begin(order), std::end(order), re);


	for (int i = 0; i < count; ++i)
		sls.emplace_back(64, 256);
	
	auto start = Clock::now();
	for(auto f = 0; f < 50; ++f)
	{
		for (int i = 0; i < count / 2; ++i)
		{
			int j = 0;
			for(auto it = std::begin(sls), E = std::end(sls); it != E; ++it, ++j)
				if (j == order[i])
				{
					sls.erase(it);
					break;
				}
		}
		for (int i = 0; i < count / 2; ++i)
		{
			int j = 0;
			for (auto it = std::begin(sls), E = std::end(sls); it != E; ++it, ++j)
				if (j == order[i])
				{
					sls.emplace(it, 64, 256);
					break;
				}
		}

		std::shuffle(std::begin(order), std::end(order), re);
	}

	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();


	size_t *ar[count];
	for (int i = 0; i < count; ++i)
	{
		ar[i] = multi.allocate(1);
	}

	while (true)
	{
		for (int i = 0; i < count / 3; ++i)
		{
			multi.deallocate(ar[order[i]], 1);
		}

		for (int i = 0; i < count / 3; ++i)
			ar[order[i]] = multi.allocate(1);

		std::shuffle(std::begin(order), std::end(order), re);
	}



	//auto start = Clock::now();


	return 0;
}