#pragma once
#include "SmpContainer.h"
#include "SlabMultiDispatcher.h"
#include <thread>

namespace alloc
{
template<class T>
class SlabMulti;
}

struct SpinLock
{
	SpinLock() noexcept :
		flag{ ATOMIC_FLAG_INIT }
	{}

	SpinLock(SpinLock&& other)				= delete;
	SpinLock& operator=(SpinLock&& other)	= delete;

	void lock() noexcept
	{
		while (flag.test_and_set(std::memory_order_acquire));
	}

	void unlock() noexcept { flag.clear(std::memory_order_release); }

	std::atomic_flag flag;
};

namespace ImplSlabMulti
{

struct Slab // TODO: Try implementing the availble vector as either a cache-oblvious or array based heap (so we maintain locality in allocations)
{
private:
	using size_type = size_t;

	// TODO: Reorganize for size
	size_type				blockSize;	// Size of the blocks the super block is divided into
	size_type				count;		// TODO: This can be converted to IndexSizeT 
	std::vector<IndexSizeT>	availible;	// list of avalible indicies (foreign threads never touch this)
	std::list<IndexSizeT>	foreigns;	// list of deallocations made by foreign threads
	SpinLock				spinLock;	// Spinlock for foreigns

public:

	Slab() = default;

	Slab(size_t blockSize, size_t count) noexcept :
		blockSize{	blockSize				},
		count{		count					},
		availible{	dispatcher.getIndicies(blockSize) },
		foreigns{},
		spinLock{}
	{}

	Slab(const Slab& other) noexcept :
		blockSize{	other.blockSize },
		count{		other.count		},
		availible{	other.availible },
		foreigns{	other.foreigns	},
		spinLock{}
	{}

	Slab(Slab&& other) noexcept :
		blockSize{	other.blockSize				},
		count{		other.count					},
		availible{	std::move(other.availible)	},
		foreigns{	std::move(other.foreigns)	},
		spinLock{}
	{}

	Slab& operator=(Slab&& other) noexcept
	{
		blockSize	= other.blockSize;
		count		= other.count;
		availible	= std::move(other.availible);
		foreigns	= std::move(other.foreigns);
		return *this;
	}

	~Slab()
	{}

	bool empty()				noexcept { std::lock_guard lock(spinLock); return availible.size() + foreigns.size() == count;	}
	size_type size()			noexcept { std::lock_guard lock(spinLock); return count - (availible.size() + foreigns.size()); }

	std::pair<bool, size_type> emptyAndSize() noexcept
	{
		std::lock_guard lock(spinLock);
		return { availible.size() + foreigns.size() == count,
			count - (availible.size() + foreigns.size()) };
	}

	// Can only be used safely while holding a non-shared lock on Cache
	size_type full()			noexcept { return availible.empty() && foreigns.empty(); }

	std::pair<byte*, bool> allocate(byte* mem)
	{
		auto idx = availible.back();
		availible.pop_back();
		return { mem + (idx * blockSize), availible.empty() };
	}

	template<class P>
	void deallocate(P* ptr, byte* mem, bool thisThread)
	{
		auto idx = static_cast<size_type>((reinterpret_cast<byte*>(ptr) - mem)) / blockSize;

		if (thisThread)
			availible.emplace_back(idx);
		else
		{
			std::lock_guard lock(spinLock);
			foreigns.emplace_back(idx);
		}
	}

	void mergeForeigns()
	{
		std::lock_guard lock(spinLock);
		for (const auto& idx : foreigns)
			availible.emplace_back(idx);
		foreigns.clear();
	}

	// TODO: Look into holding mem outside this class in a vector for faster access!
	static bool containsMem(byte* ptr, byte* mem, size_t blockSize, size_t count) noexcept
	{
		return (ptr >= mem && ptr < mem + blockSize * count);
	}

};

class Cache
{
	using size_type		= size_t;

	template<class T>
	using Container		= std::vector<T>;

	using SIt			= Container<Slab>::iterator;
	using MIt			= Container<byte*>::iterator;

	using SharedMutex	= alloc::SharedMutex<32>;

