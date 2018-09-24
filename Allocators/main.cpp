#include <memory>
#include "FixedBlock.h"


// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// TODO: Considerations:
// Thread Safety
int main()
{
	std::allocator<int> a;
	a.allocate(5);
	alloc::FixedBlock<int, 4> fb;


	alloc::FixedBlock<int, 4>::rebind<char>::other aa;
	

	return 0;
}