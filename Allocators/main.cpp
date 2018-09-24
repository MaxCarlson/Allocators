#include <memory>
#include "Linear.h"
#include "FixedBlock.h"


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
	std::allocator<int> a;

	alloc::Linear<int, sizeof(int) * 4> l;
	alloc::Linear<char, sizeof(int) * 4> alc;
	alloc::Linear<char, sizeof(int) * 8> alc8;


	auto* data = l.allocate(2);

	data[0] = 1;

	//alloc::FixedBlock<int, 4> fb;
	//alloc::FixedBlock<int, 4>::rebind<char>::other aa;
	

	return 0;
}