	// TODO: Reorder for padding size
	std::atomic<size_type>		mySize;
	size_type					myCapacity;
	const size_type				count;
	const size_type				blockSize;
	int							threshold;
	Container<Slab>				slabs;
	Container<byte*>			ptrs;		// Vector of the memory the Slabs manage (in same order as Slab vec)
	SIt							actBlock;	// Slab iterator to active block
	MIt							actMem;		// Mem  iterator to active block
	SharedMutex					mutex;

	static constexpr double		freeThreshold	= 0.25;
	static constexpr int		MIN_SLABS		= 1;

public:

	friend struct Bucket;

	Cache(size_type count, size_type blockSize) :
		mySize{		0			},
		myCapacity{ 0			},
		count{		count		},
		blockSize{	blockSize	},
		threshold{	static_cast<int>(count * freeThreshold) },
		slabs{					},
		ptrs{					},
		mutex{					}
	{
		addCache();
		actBlock	= std::begin(slabs);
		actMem		= std::begin(ptrs);
	}

	Cache(Cache&& other) noexcept :
		mySize{		other.mySize.load(std::memory_order_relaxed) },
		myCapacity{ other.myCapacity			},
		count{		other.count					},
		blockSize{	other.blockSize				},
		threshold{	other.threshold				},
		slabs{		std::move(other.slabs)		},
		ptrs{		std::move(other.ptrs)		},	
		actBlock{	std::move(other.actBlock)	},
		mutex{		std::move(other.mutex)		}
	{}

	Cache(const Cache& other) : // TODO: Why is this needed?
		mySize{		other.mySize.load(std::memory_order_relaxed) },
		myCapacity{ other.myCapacity	},
		count{		other.count			},
		blockSize{	other.blockSize		},
		threshold{	other.threshold		},
		slabs{		other.slabs			},
		ptrs{		other.ptrs			},
		actBlock{	other.actBlock		},
		mutex{}
	{}

private:

	// Place it right before pos
	template<class It, class Item>
	void splice(It& pos, It it, Container<Item>& cont)
	{
		auto val = std::move(*it);
		std::memmove(&*(pos + 1), &*pos, sizeof(Item) * static_cast<size_t>(it - pos));

		// Two of the same Slab exist after the memmove above.
		// To avoid the destructor call destroying the Slab we want to keep
		// we placement new the value of 'it' into the redundent Slab (pos is then set to correct position)
		new(&*pos) Item{ std::move(val) };
		pos = std::begin(cont) + (static_cast<size_t>(pos - std::begin(cont)) + 1);
	}

	void spliceBoth(SIt& spos, MIt &mpos, SIt sit, MIt mit)
	{
		std::lock_guard lock(mutex);
		splice<SIt, Slab >(spos, sit, slabs);
		splice<MIt, byte*>(mpos, mit, ptrs);
	}

	void addCache()
	{
		slabs.emplace_back(blockSize, count);
		ptrs.emplace_back(dispatcher.getBlock());

		myCapacity += count;
	}

	void memToDispatch(SIt sit, MIt mit)
	{
		std::lock_guard lock(mutex);
		myCapacity -= count;

		if (sit != actBlock)
		{
			if (sit > actBlock)
			{
				std::swap(*sit, slabs.back());
				std::swap(*mit, ptrs.back());
				slabs.pop_back();
				ptrs.pop_back();
			}
			else
			{
				auto idx = static_cast<size_t>(actBlock - std::begin(slabs)) - 1;
				slabs.erase(sit);
				ptrs.erase(mit);

				actBlock	= std::begin(slabs) + idx;
				actMem		= std::begin(ptrs)  + idx;
			}
		}
		else
		{
			std::swap(*actBlock, slabs.back());
			std::swap(*actMem, ptrs.back());
			slabs.pop_back();
			ptrs.pop_back();

			actBlock	= std::end(slabs) - 1;
			actMem		= std::end(ptrs) - 1;
		}
	}

	template<class It, class Cont>
	size_t getItIndex(It& it, const Cont& cont) const
	{
		return it - std::begin(cont);
	}

