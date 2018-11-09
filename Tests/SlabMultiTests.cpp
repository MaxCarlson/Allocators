#pragma once
#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/SlabMulti.h"
#include <vector>
#include <map>
#include <random>

constexpr int count = 1000;



struct TestStruct
{
	inline static std::atomic<size_t> DtorCount = 0;

	TestStruct(int i) : 
		i{ i } 
	{}

	~TestStruct() 
	{
		++DtorCount;
	}

	bool operator==(const TestStruct& other) const { return i == other.i; }

	int i;
};

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
		std::vector<int> order(num);
		std::default_random_engine re(seed);
		std::iota(std::begin(order), std::end(order), 0);
		std::shuffle(std::begin(order), std::end(order), re);
		return order;
	}

	template<class T>
	struct AlRet
	{
		int totalAllocs = 0;
		std::vector<std::pair<T*, int>> ptrs;
	};

	template<class T>
	AlRet<T> alloc(int maxPerAl, int seed, int num = count)
	{
		AlRet<T> val;
		std::default_random_engine		re(seed);
		std::uniform_int_distribution	dis(1, maxPerAl);

		for(int i = 0; i < num; ++i)
		{
			const auto n = dis(re);
			val.ptrs.emplace_back(multi.allocate<T>(n), n);

			// Set sentinal values so we can make sure everything
			// is working correctly later
			for (int j = 0; j < n; ++j)
				new (val.ptrs.back().first + j) T(n);
			val.totalAllocs += n;
		}
		return val;
	}

	template<class T>
	void dealloc(std::vector<std::pair<T*, int>>& ptrs, const std::vector<int>& order, int num = count)
	{
		for (int i = 0; i < ptrs.size(); ++i)
		{
			auto it = std::begin(ptrs) + order[i];

			for (int j = 0; j < it->second; ++j)
				Assert::IsTrue(*(it->first + j) == it->second);

			multi.deallocate(it->first, it->second);
		}
	}

	TEST_METHOD(AlDe_Serial)
	{
		auto order		= getOrder(1);
		auto ptrsSt		= alloc<size_t>(64, 1);
		auto ptrsTs		= alloc<TestStruct>(64, 2);

		dealloc(ptrsSt.ptrs, order);

		auto tsDtorPre = static_cast<size_t>(TestStruct::DtorCount);
		dealloc(ptrsTs.ptrs, order);
		Assert::IsTrue(TestStruct::DtorCount == tsDtorPre + ptrsTs.totalAllocs);
	}

	TEST_METHOD(Dealloc_Serial)
	{
		// TODO: How to test alloc/dealloc sepperatly here,
		// Probably would help to have a special helper type
	}

	TEST_METHOD(Container_Map)
	{
		decltype(multi)::rebind<std::pair<const size_t, int>>::other al(multi);
		std::map<size_t, int, std::less<size_t>, decltype(al)> m(al);
		
		for (int i = 0; i < count; ++i)
		{
			m.emplace(i, i);
		}

		for (int i = 0; i < count; ++i)
		{
			auto find = m.find(i);
			Assert::IsTrue(find->first == find->second);
			Assert::IsTrue(find->first == i);
		}
	}

	TEST_METHOD(Alloc_Parallel)
	{

	}

	TEST_METHOD(Dealloc_Parallel)
	{

	}

};

}