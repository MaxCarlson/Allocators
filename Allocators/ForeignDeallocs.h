#pragma once
#include <vector>
#include "ImplSlabMulti.h"
#include "SmpContainer.h"


namespace ImplSlabMulti
{
using alloc::SmpContainer;


// TODO: For Caches, cache MAX and MIN addresses for each Slab size so we can quickly check if a ForeginDeallocation can even
// possibly be found in the Cache
struct ForeignDeallocs
{
	ForeignDeallocs() :
		age{ 0 }
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

	using FptrList = std::list<FPtr>;

	struct FCache
	{
		using It		= FptrList::iterator;
		using rIt		= FptrList::reverse_iterator;
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
					ch.second.emplace_back(it);
		}

		void processDe(std::thread::id id, SmpContainer<std::thread::id, Bucket>& buckets)
		{
			// Find the Bucket and start a shared lock on the SmpContainer
			auto[sLock, find] = buckets.findAndStartSL(id);

			int emptyLevels = 0;

			// Process each level of Cache and try to dealloc foreign ptrs
			for (auto& ch : caches)
			{
				for (auto it = std::rbegin(ch.second),
					E = std::rend(ch.second);
					it != E;)
				{
					// Try and deallocate the ptr
					bool found = false;
					if((*it)->found.load(std::memory_order_relaxed))
						found = find->second.deallocate((*it)->ptr, (*it)->count);
					
					// If we find the ptr mark it as found for other threads
					if (found)
					{
						(*it)->found.store(true, std::memory_order_relaxed);
					}

					++it;
					ch.second.pop_back();
				}

				if (ch.second.empty())
					++emptyLevels;
			}

			// Mark this thread as having no more foreign deallocs to process
			if (emptyLevels == caches.size())
				isEmpty = true;
		}

		bool empty() const noexcept { return isEmpty; }
	};

	template<class T>
	void addPtr(T* ptr, size_t count, size_t bytes, std::thread::id thisThread)
	{
		std::lock_guard lock(mutex); 
		fptrs.emplace_back(reinterpret_cast<byte*>(ptr), bytes, static_cast<IndexSizeT>(count));
		FCache::It it = --std::end(fptrs);

		for (auto& th : myMap)
			th.second.addPtr(it);
	}

	void removePtr(FCache::It it)
	{
	
	}

	void registerThread(std::thread::id id)
	{
		std::lock_guard lock(mutex);
		myMap.emplace(id, FCache{ *this });
	}

	template<class SmpCont>
	void handleDeallocs(std::thread::id id, SmpCont& cont)
	{
		std::shared_lock lock(mutex); 

		// Thread should never be unregistered so we're just going to
		// not check validity of find here
		auto find = myMap.find(id);
		find->second.processDe(id, cont);
	}

	bool hasDeallocs(std::thread::id id)
	{
		std::shared_lock lock(mutex);
		auto find = myMap.find(id);
		return find->second.empty();
	}

	size_t					age;
	FptrList				fptrs;
	alloc::SharedMutex<8>	mutex;
	std::unordered_map<std::thread::id, FCache> myMap; // TODO: Replace with fast umap (or vector)
};

} // End ImplSlabMulti::