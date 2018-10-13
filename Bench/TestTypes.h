#pragma once
#include <array>
#include <string>


struct PartialInit
{
	PartialInit(std::string name) : name(name) 
	{
	}

	std::string name;
};