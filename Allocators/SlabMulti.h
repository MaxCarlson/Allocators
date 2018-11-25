#pragma once
#include <array>
#include <numeric>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include "SharedMutex.h"

namespace alloc
{
template<class T>
class SlabMulti;
}

namespace ImplSlabMulti
{

struct Bucket;

template<class>
class alloc::SlabMulti;

using alloc::LockGuard;
using alloc::SharedLock;

constexpr auto SUPERBLOCK_SIZE	= 1 << 20;
constexpr auto SLAB_SIZE		= 1 << 14;
constexpr auto MAX_SLAB_BLOCKS	= 65535;						// Max number of memory blocks a Slab can be divided into 
constexpr auto NUM_CACHES		= 8;
constexpr auto SMALLEST_CACHE	= 64;
constexpr auto LARGEST_CACHE	= SMALLEST_CACHE << (NUM_CACHES - 1);
constexpr auto INIT_SUPERBLOCKS = 4;							// Number of Superblocks allocated per request

static_assert(LARGEST_CACHE <= SLAB_SIZE);

using byte			= alloc::byte;
using IndexSizeT	= alloc::FindSizeT<MAX_SLAB_BLOCKS>::size_type;

auto buildCaches = [](int startSz)
{
	std::vector<int> v;
	for (int i = startSz; i <= LARGEST_CACHE; i <<= 1)
		v.emplace_back(i);
	return v;
};

auto buildBlocksPer = [](const std::vector<int>& cacheSizes)
{
	std::vector<int> v;
	for (const auto s : cacheSizes)
		v.emplace_back(SLAB_SIZE / s);
	return v;
};

const std::vector<int> cacheSizes		= buildCaches(SMALLEST_CACHE);
const std::vector<int> blocksPerSlab	= buildBlocksPer(cacheSizes);

struct GlobalDispatch
{
	using FreeIndicies = std::vector<std::pair<size_t, std::vector<IndexSizeT>>>;

	GlobalDispatch() :
		mutex{},
		blocks{},
		totalSBlocks{ 0 },
		availible{ buildIndicies() }
	{
		requestMem(INIT_SUPERBLOCKS);
	}

	byte* getBlock()
	{
		std::lock_guard lock(mutex); 
		if (blocks.empty())
			requestMem(INIT_SUPERBLOCKS);
		byte* mem = blocks.back();
		blocks.pop_back();
		return mem;
	}

	void returnBlock(byte* block)
	{
		std::lock_guard lock(mutex);
		blocks.emplace_back(block);
	}

	std::vector<IndexSizeT> getIndicies(size_t blockSz) const
	{
		for (const auto bs : availible)
			if (bs.first >= blockSz)
				return bs.second;

		throw std::runtime_error("Incorrect Cache size request");
	}
	
private:

	void requestMem(int sblocks = 1)
	{
		for (int i = 0; i < sblocks; ++i)
		{
			byte* mem = reinterpret_cast<byte*>(operator new(SUPERBLOCK_SIZE));

			for (int idx = 0; idx < SUPERBLOCK_SIZE; idx += SLAB_SIZE)
				blocks.emplace_back(mem + idx);
		}
		totalSBlocks += sblocks;
	}

	// Build the vectors of superblock block indices
	FreeIndicies buildIndicies() const
	{
		int i = 0;
		FreeIndicies av{ static_cast<size_t>(NUM_CACHES) };
		for (auto& a : av)
		{
			a.first = cacheSizes[i];
			a.second.resize(SLAB_SIZE / a.first);
			std::iota(std::rbegin(a.second), std::rend(a.second), 0);
			++i;
		}
		return av;
	}

	std::mutex			mutex;
	std::vector<byte*>	blocks;			// TODO: Should these be kept in address sorted order to improve locality?
	int					totalSBlocks;
	const FreeIndicies	availible;
};

inline GlobalDispatch dispatcher; // TODO: Make this local to the allocator!

struct Slab
{
private:
	using size_type = size_t;

	byte*					mem;
	size_type				blockSize;	// Size of the blocks the super block is divided into
	size_type				count;		// TODO: This can be converted to IndexSizeT 
	std::vector<IndexSizeT>	availible;	// TODO: Issue: Over time allocation locality decreases as indicies are jumbled

public:

	Slab() = default;

	Slab(size_t blockSize, size_t count) : 
		mem{		dispatcher.getBlock()				}, 
		blockSize{	blockSize							}, 
		count{		count								}, 
		availible{	dispatcher.getIndicies(blockSize)	}
	{
	}

	Slab(const Slab& other) noexcept :
		mem{		other.mem		},
		blockSize{	other.blockSize	},
		count{		other.count		},
		availible{	other.availible	}
	{
	}

