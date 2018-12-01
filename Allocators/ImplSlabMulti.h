#pragma once
#include "SmpContainer.h"
#include "SlabMultiDispatcher.h"
#include <thread>

namespace alloc
{
template<class T>
class SlabMulti;
}

namespace ImplSlabMulti
{

struct Slab
{
private:
	using size_type = size_t;

	byte*					mem;		// TODO: Test moving this outside Slab so we don't have to thrash cache when we dealloc and search mem's
	//byte*					end;		// TODO: Cache this
	size_type				blockSize;	// Size of the blocks the super block is divided into
	size_type				count;		// TODO: This can be converted to IndexSizeT 
	std::vector<IndexSizeT>	availible;	// TODO: Issue: Over time allocation locality decreases as indicies are jumbled

public:

	Slab() = default;

	Slab(size_t blockSize, size_t count) noexcept :
		mem{		dispatcher.getBlock()	},
		blockSize{	blockSize				},
		count{		count					},
		availible{	dispatcher.getIndicies(blockSize) }
	{
	}

	Slab(const Slab& other) noexcept :
		mem{		other.mem		},
		blockSize{	other.blockSize },
		count{		other.count		},
		availible{	other.availible }
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
	Container		slabs;			// TODO: Reorder for padding size
	It				actBlock;
	alloc::SharedMutex<8> mutex;
	static constexpr double freeThreshold	= 0.25;
	static constexpr int MIN_SLABS			= 1;

public:

	friend struct Bucket;

	Cache(size_type count, size_type blockSize) :
		mySize{		0			},
		myCapacity{ 0			},
		count{		count		},
		blockSize{	blockSize	},
		threshold{	static_cast<int>(count * freeThreshold) },
		slabs{},
		mutex{}
	{
		addCache();
		actBlock = std::begin(slabs);
	}

	Cache(Cache&& other) noexcept :
		mySize{		other.mySize			},
		myCapacity{ other.myCapacity		},
		count{		other.count				},
		blockSize{	other.blockSize			},
		threshold{	other.threshold			},
		slabs{		std::move(other.slabs)	},
		actBlock{	other.actBlock			},
		mutex{		std::move(other.mutex)	}
	{}

	Cache(const Cache& other) : // TODO: Why is this needed?
		mySize{		other.mySize		},
		myCapacity{ other.myCapacity	},
		count{		other.count			},
		blockSize{	other.blockSize		},
		threshold{	other.threshold		},
		slabs{		other.slabs			},
		actBlock{	other.actBlock		},
		mutex{}
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
		std::lock_guard lock(mutex);

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
		// TODO: Try benchmarking: After looking at blocks from actBlock to end, 
		// look in reverse order from actBlock to begin
		std::lock_guard lock(mutex);

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
};

// Bucket of Caches
struct Bucket
{
	using size_type = size_t;

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
	std::thread::id		id;
	std::vector<Cache>	caches;
};

} // End ImplSlabMulti::