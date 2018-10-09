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

	}

	std::array<int, count> ar;
};

template <class T, class Tuple, size_t... Is>
T construct_from_tuple(Tuple&& tuple, std::index_sequence<Is...>) {
	return T{ std::get<Is>(std::forward<Tuple>(tuple))... };
}

template <class T, class Tuple>
T construct_from_tuple(Tuple&& tuple) {
	return construct_from_tuple<T>(std::forward<Tuple>(tuple),
		std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{}
	);
}

// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// General TODO's:
// Thread Safety with allocators
// Slab Allocation
// Buddy Allocation
// Mix Slab Allocation with existing allocators
int main()
{
	alloc::Slab<int> slab;

	SlabObj::Interface itfc;

	int a = 5;
	SlabObj::Ctor tor(a, 3, 4);

	Large aa(construct_from_tuple<Large>(tor.args));

	itfc.addCache<Large>(128);

	slab.addMemCache(sizeof(char), count);
	slab.addMemCache(sizeof(uint16_t), count);
	slab.addMemCache(sizeof(uint32_t), count);
	slab.addMemCache(sizeof(uint64_t), count);
	slab.addMemCache<Large>(count);


	return 0;
}