#pragma once
#include <numeric>
#include <thread>
#include <mutex>
#include <atomic>

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


constexpr auto SUPERBLOCK_SIZE = 1 << 20;
constexpr auto SLAB_SIZE = 1 << 14;
constexpr auto MAX_SLAB_BLOCKS = 65535;						// Max number of memory blocks a Slab can be divided into 
constexpr auto NUM_CACHES = 8;
constexpr auto SMALLEST_CACHE = 64;
constexpr auto LARGEST_CACHE = SMALLEST_CACHE << (NUM_CACHES - 1);
constexpr auto INIT_SUPERBLOCKS = 4;							// Number of Superblocks allocated per request

static_assert(LARGEST_CACHE <= SLAB_SIZE);

using byte = alloc::byte;
using IndexSizeT = alloc::FindSizeT<MAX_SLAB_BLOCKS>::size_type;

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

const std::vector<int> cacheSizes = buildCaches(SMALLEST_CACHE);
const std::vector<int> blocksPerSlab = buildBlocksPer(cacheSizes);

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

inline GlobalDispatch dispatcher; // TODO: Make this local to the allocator

struct Slab
{
private:
	using size_type = size_t;

	byte*					mem;
	//byte*					end;		// TODO: Cache this
	size_type				blockSize;	// Size of the blocks the super block is divided into
	size_type				count;		// TODO: This can be converted to IndexSizeT 
	std::vector<IndexSizeT>	availible;	// TODO: Issue: Over time allocation locality decreases as indicies are jumbled

public:

	Slab() = default;

	Slab(size_t blockSize, size_t count) :
		mem{ dispatcher.getBlock() },
		blockSize{ blockSize },
		count{ count },
		availible{ dispatcher.getIndicies(blockSize) }
	{
	}

	Slab(const Slab& other) noexcept :
		mem{ other.mem },
		blockSize{ other.blockSize },
		count{ other.count },
		availible{ other.availible }
	{
	}

	Slab(Slab&& other) noexcept :
		mem{ other.mem },
		blockSize{ other.blockSize },
		count{ other.count },
		availible{ std::move(other.availible) }
	{
		other.mem = nullptr;
	}

	Slab& operator=(Slab&& other) noexcept
	{
		if (mem)
			dispatcher.returnBlock(mem);
		mem = other.mem;
		blockSize = other.blockSize;
		count = other.count;
		availible = std::move(other.availible);
		other.mem = nullptr;
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

class Cache
{
	using size_type = size_t;
	using Container = std::vector<Slab>;
	using It = Container::iterator;

	size_type		mySize;
	size_type		myCapacity;
	const size_type	count;
	const size_type	blockSize;
	int				threshold;
	Container		slabs;
	It				actBlock;
	static constexpr double freeThreshold = 0.25;
	static constexpr int MIN_SLABS = 1;

public:

	friend struct Bucket;

	Cache(size_type count, size_type blockSize) :
		mySize{ 0 },
		myCapacity{ 0 },
		count{ count },
		blockSize{ blockSize },
		threshold{ static_cast<int>(count * freeThreshold) },
		slabs{}
	{
		addCache();
		actBlock = std::begin(slabs);
	}

	Cache(Cache&& other) noexcept :
		mySize{ other.mySize },
		myCapacity{ other.myCapacity },
		count{ other.count },
		blockSize{ other.blockSize },
		threshold{ other.threshold },
		slabs{ std::move(other.slabs) },
		actBlock{ other.actBlock }
	{}

	Cache(const Cache& other) : // TODO: Why is this needed?
		mySize{ other.mySize },
		myCapacity{ other.myCapacity },
		count{ other.count },
		blockSize{ other.blockSize },
		threshold{ other.threshold },
		slabs{ other.slabs },
		actBlock{ other.actBlock }
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
		pos = std::begin(slabs) + (static_cast<size_t>(pos - std::begin(slabs)) + 1);
	}

	void addCache()
	{
		slabs.emplace_back(blockSize, count);
		myCapacity += count;
	}

	void memToDispatch(It it)
	{
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
	bool deallocate(T* ptr)
	{
		// Look at the active block first, then the fuller blocks after it
		// after that start over from the beginning
		//
		// TODO: Try benchmarking: After looking at blocks after actBlock looking in reverse order
		// from active block

		auto it = actBlock;
		for (auto E = std::end(slabs);;)
		{
			if (it->containsMem(ptr))
			{
				it->deallocate(ptr);
				break;
			}

			if (++it == E)
				it = std::begin(slabs);

			// If this Cache doesn't contain the memory location
			// we need to tell the caller to look elsewhere			
			if (it == actBlock)
				return false;
		}

		// Handles the iterator in cases where it changes
		// and returns memory to dispatcher if a Slab is empty
		memToDispatch(it);

		--mySize;
		return true;
	}

	bool isEmpty() const noexcept
	{
		for (const auto& s : slabs)
			if (s.size())
				return false;
		return true;
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

	Bucket(Bucket&& other) noexcept :
		caches(std::move(other.caches))
	{}

	~Bucket()
	{

	}

	byte* allocate(size_type bytes)
	{
		for (auto& c : caches)
			if (c.blockSize >= bytes) // TODO: Can this be done more effeciently than a loop since the c.blockSize is always the same?
				return c.allocate();

		return reinterpret_cast<byte*>(operator new(bytes));
	}

	template<class T>
	bool deallocate(T* ptr, size_type n)
	{
		const auto bytes = sizeof(T) * n;
		for (auto& c : caches)
			if (c.blockSize >= bytes) // TODO: Can this be done more effeciently than a loop since the c.blockSize is always the same?
				return c.deallocate(ptr);

		operator delete(ptr, bytes);
		return true;
	}

private:
	std::vector<Cache> caches;
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

} // End ImplSlabMulti::