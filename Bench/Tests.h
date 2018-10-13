#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>

using Clock = std::chrono::high_resolution_clock;

constexpr auto max = 1000000;
constexpr auto count = 9400;

auto RandomEngine = std::default_random_engine(1);

template<class T>
struct TestInit
{
	TestInit(std::vector<std::string> names,
		std::vector<bool> construct,
		std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs)
		: names(names), construct(construct), allocs(allocs) {}

	using MyType = T;

	std::vector<size_t> order;
	std::vector<std::string> names;
	std::vector<bool> construct;
	std::vector<std::pair<std::function<T*()>, std::function<void(T*)>>> allocs;
};

template<class T, class Ctor>
void basicAlloc(std::pair<std::function<T*()>, std::function<void(T*)>>& allocs, std::string toPrint, bool construct, Ctor& ctor)
{
	using TimeType = std::chrono::milliseconds;

	auto&[alloc, dealloc] = allocs;

	auto start = Clock::now();

	size_t num = 0;
	size_t idx = 0;
	size_t deallocTime = 0;
	std::vector<T*> ptrs;

	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
		{
			idx = 0;
			auto start = Clock::now();

			for (auto& ptr : ptrs)
				dealloc(ptr);
			ptrs.clear();

			auto end = Clock::now();
			deallocTime += std::chrono::duration_cast<TimeType>(end - start).count();
		}

		ptrs.emplace_back(alloc());

		if (construct)
			ctor.construct(ptrs[idx]);
	}

	auto end = Clock::now();

	for (auto ptr : ptrs)
		dealloc(ptr);

	std::cout << toPrint.c_str() << " Time: " << std::chrono::duration_cast<TimeType>(end - start).count() - deallocTime << " " << num << '\n';
}

template<class T, class Ctor>
void basicAlDeal(std::pair<std::function<T*()>, std::function<void(T*)>>& allocs, std::string toPrint, bool construct, Ctor& ctor)
{
	auto&[alloc, dealloc] = allocs;

	auto start = Clock::now();

	size_t num = 0;
	size_t idx = 0;
	for (int i = 0; i < max; ++i)
	{
		if (i % count == 0)
			idx = 0;

		auto* loc = alloc();

		if (construct)
			ctor.construct(loc);

		dealloc(loc);
	}

	auto end = Clock::now();

	std::cout << toPrint << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " " << num << '\n';
}