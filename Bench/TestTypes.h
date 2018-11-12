#pragma once
#include <array>
#include <string>
#include <mutex>

// Helper struct for printing average 
// scores over all test ( Must be outside of main for good printing)
struct AveragedScores {};
// Helper struct for non-type dependent tests
struct NonType { void meddle() {} };


template<class Type>
struct DefaultAlloc
{
	DefaultAlloc() = default;

	template<class U>
	DefaultAlloc(const DefaultAlloc<U>& other) {};

	using STD_Compatible	= std::true_type;
	using Thread_Safe		= std::true_type;
	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

	template<class T = Type>
	T* allocate() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T = Type>
	T* allocate(size_t count) { return reinterpret_cast<T*>(operator new(sizeof(T) * count)); }

	template<class T = Type>
	void deallocate(T* ptr, size_t n) { operator delete(ptr); }

	template<class U>
	struct rebind { using other = DefaultAlloc<U>; };
};

// A wrapper class for the allocator wrappers on non multi-threaded
// allocators to use in multi-threaded tests
template<class Init, class Type, class Al>
class LockedAl
{
public:

	//LockedAl() :
	//	init{nullptr}
	//{}

	LockedAl(Init& init) :
		init{ &init }
	{}

	template<class U>
	LockedAl(const LockedAl<Init, U, Al>& other) noexcept : 
		init{ other.init }
	{}

	LockedAl(const LockedAl& other) noexcept : // TODO: Look into why this is needed, and why the one above can't deduce?
		init{ other.init }
	{}

	LockedAl(LockedAl&& other) noexcept :
		init{ other.init }
	{}

	template<class U>
	LockedAl(LockedAl<Init, U, Al>&& other) noexcept :
		init{ other.init }
	{}

	template<class U>
	bool operator==(const LockedAl<Init, U, Al>& other) const noexcept { return other.init == init; }

	template<class U>
	bool operator!=(const LockedAl<Init, U, Al>& other) const noexcept { return *this == other; }

	template<class Init, class U, class Al>
	friend class LockedAl;

	using STD_Compatible	= std::true_type;
	using Thread_Safe		= typename Al::Thread_Safe;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

	template<class U>
	struct rebind { using other = LockedAl<Init, U, Al>; }; 

	template<class T = Type>
	T* allocate(size_t n = 1)
	{
		if constexpr (std::is_same_v<Thread_Safe, std::false_type>)
		{
			std::lock_guard lock(mutex);
			return init->al(T{}, n);
		}
		else
			return init->al(T{}, n);
	}

	void deallocate(Type* ptr, size_t n = 1)
	{
		if constexpr (std::is_same_v<Thread_Safe, std::false_type>)
		{
			std::lock_guard lock(mutex);
			init->de(ptr, n);
		}
		else
			init->de(ptr, n);
	}

private:
	Init* init;
	mutable std::mutex mutex;
};

inline static int TestV = 0;

struct SimpleStruct
{
	SimpleStruct() = default;
	SimpleStruct(int a, int b, size_t e, size_t f) : a(a), b(b), c(a), d(b), e(e), f(f) 
	{ TestV += a + b + c + d + int(e) + int(f); }

	void meddle()
	{
		a = d + b;
		c = a + b;
		e = f + a;
		TestV += a + b + c + d + int(e) + int(f);
	}

	int a;
	int b;
	int c;
	int d;
	size_t e;
	size_t f;
};

struct PartialInit
{
	PartialInit() = default;
	PartialInit(const std::string& name) : name(name)
	{
		TestV += name[0];
	}

	// TODO: Probably make the structs meddle functions
	// less intensive to give more descrepency in benching
	void meddle()
	{
		TestV += name[1];
	}

	std::string name;
};

