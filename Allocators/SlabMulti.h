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
constexpr auto NUM_CACHES		= 7;
constexpr auto SMALLEST_CACHE	= 64;
constexpr auto LARGEST_CACHE	= SMALLEST_CACHE << NUM_CACHES;
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
	using FreeIndicies = std::vector<std::pair<size_t, std::vector<SlabImpl::IndexSizeT>>>;

	GlobalDispatch() :
		mutex{},
		blocks{},
		availible{ buildIndicies() }
	{
		requestMem(INIT_SUPERBLOCKS);
	}

	byte* getBlock()
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (blocks.empty())
			requestMem(INIT_SUPERBLOCKS);
		byte* mem = blocks.back();
		blocks.pop_back();
		return mem;
	}

	void returnBlock(byte* block)
	{
		std::unique_lock<std::mutex> lock(mutex);
		blocks.emplace_back(block);
	}

	std::vector<SlabImpl::IndexSizeT> getIndicies(size_t blockSz) const
	{
		for (const auto bs : availible)
			if (bs.first >= blockSz)
				return bs.second;
	}
	
private:

	void requestMem(int sblocks = 1)
	{
		std::unique_lock<std::mutex> lock(mutex);
		for (int i = 0; i < sblocks; ++i)
		{
			byte* mem = reinterpret_cast<byte*>(operator new(SUPERBLOCK_SIZE));

			for (int idx = 0; idx < SUPERBLOCK_SIZE; idx += SLAB_SIZE)
			{
				auto* m = mem + idx;
				blocks.emplace_back(m);
			}
		}
	}

	// Build the vectors of superblock block indices
	FreeIndicies buildIndicies()
	{
		int i = 0;
		FreeIndicies av{ static_cast<size_t>(NUM_CACHES) };
		for (auto& a : av)
		{
			a.first = SLAB_SIZE / cacheSizes[i];
			a.second.resize(a.first);
			std::iota(std::rbegin(a.second), std::rend(a.second), 0);
			++i;
		}
		return av;
	}

	std::mutex			mutex;
	std::vector<byte*>	blocks; // TODO: Should these be kept in address sorted order to improve locality?
	const FreeIndicies	availible;
};

inline GlobalDispatch dispatcher; // TODO: Make this local to the allocator!

struct Slab
{
private:
	using size_type = size_t;

	byte*								mem;
	size_type							blockSize;	// Size of the blocks the super block is divided into
	size_type							count;		// TODO: This can be converted to IndexSizeT 
	std::vector<SlabImpl::IndexSizeT>	availible;	// TODO: Issue. Overtime allocation locality decreases as indicies are jumbled

public:

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
	using Container = std::list<Slab>;
	using It		= Container::iterator;

	const size_type	count;
	const size_type	blockSize;
	int				threshold;
	Container		slabs;
	It				actBlock;
	static constexpr double freeThreshold = 0.25;

	Cache(size_type count, size_type blockSize) :
		count{		count },
		blockSize{	blockSize },
		threshold{	static_cast<int>(count * freeThreshold) }, // TODO: Cache these values at compile time
		slabs{},
		actBlock{	std::begin(slabs) }
	{
		slabs.emplace_back(blockSize, count); 
	}

	Cache(Cache&& other) noexcept:
		count{		other.count },
		blockSize{	other.blockSize },
		threshold{	other.threshold }, // TODO: Cache these values at compile time
		slabs{		std::move(other.slabs)},
		actBlock{	other.actBlock }
	{}

	byte* allocate()
	{
		auto[mem, full] = actBlock->allocate();

		// If active block is full, create a new one and add
		// it to the list before the previous AB
		if (full)
			actBlock = slabs.emplace(actBlock, blockSize, count);

		return mem;
	}

	void deallocate(byte* ptr) 
	{

	}
};

// Bucket of Caches
struct Bucket
{
	using size_type = size_t;

	std::vector<Cache> buckets;

	Bucket()
	{
		for (int i = 0; i < NUM_CACHES; ++i)
			buckets.emplace_back(blocksPerSlab[i], cacheSizes[i]);
	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : buckets)
			if (c.blockSize >= bytes)
				return c.allocate();

		throw std::bad_alloc();
	}

	template<class T>
	void deallocate(T* ptr) 
	{

	}
};

template<class T>
struct SmpVec
{
	SmpVec()
	{}

	template<class... Args>
	decltype(auto) emplace_back(Args&& ...args) 
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		return vec.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	byte* lIterate(Func&& func)
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		for (auto& v : vec)
			if (auto ptr = func(v))
				return ptr;
		return nullptr;
	}

	template<class Func>
	byte* siterate(Func&& func)
	{
		std::shared_lock<std::shared_mutex> lock(mutex);
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
		std::shared_lock<std::shared_mutex> lock(mutex);
		return vec.empty();
	}

private:
	std::vector<T> vec;
	mutable std::shared_mutex mutex;
};

struct BucketPair
{
	BucketPair(Bucket bucket,
		std::thread::id id) :
		bucket{ bucket },
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

	SmpVec<BucketPair> buckets;

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
		
		// Then other threads
	}
};

}// End SlabMultiImpl::

namespace alloc // Move this to Slab.h eventually
{

// TODO IDEAS:
// TODO: Should this have a bookeeping thread for other-than-alloc-operations?
// TODO: Create a global Pool of Slabs that can be passed to Caches in need 
// (that way we can treat the issue of possibly having Slabs that are never used
// in the vector of Caches)
//
template<class Type>
class SlabMulti
{
public:
	using STD_Compatible	= std::true_type;

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
		interfacePtr{ std::move(other.interfacePtr) }
	{
		other.interfacePtr = nullptr;
	}

	template<class T = Type>
	T* allocate(size_t count)
	{
		return interfacePtr->allocate<T>(count);
	}

	template<class T = Type>
	void deallocate(T* ptr, size_type n)
	{
		interfacePtr->deallocate(ptr, n);
	}
};

}