#include <memory>
#include "Linear.h"
#include "FixedBlock.h"


// Just a temporary main to test allocators from
// Should be removed in any actual use case
//
// TODO: Considerations:
// Thread Safety
int main()
{
	std::allocator<int> a;

	alloc::Linear<int, sizeof(int) * 4> l;

	auto* data = l.allocate(2);

	data[0] = 1;



	//alloc::FixedBlock<int, 4> fb;
	//alloc::FixedBlock<int, 4>::rebind<char>::other aa;
	

	return 0;
}