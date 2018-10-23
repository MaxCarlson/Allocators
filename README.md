# Allocators


## Implemented (so far)
1. Slab allocators [Wiki](https://en.wikipedia.org/wiki/Slab_allocation) 
    - [SlabMem](#slabmem) holds m caches of Slabs divided into n byte blocks
    - [SlabObj](#slabobj) holds a cache of any type of objects. Type determined through template specialization
2. [Free List allocator](#freelist-allocator) [Wiki](https://en.wikipedia.org/wiki/Free_list)
3. [Linear allocator](#linear-allocator) [Wiki](https://nfrechette.github.io/2015/05/21/linear_allocator/)

### Slab Allocator
#### SlabMem
SlabMem is used by creating a variable number of caches of different sizes. Each cache holds Slabs (contiguous chunks of memory) of a particular size that are divided into blocks
```cpp
// int is the type this allocator will default to, but it can be overridden
alloc::SlabMem<int> slabM;

// Create a cache of Slabs divided into 1024, 512 byte blocks
SlabM.addCache(512, 1024);
// Create another cache
SlabM.addCache(1024, 1024);

// In allocating space for one int the smallest block size
// we find is 512 bytes. What a waste!
int* ptrI = SlabM.allocate();

// All alloc::SlabMem's are the same except for their default type (the template parameter)
// as they all use the same storage in a static interface
alloc::SlabMem<size_t>::deallocate(ptrI);

// Finds the smallest Cache that has Slabs divided into blocks
// of at least (sizeof(int) * 128) bytes in size. In this case, the first Cache
ptrI = SlabM.allocate(128);

// Do NOT add another (smaller than the largest) Cache after allocations start,
// you run the risk of deallocations failing (will fail)

// Allocate enough space for 257 uint16_t's,
// uses the second cache as allocation would take atleast 514 bytes
uint16_t* ptrS = SlabM.allocate<uint16_t>(257);

// If allocation included a count, that number must be 
// included in the deallocation. Otherwise the allocator
// could look in the wrong cache
slabM.deallocate(ptrI, 128);
slabM.deallocate(ptrS, 257);

// Aside from a memory leak, during the attempted
// 1025 allocation SlabMem will find it is out of room and grow
// the 512 byte cache by adding another Slab of 512 * 1024 bytes
for(int i = 0; i < 1026; ++i)
    alloc::SlabMem<uint32_t>::allocate();

slabM.freeEmpty();    // Frees every empty Slab in every Cache
slabM.freeEmpty(512); // Frees every empty Slab in the 512 byte Cache
slabM.freeAll();      // Frees every Slab in every Cache (does NOT destruct anything)
slabM.freeAll(1024);  // Frees every Slab in the 1024 byte Cache
slabM.info();         // Returns a std::vector<CacheInfo> which holds stats about that Caches
```

#### SlabObj
SlabObj acts similarly to SlabMem, but instead of holding caches of un-initialized memory SlabObj creates caches of constructed objects. How those objects are constructed, as well as how they are handled when they are 'deallocated' and sent back to the object pool is completely customizable through template specializations (with either argument forwarding or lambda's). 
```cpp

// Example struct
struct Large
{
    Large() = default;
    Large(int a, const std::vector<int>& vec) : a(a), b(vec)
    {
        std::fill(std::begin(vec), std::end(vec), a);
    }
    int a;
    std::vector<int> vec;
};

// Create a SlabObj allocator
alloc::SlabObj<int> slabO; 

// Note: The only difference between this and SlabObj<Large> is that the former will default to allocating 
// ints if a type isn't specified, everything is statically stored by type

// Create a cache of Slabs that hold at least 1 Large object (per Slab) using Large's default Ctor.
// However, SlabObj will default to Slab sizes closest (rounding up) to the nearest page size
alloc::SlabObj<Large>::addCache(1); 

// Another syntax 
slabO.addCache<Large>(1);

// If you want the objects to be initialized using
// custom arguments you can use alloc::CtorArgs.
// This also allows for multiple specialized caches of the same
// object type through template specialization
std::vector<int> initVec(1, 100); 
alloc::CtorArgs ctorLA(5, initVec);

// Create a Cache of at least 100 Large objects per Slab, 
// constructed like so: Large{ 5, initVec };
slabO.addCache<Large, decltype(ctorLA)>(100, ctorLa);

// You can also use lambda's as both
// Ctors as well as functions to apply to your objects
// when they're 'deallocated' (returned to the Cache)
auto lDtor = [&](Large& lrg)
{
    if(lrg.vec.size() > 100)
        lrg.vec.shrink_to_fit();
};

// alloc::Xtors allows for custom Ctor/lambda ctor
// to be coupled with a lambda 'dtor'
alloc::Xtors xtors(ctorLA, lDtor);
using XtorT = decltype(xtors);

// Create a Cache of atleast 5000 (per Slab) Large objects that uses the Ctor above
// and also applies the lDtor function on object 'deallocation'
slabO.addCache<Large, XtorT>(5000, xtors);

// In order to allocate an object from a Cache that
// uses custom Ctors/Dtors you must include the type of the Xtors
// in the allocation/deallocation commands
// This returns a pointer to a Large object constructed with the Ctor above
Large* p = slabO.allocate<Large, XtorT>();

// Returns the object back to the pool, and applies
// the lambda above shrinking the vector to fit if size > 100
slabO.deallocate<Large, XtorT>(p);
```

### FreeList Allocator

### Linear Allocator
