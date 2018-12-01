#pragma once
#include <vector>
#include "SlabMultiDispatcher.h"
#include "SmpContainer.h"

namespace ImplSlabMulti
{
using alloc::SmpVector;
using alloc::SharedMutex;

// TODO: For Caches, cache MAX and MIN addresses for each Slab size so we can quickly check if a ForeginDeallocation can even
// possibly be found in the Cache
class ForeignDeallocs
{
public:
	ForeignDeallocs() :
		fptrs{},
		mutex{},
		//deadThreads{},
		myMap{}
	{}

	struct FPtr
	{
		FPtr(byte* ptr, size_t bytes, IndexSizeT count) :
			ptr{	ptr		},
			bytes{	bytes	},
			count{	count	},
			found{	false	}
		{}

		byte*				ptr;
		size_t				bytes;
		IndexSizeT			count;
		std::atomic<bool>	found;
	};


	using FptrList	= std::list<FPtr>;
	using It		= FptrList::iterator;

	struct FCache
	{
		using Cache		= std::pair<size_t, std::vector<It>>;
		using Caches	= std::vector<Cache>;

		bool				isEmpty;
		Caches				caches;
		ForeignDeallocs&	myCont;

		FCache(ForeignDeallocs& myCont) :
			isEmpty{	true	},
			caches{				},
			myCont{		myCont	}
		{
			for (const auto& cs : cacheSizes)
				caches.emplace_back(cs, std::vector<It>{});
		}

		void addPtr(It it)
		{
			isEmpty = false;
			for (auto& ch : caches)
				if (ch.first >= it->bytes)
				{
					ch.second.emplace_back(it);
					return;
				}
		}

		template<class SmpContainer>
		std::vector<It> processDe(std::thread::id id, SmpContainer& buckets)
		{
			// Find the Bucket and start a shared lock on the SmpContainer
			auto[sLock, find] = buckets.findAndStartSL(id);

			int emptyLevels = 0;
			std::vector<It> ptrsFound;

			// Process each level of Cache and try to dealloc foreign ptrs
			for (auto& ch : caches)
			{
				for (auto it = std::rbegin(ch.second),
					E = std::rend(ch.second);
					it != E;)
				{
					// Try and deallocate the ptr
					bool found = false;
					if((*it)->found.load(std::memory_order_relaxed))				// TODO: Might be faster not to check branch depending?
						found = find->second.deallocate((*it)->ptr, (*it)->count);	// TODO: WE know the memory size of the Cache we need, so we should write a custom function that takes an idx param
					
					// If we find the ptr mark it as found for other threads
					if (found)
					{
						(*it)->found.store(true, std::memory_order_relaxed);
						ptrsFound.emplace_back((*it));
					}

					// Remove all ptrs we've checked (or were already marked processed)
					++it;
					ch.second.pop_back();
				}

				if (ch.second.empty())
					++emptyLevels;
			}

			// Mark this thread as having no more foreign deallocs to process
			if (emptyLevels == caches.size())
				isEmpty = true;

			return ptrsFound;
		}

		bool empty() const noexcept { return isEmpty; }
	};

	void registerThread(std::thread::id id)
	{
		std::lock_guard lock(mutex);
		myMap.emplace(id, FCache{ *this });
	}

	template<class T>
	void addPtr(T* ptr, size_t count, size_t bytes)
	{
		fptrs.emplace_back(reinterpret_cast<byte*>(ptr), bytes, static_cast<IndexSizeT>(count));
		It it = --std::end(fptrs);

		//lock.unlock();
		//std::shared_lock slock(mutex);

		for (auto& th : myMap)
			th.second.addPtr(it);
	}

	template<class T, class SmpCont>
	void addPtrAndDealloc(T* ptr, SmpCont& cont, size_t count, size_t bytes, std::thread::id id)
	{
		std::unique_lock lock(mutex);
		addPtr(ptr, count, bytes);
		handleDeallocsImpl(id, cont, lock);
	}

	// Don't ever call this function while holding a lock or shared_lock
	void removePtrs(std::vector<It>& ptrs)
	{
		std::lock_guard lock(mutex); // TODO: This should really just be a lock on the list fptrs, not on the whole *this

		for (const auto& it : ptrs)
			fptrs.erase(it);
	}

	// If we do have foreign deallocations, we need to try and handle them
	template<class SmpCont>
	void handleDeallocs(std::thread::id id, SmpCont& cont, std::unique_lock<SharedMutex<8>>& lock)
	{
		// Thread should never be unregistered so we're just going to
		// not check validity of find here
		auto find = myMap.find(id);
		std::vector<It> ptrsFound = find->second.processDe(id, cont);

		if (!ptrsFound.empty())
		{
			lock.unlock();
			removePtrs(ptrsFound);
		}

		// TODO: Need to decide how to check dead threads
		// lock.lock();
	}

	template<class SmpCont>
	void handleDeallocs(std::thread::id id, SmpCont& cont)
	{
		std::unique_lock lock(mutex);
		handleDeallocs(id, cont, lock);
	}

	// Check if we even have deallocs to try in the first place
	bool hasDeallocs(std::thread::id id)
	{
		std::shared_lock lock(mutex);
		auto find = myMap.find(id);
		return find->second.empty();
	}

//	void unregisterDeadThread(Bucket&& bucket)
	//{
		//deadThreads.emplace_back(std::move(bucket));
//	}

private:

	FptrList				fptrs;
	alloc::SharedMutex<8>	mutex;
	//SmpVector<Bucket>		deadThreads;
	std::unordered_map<std::thread::id, FCache> myMap; // TODO: Replace with fast umap (or vector OR class template it?) ALSO: Should probably be an SmpMap
};

} // End ImplSlabMulti::