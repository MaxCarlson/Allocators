#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include <iomanip>

using Clock = std::chrono::high_resolution_clock;

constexpr auto cacheSz		= 1024;
//constexpr auto iterations	= 1000;
constexpr auto iterations	= 5000000;
constexpr auto maxAllocs	= 22000;

// Holds arguments for all tests of a type
template<class T>
struct TestInit
{
	TestInit(std::vector<std::string> names,
		std::vector<bool> construct,
		std::vector<bool> skip,
		std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs)
		: names(names), construct(construct), skip(skip), allocs(allocs) {}

	using MyType = T;

	std::vector<size_t> order;
	std::vector<std::string> names;
	std::vector<bool> construct;
	std::vector<bool> skip;
	std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs;
};

// Holds arguments for individual tests of a type
template<class T, class Ctor>
struct IdvTestInit
{
	using MyType = T;

	IdvTestInit(TestInit<T>& init, int idx, Ctor& ctor, std::default_random_engine re)
		: order(order), name(init.names[idx]), construct(init.construct[idx]), 
		  alloc(init.allocs[idx].first), dealloc(init.allocs[idx].second), ctor(ctor), re(re) {}

	const std::vector<size_t>& order;
	const std::string& name;
	bool construct;
	std::function<T*()>& alloc;
	std::function<void(T*)>& dealloc;
	Ctor& ctor;
	std::default_random_engine re;
};

template<class Init>
decltype(auto) allocateMax(Init& init)
{
	using T = typename Init::MyType;
	std::vector<T*> ptrs;
	ptrs.reserve(maxAllocs);

	for (auto i = 0; i < maxAllocs; ++i)
	{
		ptrs.emplace_back(init.alloc());
		if (init.construct)
			init.ctor.construct(ptrs.back());
	}
	return ptrs;
}

template<class Init>
void basicAlloc(Init init)
{
	using T = typename Init::MyType;
	using TimeType = std::chrono::milliseconds;

	size_t idx = 0;
	size_t deallocTime = 0;
	std::vector<T*> ptrs;
	ptrs.reserve(maxAllocs);

	auto start = Clock::now();

	for (int i = 0; i < iterations; ++i)
	{
		if (i % maxAllocs == 0)
		{
			idx = 0;
			auto start = Clock::now();

			for (auto& ptr : ptrs)
				init.dealloc(ptr);
			ptrs.clear();

			auto end = Clock::now();
			deallocTime += std::chrono::duration_cast<TimeType>(end - start).count();
		}

		ptrs.emplace_back(init.alloc());

		if (init.construct)
			init.ctor.construct(ptrs[idx]);
		++idx;
	}

	auto end = Clock::now();

	for (auto ptr : ptrs)
		init.dealloc(ptr);

	std::cout << init.name << " Alloc time: " << std::setw(3) << std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime << ' ';
	std::cout << "Dealloc time: " << deallocTime << "\n\n";
}

template<class Init>
void basicAlDeal(Init init)
{
	auto start = Clock::now();

	for (int i = 0; i < iterations; ++i)
	{
		auto* loc = init.alloc();

		if (init.construct)
			init.ctor.construct(loc);

		init.dealloc(loc);
	}

	auto end = Clock::now();
	std::cout << init.name << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << '\n';
}

template<class Init>
void randomAlDe(Init init)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::milliseconds;

	static constexpr int IN_ROW = 5;
	
	auto ptrs = allocateMax(init);

	std::shuffle(std::begin(ptrs), std::end(ptrs), init.re); 
	
	auto start = Clock::now();

	int allocs		= 0;
	int deallocs	= 0;
	auto dis		= std::uniform_int_distribution<int>(1, IN_ROW);

	for (auto i = 0; i < iterations; ++i)
	{
		if (deallocs > ptrs.size() || (allocs <= 0 || !ptrs.size()))
			allocs = dis(init.re);

		else if (deallocs <= 0)
			deallocs = dis(init.re);

		if (allocs && ptrs.size() < maxAllocs)
		{
			ptrs.emplace_back(init.alloc());

			if (init.construct)
				init.ctor.construct(ptrs.back());
			--allocs;
		}
		else if (deallocs || ptrs.size() + allocs >= maxAllocs)
		{
			init.dealloc(ptrs.back());
			ptrs.pop_back();
			--deallocs;
		}
	}

	auto end = Clock::now();
	
	for (auto& ptr : ptrs)
		init.dealloc(ptr);

	std::cout << init.name << " Time: " << std::chrono::duration_cast<TimeType>(end - start).count() << '\n';
}

// TODO: Should we make functions to handle different
// types of allocation, then use those in other functions with more specific benchmarks,
// to reduce need to re-write randomized allocation/deallocatin code for example?
template<class Init>
void randomAlDe(Init& init)
{

}

// TODO: Find a good way to test things like lots of local access,
// accessing different caches in the alloc if applicable, etc
template<class Init>
void memUsageAl(Init init)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::milliseconds;

	auto ptrs = allocateMax(init);
	std::shuffle(std::begin(ptrs), std::end(ptrs), init.re); 

	auto start = Clock::now();

}

// TODO: Concurrency Benchmarks