	template<class Cont>
	typename Cont::iterator itFromIdx(size_t index, Cont& cont) const
	{
		return std::begin(cont) + index;
	}

public:

	// No other threads will ever be in this function	
	byte* allocate()
	{
		auto[mem, possiblyFull] = actBlock->allocate(*actMem);

		// If active block is full, create a new one and add
		// it to the list before the previous AB
		if (possiblyFull)
		{
			std::lock_guard lock(mutex);

			// The Slab wasn't actually full and held ptrs deallocated by
			// foreign threads. We'll merge the foreign ptrs with our availible list now
			if (!actBlock->full())
			{
				actBlock->mergeForeigns();
				goto theEnd;
			}

			if (actBlock != std::begin(slabs))
			{
				--actBlock;
				--actMem;
			}

			else
			{
				auto idx = getItIndex(actBlock, slabs);
				addCache();
				std::swap(slabs[idx], slabs.back());
				std::swap(ptrs[idx],  ptrs.back());
				actBlock	= std::begin(slabs) + idx;
				actMem		= std::begin(ptrs)  + idx;
			}
		}

		theEnd:
		mySize.fetch_add(1, std::memory_order_relaxed); // TODO: This atomic decreases speed by ~ 4%
		return mem;
	}

	template<class T>
	bool deallocate(T* ptr, bool thisThread)
	{
		// Look at the active block first, then the fuller blocks after it
		// after that start over from the beginning

		std::shared_lock slock(mutex);

		// TODO: Why not keep each Slab in memory sorted order AS WELL as holding
		// on to an active block? Also, keep a vector of the idx of the next non-full Slab
		// so search time is constant (and pop_back from vec when our active block is full)

		SIt it;
		auto mit = actMem;
		for (auto E = std::end(ptrs);;)
		{
			if (Slab::containsMem(reinterpret_cast<byte*>(ptr), *mit, blockSize, count))
			{
				it = itFromIdx(getItIndex(mit, ptrs), slabs);
				it->deallocate(ptr, *mit, thisThread);
				break;
			}

			if (++mit == E)
			{
				mit = std::begin(ptrs);

				// If we've searched the whole vector we didn't find the Slab
				if (E == actMem || mit == actMem)
					return false;

				E	= actMem;
			}
		}

		// Combine the operations because we almost always need both
		// empty and size, and they both require obtaining a spinLock in the Slab
		auto[empty, size] = it->emptyAndSize();

		// If the Slab is empty enough place it before the active block
		if (size <= threshold
			&& it > actBlock)
		{
			slock.unlock();
			spliceBoth(actBlock, actMem, it, mit);
		}

		// Return memory to Dispatcher
		else if (empty
			&& slabs.size() > MIN_SLABS
			&& mySize > myCapacity - count)
		{
			slock.unlock();
			memToDispatch(it, mit);
		}

		mySize.fetch_sub(1, std::memory_order_relaxed);

		// TODO: Add Bucket awareness so if the thread is killed and we have no more elements left
		// we destroy the bucket
		return true;
	}
};

// Bucket of Caches
struct Bucket
{
	using size_type = size_t;

	enum { NOT_FOUND = -1, FOUND };

private:
	std::vector<Cache>	caches;

public:

	Bucket() :
		caches{}
	{
		caches.reserve(NUM_CACHES);
		for (int i = 0; i < NUM_CACHES; ++i)
			caches.emplace_back(blocksPerSlab[i], cacheSizes[i]);
	}

	Bucket(Bucket&& other) noexcept :
		caches{	std::move(other.caches) }
	{}

	~Bucket()
	{
	}

	byte* allocate(int idx, size_type bytes)
	{
		if (idx >= FOUND)
			return caches[idx].allocate();

		return reinterpret_cast<byte*>(operator new(bytes));
	}

	template<class T>
	bool deallocate(T* ptr, int idx, size_type bytes, bool thisThread)
	{
		if (idx >= FOUND)
			return caches[idx].deallocate(ptr, thisThread);

		operator delete(ptr, bytes);
		return true;
	}
};

} // End ImplSlabMulti::