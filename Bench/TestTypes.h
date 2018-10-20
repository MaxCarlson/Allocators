#pragma once
#include <array>
#include <string>

struct DefaultAlloc
{
	template<class T>
	T* allocate() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T>
	T* allocate(size_t count) { return reinterpret_cast<T*>(operator new(sizeof(T) * count)); }

	template<class T>
	void deallocate(T* ptr) 
	{
		ptr->~T();
		operator delete(ptr); 
	}
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
		name[1] += name[3];
		TestV += name[0] + name[1] + name[2] + name[3];
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
		b = c + c;
		c = a + b;
		d = e + f;
		e = f + a;
		f = a + e;
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
