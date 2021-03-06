#pragma once
#include "SharedMutex.h"
#include <shared_mutex>
#include <type_traits>

namespace ImplSmpContainer
{
constexpr int SharedMutexSize	= 8;

template<class Container>
struct SmpContainersBase
{
	using SharedMutex = alloc::SharedMutex<SharedMutexSize>;

	SmpContainersBase() = default;

	SmpContainersBase(SmpContainersBase&& other) noexcept :
		cont{	std::move(other.cont)	},
		mutex{	std::move(other.mutex)	}
	{}

	// Wrapper functions so that a lock can be placed directly on the container
	void lock_shared()		{ mutex.lock_shared(); }
	void unlock_shared()	{ mutex.unlock_shared(); }
	void lock()				{ mutex.lock(); }
	void unlock()			{ mutex.unlock(); }

	bool empty()
	{
		return cont.empty();
	}

	Container	cont;
	SharedMutex	mutex;
};


} // End ImplSmpContainer::

namespace alloc
{

// TODO: Should be out of alloc:: 
// 
// This is a super nifty couple of structs that can tell us
// if a class has a function called find with(args...) as a signature
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

// A wrapper for Map like classes that containers shared/lock protected functions
template<class K, class V, class Container>
struct SmpMap 
	: public ImplSmpContainer::SmpContainersBase<Container>
{
	using MyContainer	= Container;
	using MyBase		= ImplSmpContainer::SmpContainersBase<MyContainer>;

	using It			= typename MyContainer::iterator;
	using SharedMutex	= typename MyBase::SharedMutex;

	//using typename ImplSmpContainer::has_find;
	static constexpr auto hasFind = has_find<MyContainer, It(const K&)>::type{};


	template<class... Args>
	decltype(auto) emplace(Args&& ...args) // TODO: This should take an iterator, and not be emplace back!
	{
		std::lock_guard lock(this->mutex);
		return this->cont.emplace_back(std::forward<Args>(args)...);
	}

	template<class Func>
	decltype(auto) findDo(const K& k, Func&& func)
	{
		std::shared_lock lock(this->mutex);
		auto found = find(k, hasFind);
		return func(found, this->cont);
	}

	// Takes a lambda which if it returns true stops the loop
	// lambda takes a single argument equal to the basic unit of the Container
	template<class Func>
	void iterate(Func&& func)
	{
		std::shared_lock lock(this->mutex);
		for (auto& it : this->cont)
		{
			auto ret = func(it);
			if (ret)
				return;
		}
	}

private:

	//template<typename std::enable_if<std::is_same<MyContainer, std::vector<std::pair<K, V>>>::value, int>::type = 0>
	It find(const K& key, std::false_type)
	{
		return std::find_if(std::begin(this->cont), std::end(this->cont), [&](const auto& v)
		{
			return v.first == key;
		});
	}

	It find(const K& key, std::true_type)
	{
		return this->cont.find(key);
	}
};

template<class Type, template<class> class Container = std::vector>
class SmpVector : ImplSmpContainer::SmpContainersBase<Container<Type>>
{
public:
	using MyContainer	= Container<Type>;
	using MyBase		= ImplSmpContainer::SmpContainersBase<MyContainer>;

	using iterator		= typename MyContainer::iterator;
	using SharedMutex	= typename MyBase::SharedMutex;

	SmpVector() : 
		MyBase{}
	{}

	SmpVector(SmpVector&& other) noexcept :
		MyBase{ std::move(other) }
	{}


	template<class... Args>
	decltype(auto) emplace_back(Args&& ...args)
	{
		std::lock_guard lock(this->mutex);
		return this->cont.emplace_back(std::forward<Args>(args)...);
	}

	template<class It, class... Args>
	decltype(auto) emplace(It it, Args&& ...args)
	{
		std::lock_guard lock(this->mutex);
		return this->cont.emplace(it, std::forward<Args>(args)...);
	}
};


}