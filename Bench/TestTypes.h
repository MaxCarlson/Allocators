#pragma once
#include <array>
#include <string>

struct DefaultAlloc
{
	template<class T>
	T* allocate() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T>
	void deallocate(T* ptr) { operator delete(ptr); }
};

inline static int TestV = 0;

struct PartialInit
{
	PartialInit(const std::string& name) : name(name) 
	{
		TestV += name[0];
	}

	std::string name;
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
