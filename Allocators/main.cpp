#include "Linear.h"
#include "Pool.h"
#include "FreeList.h"
#include "Slab.h"

#include <memory>
#include <chrono>
#include <iostream>

using Clock = std::chrono::high_resolution_clock;

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
	alloc::FreeList<int, 513, alloc::TreePolicy> fl1;

	alloc::FreeList<size_t, 512> fl;

	//bool t = fl == fl1;

	using size_type = alloc::FindSizeT<512, 1>::size_type;

	auto* m = fl.allocate(2);
	m[0] = 1;
	m[1] = 2;



	auto* m1 = fl.allocate(2);
	m1[0] = 3;
	m1[1] = 4;

	auto* m2 = fl.allocate(2);

	std::cout << &m[0] << '\n';
	std::cout << &m[1] << '\n';
	std::cout << &m1[0] << '\n';
	std::cout << &m1[1] << '\n';


	fl.deallocate(m);
	fl.deallocate(m2);
	fl.deallocate(m1);

	return 0;
}