# Allocators


## Implemented (so far)
1. Slab allocators [Wiki](https://en.wikipedia.org/wiki/Slab_allocation) 
    - [SlabMulti](#slabmulti) uses thread-private Slabs as well as a custom, write-contention-free shared mutex to acheive faster allocation times than "new" when dealing with multiple threads.
        - [SharedMutex](#sharedmutex)
    - [SlabObj](#slabobj) holds a cache of any type of objects. Type determined through template specialization
    - [SlabMem](#slabmem) holds m caches of Slabs divided into n byte blocks
2. [Free List allocator](#freelist-allocator) [Wiki](https://en.wikipedia.org/wiki/Free_list)
3. [Linear allocator](#linear-allocator) [Wiki](https://nfrechette.github.io/2015/05/21/linear_allocator/)

## Slab Allocators

### SlabMulti
```cpp
#include "SlabMulti.h"

// SlabMulti uses thread-private Buckets of Caches.
// Each Cache holds 16KB Slabs divided into different chunk sizes,  
// ranging from 64 bytes to 8KB per chunk, growing in powers of two
alloc::SlabMulti<size_t> multi;

// It can be used with std::containers
// (multi itself is used as the allocator here)
std::vector<size_t, decltype(multi)> vecS{multi}; 

// multi is NOT used as the allocator here, instead a new SlabMulti is created
// specifically for this vector (multi isn't passed in ctor here)
std::vector<int, decltype(multi)::template rebind<int>::other> vecI;

// You don't have to rebind it to allocate different types 
auto puI = multi.allocate<uint16_t>(100);
multi.deallocate(puI, 100);
```

#### SlabMulti - Internals
SlabMulti handles Cache sizes internally. Each thread-private Bucket contains 8 Caches, each holding 16KB Slabs divided into 64byte-8KB blocks. When a thread requests memory for the first time it is registered, added to the vector of Buckets, and assigned 8 (one of each size) 16KB Caches holding one Slab each. When a Cache of Slabs runs out of memory, the Cache requests memory from the Dispatcher. The Dispatcher requests memory from the OS in 1MB chunks, and divides those chunks into 16KB Slabs which it then parcels out to Caches that make requests. When a Slab has all its memory returned to it though deallocation, it destroys itself and hands its memory back to the Dispatcher (barring it isn't the only Slab left/empty in the Cache).

### SharedMutex
SharedMutex is a high performance shared mutex best suited to low-write, high-read situations. It has four public functions: shared_lock, shared_unlock, lock, and unlock. SharedMutex is compatible with the std::locks (std::shared/unique_lock, etc) with the functions mentioned previously.

```cpp

// The idea behind SharedMutex is simple. Instead of using a single internal atomic<int>
// and counting how many have shared-access, we keep a flag for each thread registered, 
// ensuring that most writes are to thread-private data.
struct ContentionFreeFlag
{
	std::thread::id		id;
	std::atomic<int>	flag;
	alloc::byte		noFalseSharing[64]; 
};

// SharedMutex takes a template parameter that determines how many threads
// can be registered at a time before we have to start locking the spill lock
template<size_t threads = 4>
class SharedMutex
{
public:
	void sharedLock();
	void sharedUnlock();
	void lock();
	void unlock();

private:

	struct ThreadRegister
	{...};

	int getOrSetIndex();
    	int registerThread();

	std::atomic<bool>			spLock;
	std::array<ContentionFreeFlag, threads> flags;
};

// static thread_local ThreadRegister variabels are used to store registered threads
// indices. This also allows for thread de-registration on thread destruction
SharedMutex<>::getOrSetIndex(int idx = ThreadRegister::Unregistered)
{
	static thread_local ThreadRegister tr(*this);
	...
	return tr.index;
}

// The locking mechinism is expensive, but because it is rarely used
// we get large gains from keeping the shared lock mostly write contention free.
// A similar mechinism is used when we run out of space to register a thread, 
// the thread "spills" out of the array and has to use the spill lock
void SharedMutex<>::lock()
{
	// Spin until we acquire the spill lock
	bool locked = false;
	while (!spLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
		locked = false;

	// Now spin until all other threads are non-shared locked
	for (ContentionFreeFlag& f : flags)
		while (f.flag.load(std::memory_order_acquire) == ContentionFreeFlag::SharedLock)
			;
}

// Instead of using SharedMutex's functions directly, use the std::(locks) RAII wrappers.
void testSharedMutex()
{
	alloc::SharedMutex mutex1;
	alloc::SharedMutex mutex2;
	
	{
		// mutex1's lock function is called
		std::lock_guard  lock(mutex1);
		
		// mutex2's shared_lock function is called
		std::shared_lock lock(mutex2);
		
		//! Cannot upgrade lock! Will result in a deadlock
		// std::lock_guard newLock(mutex2);
		
		//! Cannot recursivly call shared_lock or lock, will cause deadlock 
		// std::shared_lock slock(mutex2);
		
	}// On std::lock_guard and std::shared_locks's destruction unlock and unlock_shared are called respectivly
}
```

### SlabObj
SlabObj acts similarly to SlabMem, but instead of holding caches of un-initialized memory SlabObj creates caches of constructed objects. How those objects are constructed, as well as how they are handled when they are 'deallocated' and sent back to the object pool is completely customizable through template specializations (with either argument forwarding or lambda's). 
```cpp
#include "Slabs.h"

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

### SlabMem
SlabMem is used by creating a variable number of caches of different sizes. Each cache holds Slabs (contiguous chunks of memory) of a particular size that are divided into blocks
```cpp
#include "Slabs.h"

// int is the type this allocator will default to, but it can be overridden
alloc::SlabMem<int> slabM;

// Create a Cache of Slabs divided into 1024, 512 byte blocks 
SlabM.addCache(512, 1024);

// Create another Cache (each Slabs memory is contiguous)
// Maximum number of Caches (not Slabs in Caches) is by default 127, but can be changed in SlabMem.h
SlabM.addCache(1024, 1024);

// Will add Caches at power of 2 multiples of 2048
// until <= the max size 10000. E.g. last Cache added would be Slabs containing 1024 8192 byte blocks
SlabM.addCache2(2048, 10000, 1024);

// In allocating space for one int the smallest block size
// we find is 512 bytes. What a waste!
int* ptrI = SlabM.allocate();

// Do NOT add another Cache after allocations start,
// it can result in undefined behavior

// All alloc::SlabMem's are the same except for their default type (the template parameter)
// as they all use the same storage in a static interface
alloc::SlabMem<size_t>::deallocate(ptrI);

// Finds the smallest Cache that has Slabs divided into blocks
// of at least (sizeof(int) * 128) bytes in size. In this case, the first Cache
ptrI = SlabM.allocate(128);

// Allocate enough space for 257 uint16_t's,
// uses the second cache as allocation would take atleast 514 bytes
uint16_t* ptrS = SlabM.allocate<uint16_t>(257);

// Standard deallocation
slabM.deallocate(ptrI);
slabM.deallocate(ptrS);

// Aside from a memory leak, during the attempted
// 1025th allocation SlabMem will find it is out of room and grow
// the 512 byte cache by adding another Slab of 512 * 1024 bytes
for(int i = 0; i < 1026; ++i)
    alloc::SlabMem<uint32_t>::allocate();

slabM.freeEmpty();    // Frees every empty Slab in every Cache
slabM.freeEmpty(512); // Frees every empty Slab in the 512 byte Cache
slabM.freeAll();      // Frees every Slab in every Cache (does NOT destruct anything)
slabM.freeAll(1024);  // Frees every Slab in the 1024 byte Cache
slabM.info();         // Returns a std::vector<CacheInfo> which holds stats about that Caches
```

### FreeList Allocator
The FreeList allocator is a fixed to a size specified at compile time. It stores info about the free blocks in a variety of different ways through different policies.

```cpp
#include "FreeList.h"

constexpr int listBytes = 1 << 8;

// Traditional Free List allocator that stores info about free
// memory in a list
alloc::FreeList<int, listBytes> listAlloc;

// Stores the free 'list' in a sorted vector
alloc::FreeList<int, listBytes, alloc::FlatPolicy> flatAlloc; 

// Stores the free 'list' in a red-black tree
alloc::FreeList<int, listBytes, alloc::TreePolicy> treeAlloc; 

// FreeList allocators have static Storage based on Policy and Size (listBytes)
// so that any allocators sharing those two are effectively identical.
// alloc::FreeList<size_t, listBytes, alloc::Flat> == alloc::FreeList<int, listBytes, alloc::Flat> 

// Storage size is fixed at compile time and will throw std::bad_alloc() if it runs out

// Frees all the memory in the allocator 
listAlloc.freeAll();
```

### Linear Allocator
