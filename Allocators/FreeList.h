#pragma once
#include <list>
#include <map>
#include "AllocHelpers.h"

namespace alloc
{

	enum AlSearch : byte
	{
		BEST_FIT,
		FIRST_FIT
	};

	template<size_t bytes>
	struct TreePolicy
	{
		static byte* MyBegin;
		static std::map<size_t, byte*> availible;

		inline static bool init = 1;
		inline static AlSearch search = BEST_FIT;

		TestPolicy()
		{
			if (init)
			{
				MyBegin = reinterpret_cast<byte*>(operator new (bytes));
				availible.emplace({ bytes, MyBegin });
				init = false;
			}
		}

		byte* bestFit(const size_t byteCount)
		{

		}

		byte* firstFit(const size_t byteCount)
		{
			byte* found = nullptr;
			for (auto it = std::begin(availible);
				it != std::end(availible); ++it)
			{
				// Found a section of memory large enough to hold
				// what we want to allocate
				if (n.second >= byteCount)
				{
					found = it.first;

					// If memory section is larger than what we want
					// add a new list entry the reflects the 
					if (it->second > byteCount)
					{

					}

					availible.erase(it);
					break;
				}
			}

			return found;
		}

		template<class T>
		T* allocate(size_t count)
		{
			bool found = false;
			T* mem;

			const size_t byteCount = sizeof(T) * count;

			if (search == FIRST_FIT)
				mem = firstFit<T>(byteCount);
			else
				mem = bestFit<T>(byteCount);

			if (!found)
				throw std::bad_alloc();
		}
	};

	template<size_t bytes>
	struct ListPolicy
	{

	};

	template<class Type, size_t bytes = 0, template<size_t> class Policy = ListPolicy>
	class FreeList
	{
		Policy<bytes> storage;

	public:


		FreeList() = default;



	};
}