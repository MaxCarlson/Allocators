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
#include <future>

using Clock = std::chrono::high_resolution_clock;

template<class Al, class Tp>
void doWork(Al& al, Tp tp, int seed)
{
	//std::cout << static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tp).count());
	std::vector<int, typename Al::template rebind<int>::other> vec{ al };
	std::default_random_engine re(seed);
	std::uniform_int_distribution dis(1, 10000);

	for (size_t i = 0; i < 100000000000; ++i)
	{
		vec.reserve(vec.size() + dis(re));
		vec.shrink_to_fit();
	}
}

// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// General TODO's:
// Thread Safety with allocators
// Buddy Allocation
// Mix Slab Allocation with existing allocators
int main()
{
	constexpr int count = 1000;
	alloc::SlabMulti<size_t>						multi;
//	std::vector<size_t, alloc::SlabMulti<size_t>>	vec(multi);

	std::vector<std::future<int>> vf;

	for (int i = 0; i < 100000; ++i)
	{
		std::vector<std::thread> th;

		auto tp = Clock::now();
		for (int t = 0; t < 4; ++t)
			//std::async(std::launch::async, [&]() { doWork(multi, tp, i + t); });
			th.emplace_back(std::thread{ [&]() { doWork(multi, tp, i + t); } });
		for (auto& t : th)
			t.join();
	}

	alloc::SharedMutex<0> tex;

	tex.lock_shared();

	tex.unlock_shared();

	tex.lock();
	tex.unlock();


	//auto start = Clock::now();


	return 0;
}