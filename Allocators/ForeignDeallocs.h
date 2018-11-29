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
			count{	count	}
		{}

		byte*		ptr;
		size_t		bytes;
		IndexSizeT	count;
	};

	struct FCache
	{
		using It		= std::list<FPtr>::iterator;
		using rIt		= std::list<FPtr>::reverse_iterator;
		using Cache		= std::pair<size_t, std::vector<It>>;
		using Caches	= std::vector<Cache>;

		bool				isEmpty;
		Caches				caches;
		ForeignDeallocs&	myCont;

		FCache(ForeignDeallocs& myCont) :
			isEmpty{ true },
			caches{				},
			myCont{ myCont }
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

			// Process each level of Cache and try to de foreign ptrs
			for (auto& ch : caches)
				for (auto it = std::rbegin(ch.second),
					E = std::rend(ch.second);
					it != E;)
				{
					bool found = find->second.deallocate((*it)->ptr, (*it)->count);

					if (found)
					{
						// Unlock the shared_lock so it's not deadlock
						sLock.unlock();
						{
							// Lock the SMP container
							std::lock_guard lock{ buckets };
							myCont.removePtr(it);
						}

						// Reacquire shared lock
						sLock.lock();
					}

					++it;
					ch.second.pop_back();
				}
		}

		bool empty() const noexcept
		{
			return isEmpty;
		}
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

	void removePtr(FCache::rIt it)
	{
		std::lock_guard lock(mutex);
	}

	void registerThread(std::thread::id id)
	{
		std::lock_guard lock(mutex);
		myMap.emplace(id, FCache{ *this });
	}

	template<class SmpCont>
	void handleDeallocs(std::thread::id id, SmpCont& cont)
	{
		std::lock_guard lock(mutex); // TODO: Use shared lock here and lock guard inside FCache

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
	alloc::SharedMutex<8>	mutex;
	std::list<FPtr>			fptrs;
	std::unordered_map<std::thread::id, FCache> myMap; // TODO: Replace with fast umap (or vector)
};

} // End ImplSlabMulti::