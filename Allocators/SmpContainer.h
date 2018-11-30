#pragma once
#include "SharedMutex.h"
#include <shared_mutex>
#include <type_traits>

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


// https://stackoverflow.com/a/16824239/7965931

// Primary template with a static assertion
// for a meaningful error message
// if it ever gets instantiated.
// We could leave it undefined if we didn't care.
template<typename, typename T>
struct has_find
{
	static_assert(
		std::integral_constant<T, false>::value,
		"Second template parameter needs to be of function type.");
};

// specialization that does the checking
template<typename C, typename Ret, typename... Args>
struct has_find<C, Ret(Args...)>
{
private:
	template<typename T>
	static constexpr auto check(T*)
		-> typename
		std::is_same<
		decltype(std::declval<T>().find(std::declval<Args>()...)),
		Ret    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		>::type;  // attempt to call it and see if the return type is correct

	template<typename>
	static constexpr std::false_type check(...);
public:

	typedef decltype(check<C>(0)) type;

	static constexpr bool value = type::value;
};

template<class K, class V, class Container>
struct SmpMap 
	: public ImplSmpContainer::SmpContainersBase<K, V, Container>
{
	using MyContainer	= Container;
	using MyBase		= ImplSmpContainer::SmpContainersBase<K, V, MyContainer>;

	using It			= typename MyContainer::iterator;
	using SharedMutex	= typename MyBase::SharedMutex;

	//using typename ImplSmpContainer::has_find;

	template<class... Args>
	decltype(auto) emplace(Args&& ...args)
	{
		std::lock_guard lock(this->mutex);
		return this->cont.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	decltype(auto) findDo(const K& k, Func&& func)
	{
		std::shared_lock lock(this->mutex);
		auto found = find(k, std::false_type{});
		return func(found, this->cont);
	}

	std::pair<std::shared_lock<SharedMutex>&&, It> findAndStartSL(const K& k)
	{
		std::shared_lock lock(this->mutex);
		auto found = find(k, has_find<MyContainer, It(const K&)>::type{});
		return { std::move(lock), found };
	}

private:

	//template<typename std::enable_if<std::is_same<MyContainer, std::vector<std::pair<K, V>>>::value, int>::type = 0>
	It find(K key, std::false_type f)
	{
		return std::find_if(std::begin(this->cont), std::end(this->cont), [&](const auto& v)
		{
			return v.first == key;
		});
	}

	It find(K key, std::true_type t)
	{
		return this->cont.find(key);
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