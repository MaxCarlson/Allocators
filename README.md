# Allocators

### Overview
**Allocators** is a high-performance C++ library implementing custom memory allocators optimized for multithreaded applications. It includes various allocator types, such as **Slab Allocators**, **Free List Allocators**, and **Shared Mutex** for low-contention read-heavy environments. Designed for efficient memory management and allocation in complex systems, **Allocators** enables faster allocation times and fine-grained control over memory usage.

### Features
- **SlabMulti Allocator**: Utilizes thread-local private slabs and a custom, write-contention-free shared mutex for faster allocations across multiple threads.
- **SharedMutex**: A low-contention shared mutex optimized for high-read, low-write operations, compatible with standard locking mechanisms.
- **SlabObj**: Manages object pools, allowing custom construction and deallocation of objects within slabs.
- **SlabMem**: Provides caches of uninitialized memory in configurable block sizes, ideal for allocation of contiguous memory chunks.
- **FreeList Allocator**: Implements traditional and alternative free-list policies for managing memory allocations of fixed sizes.

### Installation
1. **Prerequisites**:
   - A C++ compiler supporting C++17 or later.
   - Visual Studio.

2. **Setup**:
   - Clone the repository:
     ```bash
     git clone https://github.com/YourUsername/Allocators.git
     cd Allocators
     ```
   - Open `Regress.sln` in Visual Studio and build the solution.

### Usage Examples

#### SlabMulti Allocator
Provides thread-local caches for faster memory allocation across multiple threads, using customizable slab sizes, divided into blocks of varying sizes.

```cpp
alloc::SlabMulti<size_t> multi;
std::vector<size_t, decltype(multi)> vec{multi}; // Allocates using SlabMulti
auto* ptr = multi.allocate<uint16_t>(100);
multi.deallocate(ptr, 100);
```

#### SharedMutex
Optimized for high-read scenarios, allowing multiple threads to access shared resources with minimal contention.

```cpp
alloc::SharedMutex mutex;
std::shared_lock lock(mutex);  // Shared lock
```

#### SlabObj
Manages a pool of pre-constructed objects, allowing custom construction and deallocation behavior.

```cpp
alloc::SlabObj<int> slabO;
Large* obj = slabO.allocate<Large, XtorT>();
slabO.deallocate<Large, XtorT>(obj);
```

#### FreeList Allocator
Implements a traditional free-list allocator with various storage policies, such as **FlatPolicy** and **TreePolicy**, for efficient memory reuse.

```cpp
alloc::FreeList<int, listBytes, alloc::FlatPolicy> allocator;
allocator.freeAll();
```

### Project Structure
- **SlabMulti.h**: Implements SlabMulti, a slab allocator with thread-local caches for efficient memory usage in multithreaded applications.
- **SharedMutex.h**: High-performance shared mutex with low write contention.
- **SlabObj.h**: Allocator for managing object pools with customizable construction and deallocation.
- **SlabMem.h**: A memory allocator providing configurable slab caches.
- **FreeList.h**: FreeList allocator with selectable storage policies for managing free memory.

### Contributing
Contributions are welcome!
