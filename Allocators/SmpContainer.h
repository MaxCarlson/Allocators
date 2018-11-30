#pragma once
#include "SharedMutex.h"
#include <shared_mutex>

namespace ImplSmpContainer
{
constexpr int SharedMutexSize	= 8;

template<class K, class V, class Container>
struct SmpContainersBase
{
	using SharedMutex = alloc::SharedMutex<SharedMutexSize>;


	// Wrapper functions so that a lock can be placed directly on the container
	void lock_shared()		{ mutex.lock_shared(); }
	void unlock_shared()	{ mutex.unlock_shared(); }
	void lock()				{ mutex.lock(); }
	void unlock()			{ mutex.unlock(); }

	bool empty()
	{
		std::shared_lock lock(mutex);
		return cont.empty();
	}

	Container	cont;
	SharedMutex	mutex;
};

} // End ImplSmpContainer::

namespace alloc
{

template<class K, class V, class Container>
struct SmpMap : public ImplSmpContainer::SmpContainersBase<K, V, Container>
{
	using MyBase		= ImplSmpContainer::SmpContainersBase<K, V, Container>;

	using It			= typename Container::iterator;
	using SharedMutex	= typename MyBase::SharedMutex;

	template<class... Args>
	decltype(auto) emplace(Args&& ...args)
	{
		std::lock_guard lock(this->mutex);
		return this->cont.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func, typename std::enable_if<std::is_same<Container, std::vector<std::pair<K, V>>>::value, int>::type = 0>
	decltype(auto) findDo(const K& k, Func&& func)
	{
		std::shared_lock lock(this->mutex);
		auto find = std::find_if(std::begin(this->cont), std::end(this->cont), [&](const auto& v)
		{
			return v.first == k;
		});
		return func(find, this->cont);
	}

	std::pair<std::shared_lock<SharedMutex>&&, It> findAndStartSL(const K& k)
	{
		std::shared_lock lock(this->mutex);
		auto find = std::find_if(std::begin(this->cont), std::end(this->cont), [&](const auto& v)
		{
			return v.first == k;
		});
		return { std::move(lock), find };
	}
};

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

	template<class Func, typename std::enable_if<std::is_same<Container, std::vector<std::pair<K, V>>>::value, int>::type = 0>
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