#pragma once
#include <array>
#include <string>

struct DefaultAlloc
{
	template<class T>
	T* allocateMem() { return reinterpret_cast<T*>(operator new(sizeof(T))); }

	template<class T>
	void deallocateMem(T* ptr) { operator delete(ptr); }
};

struct PartialInit
{
	PartialInit(std::string name) : name(name) 
	{
	}

	std::string name;
};