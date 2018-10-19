#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include <iomanip>

using Clock		= std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

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

template<class TimeType>
void printTime(TimePoint start, TimePoint end, std::string str, int delta = 0, bool newLine = true)
{
	auto dt = std::chrono::duration_cast<TimeType>(end - start).count() + delta;
	std::cout << std::right << std::setw(4) << str << std::setw(5) << dt << ' ';
	if (newLine)
		std::cout << '\n';
}

template<class Init>
void basicAlloc(Init init)
{
	using T = typename Init::MyType;
	using TimeType = std::chrono::milliseconds;

	int idx = 0;
	int deallocTime = 0;
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
	
	//printTime<TimeType>(start, end, init.name + " Al: ", -deallocTime, false);
	//printTime<TimeType>(TimePoint{}, TimePoint{}, "De: ");

	std::cout << init.name << std::setw(3) << " Al: " << std::setw(5) << std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime << ' ';
	std::cout << "De: "  << std::setw(5) << deallocTime << "\n";
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


// TODO: Probably make the structs meddle functions
// less intensive to give more descrepency in benching
template<class Init>
void memAccess(Init& init, bool seq)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::microseconds;

	auto ptrs = allocateMax(init);

	if(!seq)
		std::shuffle(std::begin(ptrs), std::end(ptrs), init.re); 

	auto start = Clock::now();

	int idx = 0;
	for (auto i = 0; i < iterations; ++i)
	{
		if (idx >= maxAllocs - 1)
			idx = 0;
		ptrs[idx++]->meddle();
	}

	auto end = Clock::now();

	for (auto& ptr : ptrs)
		init.dealloc(ptr);

	printTime<TimeType>(start, end, init.name + ' ');
}

template<class Init>
void rMemAccess(Init init) { memAccess(init, false); }

template<class Init>
void sMemAccess(Init init) { memAccess(init, true); }

// TODO: Concurrency Benchmarks

template<class T, class Ctor, class Al, class De>
struct TestT
{
	using MyType = T;

	TestT(T t, Ctor& ctor, Al& al, De& de, bool singleAl = false, bool useCtor = true) 
		: ctor(ctor), al(al), de(de), singleAl(singleAl), useCtor(useCtor) {}

	Ctor& ctor;
	Al& al;
	De& de;
	bool singleAl;
	bool useCtor;
};

template<class Init, class Alloc>
double basicAlloc(Init& init, Alloc& al)
{
	using T = typename Init::MyType;
	using TimeType = std::chrono::milliseconds;

	int idx = 0;
	int deallocTime = 0;
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
				init.de(ptr);
			ptrs.clear();

			auto end = Clock::now();
			deallocTime += std::chrono::duration_cast<TimeType>(end - start).count();
		}

		ptrs.emplace_back(init.al(T{}, 1));

		if (init.useCtor)
			init.ctor.construct(ptrs[idx]);
		++idx;
	}

	auto end = Clock::now();

	for (auto ptr : ptrs)
		init.de(ptr);

	return std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime;
}