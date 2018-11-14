#pragma once
#include "AllocHelpers.h"

#include <numeric>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <shared_mutex>

namespace alloc
{
template<class T>
class SlabMulti;
}


namespace SlabMultiImpl
{

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
		mem{		dispatcher.getBlock() }, 
		blockSize{	blockSize }, 
		count{		count }, 
		availible{	dispatcher.getIndicies(blockSize) }
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
			 && reinterpret_cast<byte*>(ptr) < (mem + blockSize * count));
	}
};

struct Cache
{
	using size_type = size_t;
	using Container = std::list<Slab>; // TODO: Will test with std::vector<Slab> as in testing with Slabs it appears near as fast in bad senarios
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
			if (it == E)				// TODO: Optimization: Move another loop outside of this one, get rid of branch
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
		std::lock_guard lock(mutex);
		return vec.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	byte* lIterate(Func&& func)
	{
		std::lock_guard lock(mutex);
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

		std::shared_lock lock(mutex); 
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
		std::shared_lock lock(mutex);
		return vec.empty();
	}

private:
	std::vector<T> vec;
	std::shared_mutex mutex; // TODO: Benchmark speed with normal mutex
};

struct BucketPair
{
	BucketPair(Bucket&& bucket,
		std::thread::id id) :
		bucket{ std::move(bucket) },
		id{ id }
	{
	}

	BucketPair(BucketPair&& other) noexcept = default;
	BucketPair(const BucketPair& other) noexcept: // TODO: Revisit this. This shouldn't really need to exist
		bucket{bucket},
		id{id}
	{}


	Bucket				bucket;
	std::thread::id		id;
	//std::atomic_flag	inUse = ATOMIC_FLAG_INIT;
};
// Interface class for SlabMulti so that
// we can have multiple SlabMulti copies pointing
// to same allocator
struct Interface
{
	using size_type		= size_t;

	Interface() 
	{
	}

	template<class T>
	T* allocate(size_t count)
	{
		const auto bytes	= sizeof(T) * count;
		const auto id		= std::this_thread::get_id();

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

		return reinterpret_cast<T*>(mem);
	}

	template<class T>
	void deallocate(T* ptr, size_type n)
	{
		// Look in this threads memory Caches first
		const auto id = std::this_thread::get_id();

		buckets.siterate([&](BucketPair& p) -> byte*
		{
			if (p.id == id)
			{
				p.bucket.deallocate(ptr, n);
				return reinterpret_cast<byte*>(ptr); // This is really a hack here
			}
			return nullptr;
		});
		
		// Then other threads
		// TODO: We'll have to either lock the Slab entirely or do sepperate availible lists like TBB
	}

private:
	SmpVec<BucketPair> buckets;
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
	SlabMultiImpl::Interface* interfacePtr;

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
		interfacePtr{ new SlabMultiImpl::Interface{} }
	{

	}

	// TODO: Add reference counting to interface so we know when to destory it
	// AND benchmark the cost. Could easily be an issue with the way std::containers use allocators 
	~SlabMulti() 
	{
	
	}

	template<class U>
	SlabMulti(const SlabMulti<U>& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
	}

	template<class U>
	SlabMulti(SlabMulti<U>&& other) noexcept :
		interfacePtr{ other.interfacePtr }
	{
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