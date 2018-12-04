#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include <array>
#include <unordered_map>
#include "AllocHelpers.h"

namespace ImplSharedMutex
{
struct ContentionFreeFlag 
{
	enum Flags
	{
		Unregistered,
		Registered,
		PrepareLock,
		SharedLock
	};

	ContentionFreeFlag() noexcept :
		id{},
		flag{ Unregistered }
	{}

	ContentionFreeFlag(ContentionFreeFlag&& other) noexcept :
		id{ std::move(other.id) },
		flag{ other.flag.load() }
	{}

	std::thread::id			id;
	std::atomic<int>		flag;
	static constexpr auto	SizeOffset = (sizeof(decltype(id)) + sizeof(decltype(flag)));
	alloc::byte				noFalseSharing[std::hardware_constructive_interference_size - SizeOffset]; // TODO: Get rid of warning of non-init
};
} // End ImplSharedMutex::

namespace alloc
{

//
// The idea for this SharedMutex came from https://www.codeproject.com/Articles/1183423/We-make-a-std-shared-mutex-times-faster
//

// A write-contention free version of std::shared_mutex
// threads = number of threads that can be registered
// before falling back on using a non-write contention free lock
template<size_t threads = 4>		
class SharedMutex
{
	using CFF = ImplSharedMutex::ContentionFreeFlag;

	inline static const auto defaultThreadId = std::thread::id{};

	std::atomic<bool>			spLock;
	std::array<CFF, threads>*	flags;

public:

	SharedMutex() :
		spLock{ false },
		flags{	new std::array<CFF, threads> }
	{}

	SharedMutex(SharedMutex&& other) noexcept :
		spLock{ other.spLock.load()		}, // TODO: std::memory_order_acquire ?
		flags{	std::move(other.flags)	}
	{
		other.flags = nullptr;
	}

	~SharedMutex() { delete flags; }

	void lock_shared()
	{
		const int idx = registerThread();

		// Thread is registered
		if (idx >= ThreadRegister::Registered)
		{
			// Spin until the spill lock is unlocked
			/*
			(*flags)[idx].flag.store(CFF::SharedLock, std::memory_order_seq_cst);
			while (spLock.load(std::memory_order_seq_cst))
			{
				(*flags)[idx].flag.store(CFF::PrepareLock, std::memory_order_seq_cst);
				for (volatile int f = 0; spLock.load(std::memory_order_seq_cst); ++f)
					;
				(*flags)[idx].flag.store(CFF::SharedLock, std::memory_order_seq_cst);
			}
			*/
			
			while (spLock.load(std::memory_order_acquire))
				;
			
			(*flags)[idx].flag.store(CFF::SharedLock, std::memory_order_release);

			// Perform the SharedLock
		}

		// Thread is not registered, we must acquire spill lock
		else
		{
			bool locked = false;
			while (!spLock.compare_exchange_weak(locked, true, std::memory_order_seq_cst))
				locked = false;
		}
	}

	void unlock_shared()
	{
		// TODO: Debug safety check here
		const int idx = getOrSetIndex();

		if (idx >= ThreadRegister::Registered)
			(*flags)[idx].flag.store(CFF::Registered, std::memory_order_release);

		else
			spLock.store(false, std::memory_order_release);
	}

	void lock()
	{
		// Spin until we acquire the spill lock
		bool locked = false;
		while (!spLock.compare_exchange_weak(locked, true, std::memory_order_acquire))
			locked = false;

		// Now spin until all other threads are non-shared locked
		for (CFF& f : *flags)
			while (f.flag.load(std::memory_order_seq_cst) == CFF::SharedLock)
				;
	}

	void unlock()
	{
		// TODO: Debug safety check here
		spLock.store(false, std::memory_order_seq_cst);
	}

	bool try_lock_shared()
	{
		bool locked = false;
		const int idx = registerThread();
		if (idx >= ThreadRegister::Registered)
		{
			if (spLock.load(std::memory_order_acquire)) // TODO: I don't think this is entirely safe!
				return false;

			(*flags)[idx].flag.store(CFF::SharedLock, std::memory_order_release);
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


private:

	// Keeps track of the threads index if it has one
	// and manages the threads removal from the array on destruction
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

		ThreadRegister(ThreadRegister&& other) noexcept :
			index{	std::move(other.index) },
			cont{	other.cont }
		{
			other.index = Status::Unregistered;
		}

		ThreadRegister(const ThreadRegister& other)				= delete;
		ThreadRegister& operator=(const ThreadRegister& other)	= delete;
		ThreadRegister& operator=(ThreadRegister&& other)		= delete;

		// Unregister the thread and reset it's ID on thread's dtor call
		// (acheived through static thread_local variable)
		~ThreadRegister()
		{
			if (index >= Status::Registered)
			{
				auto& cf	= (*cont.flags)[index];
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
	//
	// TODO: Use a sorted vector or a fast hash map
	int getOrSetIndex(int idx = ThreadRegister::Unregistered)
	{
		using ObjectMap = std::vector<std::pair<void*, ThreadRegister>>;
		static thread_local ObjectMap tmap;

		// TODO: Possibly? Use binary search on sorted vec if size is greater than (?)
		auto find = std::find_if(std::begin(tmap), std::end(tmap), [&](const auto& it)
		{
			return it.first == this;
		});

		if (find == std::end(tmap))
		{
			tmap.emplace_back(this, *this);
			return ThreadRegister::PrepareToRegister;
		}
		else if(idx >= ThreadRegister::Registered)
			find->second.index = idx;

		return find->second.index;
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
			for (int i = 0; i < threads; ++i)
			{
				auto& f		= (*flags)[i];
				int free	= CFF::Unregistered;
				if (f.flag.compare_exchange_strong(free, CFF::Registered, std::memory_order_release))
				{
					idx		= i;
					f.id	= std::this_thread::get_id();
					getOrSetIndex(idx);
					break;
				}
			}

		return idx;
	}
};

} // End alloc::
