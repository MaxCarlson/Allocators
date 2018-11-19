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
		while (!flag->flag.compare_exchange_strong(free, ContentionFreeFlag::SharedLock)) // TODO: Switch loops to compare_exchange_weak()
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
		enum Status
		{
			Unregistered = -2,
			PrepareToRegister,
			Registered,
		};

		ThreadRegister(SharedMutex& cont) :
			index{	Status::Unregistered },
			cont{	cont }
		{}

		// Unregister the thread and reset it's ID on thread's dtor call
		// (acheived through static thread_local variable)
		~ThreadRegister()
		{
			const auto id = std::this_thread::get_id();
			for(ContentionFreeFlag& cf : cont.flags)
				if (cf.id == id)
				{
					cf.id = std::thread::id{};
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

		else if(idx != ThreadRegister::Unregistered)
			tr.index = idx;

		return tr.index;
	}

	// Check if this thread has been registered before,
	// If not, set it up for registration
	//
	// TODO: Doesn't handle cases where we have more threads than is possible
	ContentionFreeFlag* registerThread()
	{
		int idx = getOrSetIndex();

		// Thread has never been registered
		while (idx == ThreadRegister::PrepareToRegister)
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
						idx		= i;
						f.id	= id;
						getOrSetIndex(idx);
						break;
					}
				}
			}
		}

		return &flags[idx];
	}

	//std::atomic<bool> xLock;

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
