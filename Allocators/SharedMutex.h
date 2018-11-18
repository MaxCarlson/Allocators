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
		Locked
	};

	ContentionFreeFlag() :
		id{},
		flag{ Unregistered }
	{}

	std::thread::id			id;
	std::atomic<int>		flag;
	static constexpr int	SizeOffset = (sizeof(decltype(id)) + sizeof(decltype(flag)));
	alloc::byte				noFalseSharing[64 - SizeOffset];
};

template<size_t threads = 4>
class SharedMutex
{

public:

	SharedMutex() :
		flags{}
	{
	}

	void lockShared()
	{
		ContentionFreeFlag* flag = registerThread();

		// Perform the SharedLock
		int free = ContentionFreeFlag::Registered;
		while (!flag->flag.compare_exchange_strong(free, ContentionFreeFlag::SharedLock))
			free = ContentionFreeFlag::Registered;
	}

	void unlockShared()
	{
		ContentionFreeFlag* flag = registerThread();

		flag->flag.exchange(ContentionFreeFlag::Registered);
	}

	// Acquire a lock on every thread object
	// TODO: There's probably a better way to do this?
	void lock()
	{
		for (ContentionFreeFlag& f : flags)
		{
			int notShared		= ContentionFreeFlag::Registered;
			int unregistered	= ContentionFreeFlag::Unregistered;
			while (!(f.flag.compare_exchange_strong(notShared, ContentionFreeFlag::Locked)
				|| f.flag.compare_exchange_strong(unregistered, ContentionFreeFlag::Locked)))
			{
				notShared		= ContentionFreeFlag::Registered;
				unregistered	= ContentionFreeFlag::Unregistered;
			}
		}
	}

	void unlock()
	{
		static const auto defaultThread = std::thread::id{};

		for (ContentionFreeFlag& f : flags)
		{
			if (f.id != defaultThread)
				f.flag.exchange(ContentionFreeFlag::Registered);
			else
				f.flag.exchange(ContentionFreeFlag::Unregistered);
		}

	}

private:

	struct ThreadRegister
	{
		ThreadRegister(SharedMutex& cont) :
			index{ -1 },
			unset{ true },
			cont{ cont }
		{}

		~ThreadRegister()
		{
			const auto id = std::this_thread::get_id();
			for(ContentionFreeFlag& cf : cont.flags)
				if (cf.id == id)
				{
					int free = ContentionFreeFlag::Registered;
					while (!cf.flag.compare_exchange_weak(free, ContentionFreeFlag::Unregistered))
						free = ContentionFreeFlag::Registered;
				}
		}

		int				index;
		bool			unset;
		SharedMutex&	cont;
	};

	// Find the index of this thread in our array
	// If it's never been registered, prepare it to 
	// be or set it's registered index
	int getOrSetIndex(int idx = -1)
	{
		//static thread_local ThreadRegister tr;
		static thread_local int index	= -1;
		static thread_local bool unset	= true;

		if (unset)
			unset = false;

		else if(idx != -1)
			index = idx;

		return index;
	}

	// Check if this thread has been registered before,
	// If not, set it up for registration
	//
	// TODO: Doesn't handle cases where we have more threads than is possible
	ContentionFreeFlag* registerThread()
	{
		int idx = getOrSetIndex();

		// Thread has never been registered
		if (idx == -1)
		{
			auto id = std::this_thread::get_id();
			for (int i = 0; i < threads; ++i)
			{
				ContentionFreeFlag& f = flags[i];
				if (f.id == id)
				{
					idx = i;
					break;
				}
				else if (f.flag.load() == ContentionFreeFlag::Unregistered)
				{
					int val = ContentionFreeFlag::Unregistered;
					if (f.flag.compare_exchange_strong(val, ContentionFreeFlag::Registered))
					{
						idx = i;
						f.id = id;
						getOrSetIndex(idx);
						break;
					}
				}
			}
			//auto* b = &flags[idx];
		}

		return &flags[idx];
	}

	std::atomic<bool> xLock;

	// Thead (mostly) private locks
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
