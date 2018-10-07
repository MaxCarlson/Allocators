# Allocators


## Implemented (so far)
1. [Slab allocator](https://en.wikipedia.org/wiki/Slab_allocation) 
2. [Free List allocator](https://en.wikipedia.org/wiki/Free_list)
3. [Linear allocator](https://nfrechette.github.io/2015/05/21/linear_allocator/)

### Slab Allocator
The slab allocator breaks from the C++ std::allocator_traits format that the other allocators adhear to. It does this so that it can act as both a multi-object pool, as well as a cache of different (runtime determined) sized memory blocks. Memory caches can be added at runtime and are instatiated by calling ```slab.addCache(objSize, count);```. This creates a contiguous memory block (objSize * cache) in size that is portioned out in objSize increments.

Memory may be allocated by calling either ```slab.allocateMem(objSize);``` or ```slab.allocateMem<objectType <= objSize>();```. Memory may only be requested one allocation at at time. If the requested memory is larger than any cache added, ```std::bad_alloc()``` will be thrown.

### Free List Allocator

### Linear Allocator
