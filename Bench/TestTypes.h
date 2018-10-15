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

struct PartialInit
{
	PartialInit(const std::string& name) : name(name) 
	{
	}

	std::string name;
};