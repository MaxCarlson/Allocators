#pragma once
#include <atomic>
#include <thread>
#include "AllocHelpers.h"

struct ContentionFreeFlag
{
	enum Flags
	{
		Unregistered,
		Registered,
		SharedLock,
	};

	ContentionFreeFlag() :
		id{},
		flag{ Unregistered }
	{}

	std::thread::id			id;
	std::atomic<int>		flag;
	static constexpr auto	SizeOffset = (sizeof(decltype(id)) + sizeof(decltype(flag)));
	alloc::byte				noFalseSharing[64 - SizeOffset];
};
///*

template<size_t threads = 4>
class SharedMutex
{

public:

	SharedMutex() :
		xLock{ false },
		flags {}
	{}

	void lockShared()
	{
		const int idx = registerThread();

		// Thread is registered
		if (idx >= ThreadRegister::Registered)
		{
			ContentionFreeFlag* flag	= &flags[idx];

			// Perform the SharedLock
			flag->flag.store(ContentionFreeFlag::SharedLock, std::memory_order_seq_cst);

			// Spin until the master/overflow lock is unlocked
			while (xLock.load(std::memory_order_seq_cst))
				;
		}
		
		// Thread is not registered, we must acquire overflow lock
		else
		{
			bool locked = false;
			while (!xLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
				locked = false;
		}
	}

	void unlockShared()
	{
		// TODO: Debug safety check here
		const int idx = getOrSetIndex(ThreadRegister::CheckRegister);

		if (idx >= ThreadRegister::Registered)
		{
			ContentionFreeFlag* flag = &flags[idx];
			flag->flag.exchange(ContentionFreeFlag::Registered);
		}
		else
			xLock.store(false, std::memory_order_release);
	}

	void lock()
	{
		// Spin until we acquire the master/overflow lock
		bool locked = false;
		while (!xLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
			locked = false;

		// Now spin until all other threads are non-shared locked
		for (ContentionFreeFlag& f : flags)
		{
			while (f.flag.load() == ContentionFreeFlag::SharedLock)
				;
		}
	}

	void unlock()
	{
		// TODO: Debug safety check here
		xLock.store(false, std::memory_order_release);
	}

private:

	// Keeps track of the threads index if it has one
	// and manages the threads removal from the array on destruction
	struct ThreadRegister
	{
		enum Status
		{
			CheckRegister = -3,
			Unregistered,
			PrepareToRegister,
			Registered,
		};

		ThreadRegister(SharedMutex& cont) :
			index{ Status::Unregistered },
			cont{ cont }
		{}

		// Unregister the thread and reset it's ID on thread's dtor call
		// (acheived through static thread_local variable)
		~ThreadRegister()
		{
			const auto id = std::this_thread::get_id();
			for (ContentionFreeFlag& cf : cont.flags)
				if (cf.id == id)
				{
					cf.id = defaultThreadId;
					int free = ContentionFreeFlag::Registered;
					while (!cf.flag.compare_exchange_weak(free, ContentionFreeFlag::Unregistered))
						free = ContentionFreeFlag::Registered;

					break;
				}
		}

		int				index;
		SharedMutex&	cont;
	};

	// Find the index of this thread in our array
	// If it's never been registered, prepare it to 
	// be or set it's registered index
	int getOrSetIndex(int idx = ThreadRegister::Unregistered)
	{
		static thread_local ThreadRegister tr(*this);

		if (tr.index == ThreadRegister::Unregistered)
			tr.index = ThreadRegister::PrepareToRegister;

		else if (idx > ThreadRegister::Unregistered)
			tr.index = idx;

		return tr.index;
	}

	// Check if this thread has been registered before,
	// If not, set it up for registration
	//
	// TODO: Doesn't handle cases where we have more threads than is possible
	int registerThread()
	{
		int idx = getOrSetIndex();

		// Thread has never been registered
		if (idx == ThreadRegister::PrepareToRegister)
		{
			auto id = std::this_thread::get_id();
			for (int i = 0; i < threads; ++i)
			{
				auto& f = flags[i];
				int val = ContentionFreeFlag::Unregistered;

				if (f.id == id)
				{
					idx = i;
					break;
				}
				else if (f.flag.compare_exchange_strong(val, ContentionFreeFlag::Registered))
				{
					idx		= i;
					f.id	= id;
					getOrSetIndex(idx);
					break;
				}
			}
		}

		return idx;
	}

	inline static const auto defaultThreadId = std::thread::id{};

	std::atomic<bool>						xLock;
	// Thead (shared) private locks
	std::array<ContentionFreeFlag, threads> flags;
};

template<class Mutex>
class LockGuard
{
public:
	LockGuard(Mutex& mutex) :
		mutex{ mutex }
	{
		mutex.lock();
	}

	~LockGuard()
	{
		mutex.unlock();
	}
private:
	Mutex& mutex;
};

template<class Mutex>
class SharedLock
{
public:
	SharedLock(Mutex& mutex) :
		mutex{ mutex }
	{
		mutex.lockShared();
	}

	~SharedLock()
	{
		mutex.unlockShared();
	}
private:
	Mutex& mutex;
};
