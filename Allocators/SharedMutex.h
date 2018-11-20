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
	static constexpr auto	SizeOffset = (sizeof(decltype(id)) + sizeof(decltype(flag)));
	alloc::byte				noFalseSharing[64 - SizeOffset];
};

template<size_t threads = 4>
class SharedMutex
{

public:

	SharedMutex() :
		xLock{ false },
		flags {}
	{
	}

	void lockShared()
	{
		int idx = registerThread();

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
		//int idx = getOrSetIndex();

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
					idx = i;
					f.id = id;
					getOrSetIndex(idx);
					break;
				}
			}
		}

		return idx;
	}

	std::atomic<bool> xLock;

	inline static const auto defaultThreadId = std::thread::id{};

	// Thead (shared) private locks
	std::array<ContentionFreeFlag, threads> flags;
};

/*
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
		while (!flag->flag.compare_exchange_weak(free, ContentionFreeFlag::SharedLock)) 
			free = ContentionFreeFlag::Registered;
	}

	void unlockShared()
	{
		// TODO: Debug safety check here
		
		ContentionFreeFlag* flag = registerThread();
		flag->flag.exchange(ContentionFreeFlag::Registered);
	}

	void lock()
	{
		// Acquire a lock on every thread object
		for (ContentionFreeFlag& f : flags)
		{
			int notShared		= ContentionFreeFlag::Registered;
			int unregistered	= ContentionFreeFlag::Unregistered;
			while (!(f.flag.compare_exchange_weak(notShared, ContentionFreeFlag::Locked)
				  || f.flag.compare_exchange_weak(unregistered, ContentionFreeFlag::Locked)))
			{
				notShared		= ContentionFreeFlag::Registered;
				unregistered	= ContentionFreeFlag::Unregistered;
			}
		}
	}

	void unlock()
	{
		// TODO: Debug safety check here

		for (ContentionFreeFlag& f : flags)
		{
			if (f.id != defaultThreadId)
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
		// TODO: This will loop forever if flags is full!!
		while (idx == ThreadRegister::PrepareToRegister)
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

		return &flags[idx];
	}

	//std::atomic<bool> xLock;

	inline static const auto defaultThreadId = std::thread::id{};

	// Thead (shared) private locks
	std::array<ContentionFreeFlag, threads> flags;
};
*/


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
