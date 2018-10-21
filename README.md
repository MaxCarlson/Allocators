# Allocators


## Implemented (so far)
1. [Slab allocator](https://en.wikipedia.org/wiki/Slab_allocation) 
    - SlabMem holds caches of n bytes
    - SlabObj holds a cache of objects
2. [Free List allocator](https://en.wikipedia.org/wiki/Free_list)
3. [Linear allocator](https://nfrechette.github.io/2015/05/21/linear_allocator/)

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
// of atleast (sizeof(int) * 128) bytes in size. In this case, the first Cache
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
```cpp
alloc::SlabObj<int> slabO;


```

### Free List Allocator

### Linear Allocator
