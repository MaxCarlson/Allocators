#pragma once
#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/SlabMulti.h"
#include <vector>
#include <random>

constexpr int count = 1000;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

TEST_CLASS(SlabMultiTests)
{
public:

	alloc::SlabMulti<size_t> multi;

	TEST_CLASS_INITIALIZE(init)
	{
	}

	std::vector<int> getOrder(int seed, int num = count)
	{
		std::vector<int> order{ num };
		std::default_random_engine re(seed);
		std::iota(std::begin(order), std::end(order), 0);
		std::shuffle(std::begin(order), std::end(order), re);
		return order;
	}

	template<class T>
	std::vector<std::pair<T*, int>> alloc(int maxPerAl, int seed, int num = count)
	{
		std::vector<std::pair<T*, int>> ptrs;
		std::default_random_engine		re(seed);
		std::uniform_int_distribution	dis(1, maxPerAl);

		for(int i = 0; i < num; ++i)
		{
			const auto n = dis(re);
			ptrs.emplace_back(multi.allocate<T>(n), n);
		}
		return ptrs;
	}

	TEST_METHOD(Alloc_Serial)
	{
		auto ptrs		= alloc<size_t>(64, 1);
		auto order		= getOrder(1);

	}

	TEST_METHOD(Dealloc_Serial)
	{

	}
};

}