#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include "AllocHelpers.h"

namespace ImplSharedMutex
{
struct ContentionFreeFlag 
{
	enum Flags
	{
		Unregistered,
		Registered,
		SharedLock
	};

	ContentionFreeFlag() :
		id{},
		flag{ Unregistered }
	{}

	std::thread::id			id;
	std::atomic<int>		flag;
	static constexpr auto	SizeOffset = (sizeof(decltype(id)) + sizeof(decltype(flag)));
	alloc::byte				noFalseSharing[64 - SizeOffset]; // TODO: Get rid of warning of non-init
};
} // End ImplSharedMutex::

namespace alloc
{

// A write-contention free version of std::shared_mutex
// threads = number of threads that can be registered
// before falling back on using a non-write contention free lock
template<size_t threads = 4>
class SharedMutex
{
public:

	using CFF = ImplSharedMutex::ContentionFreeFlag;

	SharedMutex() :
		spLock{ false },
		flags{}
	{}

	void lock_shared()
	{
		const int idx = registerThread();

		// Thread is registered
		if (idx >= ThreadRegister::Registered)
		{
			// Spin until the spill lock is unlocked
			while (spLock.load(std::memory_order_acquire))
				;

			// Perform the SharedLock
			flags[idx].flag.store(CFF::SharedLock, std::memory_order_release);
		}

		// Thread is not registered, we must acquire spill lock
		else
		{
			bool locked = false;
			while (!spLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
				locked = false;
		}
	}

	bool try_lock_shared()
	{
		bool locked = false;
		const int idx = registerThread();
		if (idx >= ThreadRegister::Registered)
		{
			if (spLock.load(std::memory_order_acquire)) // TODO: I don't think this is entirely safe!
				return false;

			flags[idx].flag.store(CFF::SharedLock, std::memory_order_release);
			return true;
		}
		else
			spLock.compare_exchange_strong(locked, true, std::memory_order_seq_cst);
		
		return locked;
	}

	template<class Rep, class Period>
	bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& relTime)
	{
		// TODO:
		return false;
	}

	void unlock_shared()
	{
		// TODO: Debug safety check here
		const int idx = getOrSetIndex(ThreadRegister::CheckRegister);

		if (idx >= ThreadRegister::Registered)
			flags[idx].flag.store(CFF::Registered, std::memory_order_release);

		else
			spLock.store(false, std::memory_order_release);
	}

	void lock()
	{
		// Spin until we acquire the spill lock
		bool locked = false;
		while (!spLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
			locked = false;

		// Now spin until all other threads are non-shared locked
		for (CFF& f : flags)
			while (f.flag.load(std::memory_order_acquire) == CFF::SharedLock)
				;
	}

	void unlock()
	{
		// TODO: Debug safety check here
		spLock.store(false, std::memory_order_release);
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
			if (index >= Status::Registered)
			{
				auto& cf	= cont.flags[index];
				cf.id		= defaultThreadId;

				int free = CFF::Registered;
				while (!cf.flag.compare_exchange_weak(free, CFF::Unregistered, std::memory_order_release))
					free = CFF::Registered;
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

		// Thread has never been registered (try to once)
		if (idx == ThreadRegister::PrepareToRegister)
		{
			const auto id = std::this_thread::get_id();
			for (int i = 0; i < threads; ++i)
			{
				auto& f = flags[i];
				int free = CFF::Unregistered;

				if (f.id == id)
				{
					idx = i;
					break;
				}
				else if (f.flag.compare_exchange_strong(free, CFF::Registered))
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

	inline static const auto defaultThreadId = std::thread::id{};

	std::atomic<bool>			spLock;
	std::array<CFF, threads>	flags;
};

template<class Mutex>
class LockGuard
{
public:
	LockGuard(Mutex& mutex) :
		owns{ true },
		mutex{ &mutex }
	{
		this->mutex->lock();
	}

	LockGuard(Mutex& mutex, std::defer_lock_t df) :
		owns{ false },
		mutex{ &mutex }
	{}

	LockGuard(Mutex& mutex, std::adopt_lock_t ad) :
		owns{ true },
		mutex{ &mutex }
	{}

	~LockGuard()
	{
		if(owns)
			mutex->unlock();
	}
private:
	bool	owns;
	Mutex*	mutex;
};

template<class Mutex>
class SharedLock
{
public:
	SharedLock(Mutex& mutex) :
		owns{ true },
		mutex{ &mutex }
	{
		this->mutex->lock_shared();
	}

	SharedLock(Mutex& mutex, std::defer_lock_t df) noexcept :
		owns{ false },
		mutex{ &mutex }
	{}

	SharedLock(Mutex& mutex, std::adopt_lock_t ad) :
		owns{ true },
		mutex{ &mutex }
	{}

	~SharedLock()
	{
		if (owns)
			mutex->unlock_shared();
	}

	void lock()
	{
		mutex->lock_shared();
	}

	void unlock()
	{
		mutex->unlock_shared();
	}

private:

	bool	owns;
	Mutex*	mutex;
};

} // End alloc::
