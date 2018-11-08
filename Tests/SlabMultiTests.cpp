#pragma once
#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/AllocHelpers.h"
#include "../Allocators/SlabMulti.h"


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

TEST_CLASS(SlabMultiTests)
{
public:

	alloc::SlabMulti<size_t> multi;

	TEST_CLASS_INITIALIZE(init)
	{
	}

	TEST_METHOD(Alloc_Serial)
	{
		multi.allocate(1);
	}

};

}