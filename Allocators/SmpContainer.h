#pragma once
#include "SharedMutex.h"

namespace alloc
{
// TODO: Make this take a third template param, 
// the container so we can test diff container types
template<class K, class V>
struct SmpContainer
{
	SmpContainer() = default;

	template<class... Args>
	decltype(auto) emplace(Args&& ...args)
	{
		std::lock_guard lock(mutex);
		return vec.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	decltype(auto) findDo(const K& k, Func&& func)
	{
		std::shared_lock lock(mutex);
		auto find = std::find_if(std::begin(vec), std::end(vec), [&](const auto& v)
		{
			return v.first == k;
		});
		return func(find, vec);
	}

	decltype(auto) findAndStartSL(const K& k)
	{
		mutex.lock_shared();
		auto find = std::find_if(std::begin(vec), std::end(vec), [&](const auto& v)
		{
			return v.first == k;
		});
		return find;
	}

	void endSL()
	{
		mutex.unlock_shared();
	}

	bool empty()
	{
		std::shared_lock lock(mutex);
		return vec.empty();
	}

private:
	std::vector<std::pair<K, V>> vec;
	alloc::SharedMutex<8>		mutex;
};
}