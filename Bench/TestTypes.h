#pragma once
#include <array>
#include <string>

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

	using STD_Compatible = std::true_type;

	using size_type			= size_t;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Type*;
	using const_pointer		= const pointer;
	using reference			= Type&;
	using const_reference	= const reference;
	using value_type		= Type;

	template<class T = Type>
	T* allocate() { return reinterpret_cast<T*>(operator new(sizeof(Type))); }

	template<class T = Type>
	T* allocate(size_t count) { return reinterpret_cast<T*>(operator new(sizeof(T) * count)); }

	template<class T = Type>
	void deallocate(T* ptr, size_t n) 
	{
		operator delete(ptr);
	}

	template<class U>
	struct rebind { using other = DefaultAlloc<U>; };
};

inline static int TestV = 0;

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

struct SimpleStruct
{
	SimpleStruct() = default;
	SimpleStruct(int a, int b, size_t e, size_t f) : a(a), b(b), c(a), d(b), e(e), f(f) { TestV += a + b + c + d + e + f; }

	void meddle()
	{
		a = d + b;
		c = a + b;
		e = f + a;
		TestV += a + b + c + d + e + f;
	}

	int a;
	int b;
	int c;
	int d;
	size_t e;
	size_t f;
};

struct FullInit
{
	FullInit() = default;
	FullInit(int count) : count(count)
	{
		others = new FullInit[count];
	}

	~FullInit()
	{
		delete[] others;
	}

	int count;
	FullInit* others;
};
