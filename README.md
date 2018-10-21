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
// This is the type this allocator will default to, but it can be overridden
alloc::SlabMem<int> slabM;

// Create a cache of 1024, 512 byte blocks
SlabM.addCache(512, 1024);
// Create another cache
SlabM.addCache(1024, 1024);

// Finds the smallest cache that has slabs divided into blocks
// of at least (sizeof(int) * 128) bytes in size. In this case, the first Cache
int* ptrI = SlabM.allocate(128);

// Do NOT add another Cache after allocations start,
// you run the risk of deallocations failing

// Returns a uint16_t pointer to enough spaces for 257
// uint16_t's. In this case, since it's just barely larger
// than our first cache, the space taken from the cache is actually 1024 bytes
// instead of 514
uint16_t* ptrS = SlabM.allocate<uint16_t>(257);

// If allocation included a count, that number must be 
// included in the deallocation. Otherwise the allocator
// could look in the wrong cache
slabM.deallocate(ptrI, 128);
slabM.deallocate(ptrS, 257);

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

// Create a cache of of atleast 1 Large object using Large's default Ctor.
// However, SlabObj will default to the closest (rounding up) page size bytes of objects per cache
slabO.addCache<Large>(1); 

// If you want the objects to be initialized using
// custom arguments you can use alloc::CtorArgs.
// This also allows for multiple specialized caches of the same
// object type through template specialization
std::vector<int> initVec(1, 100);
alloc::CtorArgs ctorLA(5, initVec);

// Create a Cache of Large objects, constructed like so:
// Large{5, initVec};
slabO.addCache<Large, decltype(ctorLA)>(100, ctorLa);

// You can also use lambda's as both
// Ctors as well as functions to apply to your objects
// when they're 'deallocated' (returned to the Cache)
auto lDtor = [&](Large& lrg)
{
    if(lrg.vec.size() > 100)
        lrg.vec.shrink_to_fit();
};

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
