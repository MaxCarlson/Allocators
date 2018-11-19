#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include <iomanip>
#include <string>
#include <future>

// TODO: Encapsualte in "Benchs" namespace

using Clock		= std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

constexpr auto cacheSz		= 1024;

#ifdef NDEBUG
	constexpr auto iterations	= 1000000;
	constexpr auto maxAllocs	= 80000;
#else
	constexpr auto iterations	= 1000;
	constexpr auto maxAllocs	= 800;
#endif

constexpr auto TestThreads	= 4;

template<class Init>
std::vector<typename Init::MyType*> allocMax(Init& init)
{
	using T = typename Init::MyType;
	std::vector<T*> ptrs;
	ptrs.reserve(maxAllocs);

	for (auto i = 0; i < maxAllocs; ++i)
	{
		ptrs.emplace_back(init.al(T{}, 1));
		if (init.useCtor)
			init.ctor.construct(ptrs.back());
	}
	return ptrs;
}

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
				init.de(ptr, 1);
			ptrs.clear();

			auto end = Clock::now();
			deallocTime += static_cast<int>(std::chrono::duration_cast<TimeType>(end - start).count());
		}

		ptrs.emplace_back(init.al(T{}, 1));

		if (init.useCtor)
			init.ctor.construct(ptrs[idx]);
		++idx;
	}

	auto end = Clock::now();

	for (auto ptr : ptrs)
		init.de(ptr, 1);

	return static_cast<double>(std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime);
}

template<class Init, class Alloc>
double basicAlDea(Init& init, Alloc& al)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::milliseconds;

	auto start = Clock::now();

	for (int i = 0; i < iterations; ++i)
	{
		auto* loc = init.al(T{}, 1);

		if (init.useCtor)
			init.ctor.construct(loc);

		init.de(loc, 1);
	}

	auto end = Clock::now();
	return static_cast<double>(std::chrono::duration_cast<TimeType>(end - start).count());
}

template<class Init, class Alloc>
double randomAlDe(Init& init, Alloc& al)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::milliseconds;

	static constexpr int IN_ROW = 5;

	auto ptrs = allocMax(init);

	std::shuffle(std::begin(ptrs), std::end(ptrs), init.re);

	auto tt			= T{};
	int allocs		= 0;
	int deallocs	= 0;
	auto dis		= std::uniform_int_distribution<int>(1, IN_ROW);
	auto start		= Clock::now();

	for (auto i = 0; i < iterations; ++i)
	{
		if (deallocs > ptrs.size() || (allocs <= 0 || ptrs.empty()))
			allocs = dis(init.re);

		else if (deallocs <= 0)
			deallocs = dis(init.re);

		if (allocs && ptrs.size() < maxAllocs)
		{
			ptrs.emplace_back(init.al(tt, 1));

			if (init.useCtor)
				init.ctor.construct(ptrs.back());
			--allocs;
		}
		else if (deallocs > 0 || ptrs.size() + allocs >= maxAllocs)
		{
			auto disDe	= std::uniform_int_distribution<int>(0, static_cast<int>(ptrs.size()) - 1);
			auto idx	= disDe(init.re);
			std::swap(ptrs[idx], ptrs.back());
			init.de(ptrs.back(), 1);
			ptrs.pop_back();
			--deallocs;

			if (deallocs < 0)
				deallocs = 0;
		}
	}
	auto end = Clock::now();

	for (auto& ptr : ptrs)
		init.de(ptr, 1);

	return static_cast<double>(std::chrono::duration_cast<TimeType>(end - start).count());
}

// Measure memory access times, both randomly and sequentially 
template<class Init, class Alloc>
double memAccess(Init& init, Alloc& al, bool seq)
{
	using T			= typename Init::MyType;
	using TimeType	= std::chrono::milliseconds;

	auto ptrs = allocMax(init);

	if (!seq)
		std::shuffle(std::begin(ptrs), std::end(ptrs), init.re);

	auto start = Clock::now();

	int idx = 0;
	for (auto i = 0; i < iterations * 4; ++i)
	{
		if (idx >= maxAllocs - 1)
			idx = 0;
		ptrs[idx++]->meddle();
	}

	auto end = Clock::now();

	for (auto& ptr : ptrs)
		init.de(ptr, 1);

	return static_cast<double>(std::chrono::duration_cast<TimeType>(end - start).count());
}

template<class Init, class Alloc>
double rMemAccess(Init& init, Alloc& al) { return memAccess(init, al, false); }

template<class Init, class Alloc>
double sMemAccess(Init& init, Alloc& al) { return memAccess(init, al, true); }


// Non-Type benchmarks after this point

template<class Init, class Alloc>
double strAl(Init& init, Alloc& al, std::true_type t, size_t iterations, size_t maxAllocs)
{
	using TimeType	= std::chrono::milliseconds;
	using CharAl	= typename Alloc::template rebind<char>::other; 
	using String	= std::basic_string<char, std::char_traits<char>, CharAl>;

	// All we're doing here is creating a vector of type String which 
	// uses the allocator in the constructor (because we don't have direct access
	// to the template we 'rebind' the allocator to String
	std::vector<String, typename Alloc::template rebind<String>::other> strings(al);
	std::vector<uint16_t> lens;
	lens.reserve(maxAllocs);

	// Build string length list
	// Use numbers beyond small string optimizations
	auto dis = std::uniform_int_distribution<size_t>(37, 170); 
	for (int i = 0; i < maxAllocs; ++i)
		lens.emplace_back(dis(init.re));

	int idx		= 0;
	auto start	= Clock::now();
	for (auto i = 0; i < iterations; ++i)
	{
		if (idx % maxAllocs == 0)
		{
			idx = 0;
			strings.clear(); 
		}

		strings.emplace_back(lens[idx], static_cast<char>(lens[idx]), al); 
		++idx;
	}
	strings.clear();
	auto end = Clock::now();

	return static_cast<double>(std::chrono::duration_cast<TimeType>(end - start).count());
}

template<class Init, class Alloc>
double strAl(Init& init, Alloc& al, std::false_type f, size_t i, size_t a) { return 0.0; }

template<class Init, class Alloc>
double stringAl(Init& init, Alloc& al) 
{
	using STDCompat = typename Alloc::STD_Compatible;
	return strAl<Init, Alloc>(init, al, STDCompat{}, iterations, maxAllocs);
}

template<class Init, class Alloc>
double multiStrAl(Init& init, Alloc& al, std::false_type f) { return 0.0; }

template<class Init, class Alloc>
double multiStrAl(Init& init, Alloc& al, std::true_type tt)
{
	using T			= typename Init::MyType;
	using CharAl	= typename Alloc::template rebind<char>::other;
	using String	= std::basic_string<char, std::char_traits<char>, CharAl>;
	using StrAl		= typename Alloc::template rebind<String>::other;

	LockedAl<Alloc, int>				lockAl{ al };
	std::vector<std::future<double>>	futures;

	for (int i = 0; i < TestThreads; ++i)
		futures.emplace_back(
			std::async(std::launch::async, [&]() { 
				return strAl(init, lockAl, tt, iterations / TestThreads, maxAllocs / TestThreads); 
			})
		);

	double total = 0.0;
	for (auto& f : futures)
		total += f.get();

	return total / TestThreads;
}

template<class Init, class Alloc>
double multiStrAl(Init& init, Alloc& al) // TODO: Call strAl with paramers / numThreads on multiple threads
{
	using STDCompat = typename Alloc::STD_Compatible;
	return multiStrAl(init, al, STDCompat{});
}