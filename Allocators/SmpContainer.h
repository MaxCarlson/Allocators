#pragma once
#include "SharedMutex.h"
#include <shared_mutex>

namespace alloc
{
// TODO: Make this take a third template param, 
// the container so we can test diff container types
template<class K, class V>
struct SmpContainer
{
	SmpContainer() = default;

	using SharedMutex	= alloc::SharedMutex<8>;
	using Container		= std::vector<std::pair<K, V>>;
	using It			= typename Container::iterator;

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

	std::pair<std::shared_lock<SharedMutex>&&, It> findAndStartSL(const K& k)
	{
		std::shared_lock lock(mutex);
		auto find = std::find_if(std::begin(vec), std::end(vec), [&](const auto& v)
		{
			return v.first == k;
		});
		return { std::move(lock), find };
	}

	// Wrapper functions so that a lock can be placed directly on the container
	void lock_shared()		{ mutex.lock_shared(); }
	void unlock_shared()	{ mutex.unlock_shared(); }
	void lock()				{ mutex.lock(); }
	void unlock()			{ mutex.unlock(); }

	bool empty()
	{
		std::shared_lock lock(mutex);
		return vec.empty();
	}

private:
	Container	vec;
	SharedMutex	mutex;
};
}