	Slab(Slab&& other) noexcept :
		mem{		other.mem					},
		blockSize{	other.blockSize				},
		count{		other.count					},
		availible{	std::move(other.availible)	}
	{
		other.mem = nullptr;
	}

	Slab& operator=(Slab&& other) noexcept
	{
		if (mem)
			dispatcher.returnBlock(mem);
		mem			= other.mem;
		blockSize	= other.blockSize;
		count		= other.count;
		availible	= std::move(other.availible);
		other.mem	= nullptr;
		return *this;
	}

	~Slab()
	{
		if (mem)
			dispatcher.returnBlock(mem);
	}
		
	bool full()			const noexcept { return availible.empty(); }
	size_type size()	const noexcept { return count - availible.size(); }
	bool empty()		const noexcept { return availible.size() == count; }

	std::pair<byte*, bool> allocate() 
	{
		auto idx = availible.back();
		availible.pop_back();
		return { mem + (idx * blockSize), availible.empty() };
	}

	template<class P>
	void deallocate(P* ptr)
	{
		auto idx = static_cast<size_type>((reinterpret_cast<byte*>(ptr) - mem)) / blockSize;
		availible.emplace_back(idx);
	}

	template<class P>
	bool containsMem(P* ptr) const noexcept
	{
		return (reinterpret_cast<byte*>(ptr) >= mem
			 && reinterpret_cast<byte*>(ptr) < (mem + blockSize * count)); // TODO: Cache this blocksize * count?
	}
};

class Cache
{
	using size_type = size_t;
	using Container = std::vector<Slab>; 
	using It		= Container::iterator;

	size_type		mySize;
	size_type		myCapacity;
	const size_type	count;
	const size_type	blockSize;
	int				threshold;
	Container		slabs;
	It				actBlock;
	static constexpr double freeThreshold	= 0.25;
	static constexpr int MIN_SLABS			= 1;

public:

	friend struct Bucket;

	Cache(size_type count, size_type blockSize) :
		mySize{		0 },
		myCapacity{ 0 },
		count{		count },
		blockSize{	blockSize },
		threshold{	static_cast<int>(count * freeThreshold) }, // TODO: Cache these values at compile time
		slabs{}
	{
		addCache();
		actBlock = std::begin(slabs);
	}

	Cache(Cache&& other) noexcept :
		mySize{		other.mySize },
		myCapacity{ other.myCapacity },
		count{		other.count },
		blockSize{	other.blockSize },
		threshold{	other.threshold }, 
		slabs{		std::move(other.slabs)},
		actBlock{	other.actBlock }
	{}

	Cache(const Cache& other) : // TODO: Why is this needed?
		mySize{		other.mySize },
		myCapacity{ other.myCapacity },
		count{		other.count },
		blockSize{	other.blockSize },
		threshold{	other.threshold }, 
		slabs{		other.slabs },
		actBlock{	other.actBlock }
	{}
private:

	void splice(It& pos, It it)
	{
		auto val = std::move(*it);
		std::memmove(&*(pos + 1), &*pos, sizeof(Slab) * static_cast<size_t>(it - pos));

		// Two of the same Slab exist after the memmove above.
		// To avoid the destructor call destroying the Slab we want to keep
		// we placement new the value of 'it' into the redundent Slab (pos is then set to correct position)
		new(&*pos) Slab{ std::move(val) };
		pos	= std::begin(slabs) + (static_cast<size_t>(pos - std::begin(slabs)) + 1);
	}

	void addCache()
	{
		slabs.emplace_back(blockSize, count);
		myCapacity += count;
	}

public:
	byte* allocate()
	{
		auto[mem, full] = actBlock->allocate();

		// If active block is full, create a new one and add
		// it to the list before the previous AB
		if (full)
		{
			if (actBlock != std::begin(slabs))
				actBlock = --actBlock;

			else
			{
				auto idx = static_cast<size_t>(actBlock - std::begin(slabs));
				addCache();
				std::swap(slabs[idx], slabs.back());
				actBlock = std::begin(slabs) + idx;
			}

		}
		++mySize;
		return mem;
	}

