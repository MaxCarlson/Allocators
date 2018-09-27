#include <memory>
#include <iostream>
#include "Linear.h"
#include "FixedBlock.h"
#include "Pool.h"
#include "FreeList.h"

// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// General TODO's:
// Thread Safety with allocators
//
//
//
int main()
{
	alloc::FreeList<int, 512> fl;

	auto* m = fl.allocate(2);
	m[0] = 1;
	m[1] = 2;
	auto* m1 = fl.allocate(2);
	m1[0] = 3;
	m1[1] = 4;

	std::cout << &m[0] << '\n';
	std::cout << &m[1] << '\n';
	std::cout << &m1[0] << '\n';
	std::cout << &m1[1] << '\n';


	fl.deallocate(m1);
	fl.deallocate(m);


	return 0;
}