	template<class T>
	void deallocate(T* ptr) 
	{
		// Look at the active block first, then the fuller blocks after it
		// after that start over from the beginning
		//
		// TODO: Try benchmarking: After looking at blocks after actBlock looking in reverse order
		// from active block
		--mySize;

		auto it		= actBlock;
		for (auto E = std::end(slabs);;)
		{
			if (it->containsMem(ptr))
			{
				it->deallocate(ptr);
				break;
			}

			if (++it == E)				
				it = std::begin(slabs); // TODO: Look into better ways to do this block
		}

		// If the Slab is empty enough place it before the active block
		if (it->size() <= threshold
			&& it > actBlock) 
		{
			splice(actBlock, it);
		}

		// Return memory to Dispatcher
		else if (it->empty() 
			&& slabs.size() > MIN_SLABS
			&& mySize > myCapacity - count) 
		{
			myCapacity -= count;

			if (it != actBlock)
			{
				if (it > actBlock)
				{
					std::swap(*it, slabs.back());
					slabs.pop_back();
				}
				else
				{
					auto idx = static_cast<size_t>(actBlock - std::begin(slabs));
					slabs.erase(it);
					actBlock = std::begin(slabs) + (idx - 1);
				}
			}
			else
			{
				std::swap(*actBlock, slabs.back());
				slabs.pop_back();
				actBlock = std::end(slabs) - 1;
			}
		}
	}
};

/*
struct Cache
{
	using size_type = size_t;
	using Container = std::list<Slab>; 
	using It		= Container::iterator;

	const size_type	count;
	const size_type	blockSize;
	int				threshold;
	Container		slabs;
	It				actBlock;
	static constexpr double freeThreshold	= 0.25;
	static constexpr double MIN_SLABS		= 1;

	Cache(size_type count, size_type blockSize) :
		count{		count },
		blockSize{	blockSize },
		threshold{	static_cast<int>(count * freeThreshold) }, // TODO: Cache these values at compile time
		slabs{}
	{
		slabs.emplace_back(blockSize, count); // TODO: Figure out how to put this in the ctor list!!!
		actBlock = std::begin(slabs);
	}

	Cache(Cache&& other) noexcept:
		count{		other.count },
		blockSize{	other.blockSize },
		threshold{	other.threshold }, 
		slabs{		std::move(other.slabs)},
		actBlock{	other.actBlock }
	{}

	Cache(const Cache& other) : // TODO: Why is this needed?
		count{		other.count },
		blockSize{	other.blockSize },
		threshold{	other.threshold }, 
		slabs{		other.slabs },
		actBlock{	other.actBlock }
	{}

	byte* allocate()
	{
		auto[mem, full] = actBlock->allocate();

		// If active block is full, create a new one and add
		// it to the list before the previous AB
		if (full)
		{
			if (actBlock != std::begin(slabs))
				actBlock = --actBlock;
			else
				actBlock = slabs.emplace(actBlock, blockSize, count);

		}

		return mem;
	}

	template<class T>
	void deallocate(T* ptr) 
	{
		// Look at the active block first, then the fuller blocks after it
		// after that start over from the beginning
		//
		// TODO: Try benchmarking after looking at blocks after actBlock looking in reverse order
		// from active block
		auto it = actBlock;
		for (auto E = std::end(slabs);;)
		{
			if (it->containsMem(ptr))
			{
				it->deallocate(ptr);
				break;
			}

			++it;
			if (it == E)				
				it = std::begin(slabs); // TODO: Look into better ways to do this block
		}

		// If the Slab is empty enough place it before the active block
		// TODO: How to avoid this running multiple times without a random access iterator?
		if (it->size() <= threshold
			&& it != actBlock)
		{
			slabs.splice(actBlock, slabs, it);
		}

		// Return memory to Dispatcher
		else if (it->empty() && slabs.size() > MIN_SLABS) // TODO: If we switch to vector more work will have to be done to keep iterator correct!
		{
			if (it != actBlock)
				slabs.erase(it);
			else
			{
				if (actBlock != std::begin(slabs))
					actBlock = std::prev(it);
				else
					actBlock = std::next(it);
				slabs.erase(it);
			}
		}

		auto& bb = *actBlock;
	}
};
*/
// Bucket of Caches
struct Bucket
{
	using size_type = size_t;

	Bucket()
	{
		caches.reserve(NUM_CACHES);
		for (int i = 0; i < NUM_CACHES; ++i)
			caches.emplace_back(blocksPerSlab[i], cacheSizes[i]);
	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : caches)
			if (c.blockSize >= bytes)
				return c.allocate();

		return reinterpret_cast<byte*>(operator new(bytes)); // TODO: Make sure the de/allocations past Slab sizes are working!
	}

	template<class T>
	void deallocate(T* ptr, size_type n) 
	{
		const auto bytes = sizeof(T) * n;
		for(auto& c : caches)
			if (c.blockSize >= bytes)
			{
				c.deallocate(ptr);
				return;
			}

		operator delete(ptr, bytes); // TODO: Make sure the de/allocations past Slab sizes are working!
	}

private:
	std::vector<Cache> caches;
};

template<class T>
struct SmpVec
{
	SmpVec()
	{}

	template<class... Args>
	decltype(auto) emplace_back(Args&& ...args) 
	{
		//std::lock_guard lock(mutex);
		LockGuard lock(mutex);

		return vec.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	byte* lIterate(Func&& func)
	{
		//std::lock_guard lock(mutex);
		LockGuard lock(mutex);

		for (auto& v : vec)
			if (auto ptr = func(v))
				return ptr;
		return nullptr;
	}

	template<class Func>
	byte* siterate(Func&& func)
	{
		// TODO: Benchmark different lock types here 
		// TODO: Also look into how to not lock unless neccasary here

		//std::shared_lock lock(mutex); 
		SharedLock lock(mutex);
		for (auto& v : vec)
		{
			auto ptr = func(v);
			if (ptr)
				return ptr;
		}
		return nullptr;
	}

	bool empty() 
	{
		//std::shared_lock lock(mutex);
		SharedLock lock(mutex);

		return vec.empty();
	}

private:
	std::vector<T> vec;
	alloc::SharedMutex<8> mutex;
};

struct BucketPair
{
	BucketPair(Bucket&& bucket,
		std::thread::id id) :
		bucket{ std::move(bucket) },
		id{ id }
	{
	}

	BucketPair(BucketPair&& other)		noexcept = default;
	BucketPair(const BucketPair& other) noexcept = default;

	Bucket				bucket;
	std::thread::id		id;
};
// Interface class for SlabMulti so that
// we can have multiple SlabMulti copies pointing
// to same allocator
struct Interface
{
	using size_type	= size_t;
	
	template<class T>
	friend class alloc::SlabMulti;

	Interface() :
		refCount{0}
	{
	}

	~Interface()
	{}

	template<class T>
	T* allocate(size_t count)
	{
		const auto bytes	= sizeof(T) * count;
		const auto id		= std::this_thread::get_id();

		// Create the bucket for this thread.
		// When the thread dies it will destroy this bucket with it

		byte* mem = bucket.allocate(bytes);

		/*
		byte* mem = buckets.siterate([&bytes, &id](BucketPair& p) -> byte*
		{
			if (p.id == id)
				return p.bucket.allocate(bytes);
			return nullptr;
		});

		// We probably haven't made a bucket for this
		// thread yet!
		if (!mem)
		{
			auto& b = buckets.emplace_back(Bucket{}, id);
			mem		= b.bucket.allocate(bytes);
		}
		*/

		return reinterpret_cast<T*>(mem);
	}

	template<class T>
	void deallocate(T* ptr, size_type n)
	{
		// Look in this threads memory Caches first
		const auto id = std::this_thread::get_id();
		bucket.deallocate(ptr, n);
		/*
		buckets.siterate([&](BucketPair& p) -> byte*
		{
			if (p.id == id)
			{
				p.bucket.deallocate(ptr, n);
				return reinterpret_cast<byte*>(ptr); 
			}
			return nullptr;
		});
		*/
		// If not found search other threads
		// TODO: We'll have to either lock the Slab entirely or do sepperate availible lists like TBB
	}

private:

	inline static thread_local Bucket bucket;
	std::atomic<int>	refCount;
	SmpVec<BucketPair>	buckets;
};

}// End SlabMultiImpl::

namespace alloc 
{

// TODO IDEAS:
template<class Type>
class SlabMulti
{
public:
	using STD_Compatible	= std::true_type;
	using Thread_Safe		= std::true_type;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

private:
	ImplSlabMulti::Interface* interfacePtr;

public:

	template<class U>
	friend class SlabMulti;

	template<class U>
	struct rebind { using other = SlabMulti<U>; };

	template<class U>
	bool operator==(const SlabMulti<U>& other) const noexcept { return other.interfacePtr == interfacePtr; }

	template<class U>
	bool operator!=(const SlabMulti<U>& other) const noexcept { return *this == other; }

	SlabMulti() :
		interfacePtr{ new ImplSlabMulti::Interface{} }
	{
		interfacePtr->refCount.fetch_add(1, std::memory_order_relaxed);
	}

	~SlabMulti() 
	{
		// TODO: Make sure this memory ordering is correct, It's probably not!
		if (interfacePtr->refCount.fetch_sub(1, std::memory_order_release) < 1)
			delete interfacePtr;
	}

	template<class U>
	SlabMulti(const SlabMulti<U>& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->refCount.fetch_add(1, std::memory_order_relaxed);
	}

	template<class U>
	SlabMulti(SlabMulti<U>&& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->refCount.fetch_add(1, std::memory_order_relaxed);
	}

	SlabMulti(const SlabMulti& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->refCount.fetch_add(1, std::memory_order_relaxed);
	}

	SlabMulti(SlabMulti&& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
		interfacePtr->refCount.fetch_add(1, std::memory_order_relaxed);
	}

	template<class T = Type>
	T* allocate(size_t count = 1)
	{
		return interfacePtr->allocate<T>(count);
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{
		interfacePtr->deallocate(ptr, n);
	}
};

} // End alloc::