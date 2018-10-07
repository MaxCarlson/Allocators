#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/Linear.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{		
	TEST_CLASS(LinearTest)
	{
	public:
		alloc::Linear<int, sizeof(int) * 4> al;
		
		TEST_METHOD(Allocation)
		{
			al.reset();
			al = alloc::Linear<int, sizeof(int) * 4>();

			auto* data = al.allocate(2);
			data[0] = 1;
			data[1] = 2;
			auto* data1 = al.allocate(2);
			data1[0] = 3;
			data1[1] = 4;

			for(int i = 1; i < 5; ++i)
				Assert::AreEqual(data[i-1], i);
		}

		// This relys on reset and the constructor working properly
		TEST_METHOD(NoSpace)
		{
			al.reset();
			al = alloc::Linear<int, sizeof(int) * 4>();

			auto* data = al.allocate(4);
			bool works = false;

			try
			{
				al.allocate(1);
			}
			catch (const std::bad_alloc&)
			{
				works = true;
			}

			Assert::AreEqual(true, works);
		}

		TEST_METHOD(Reset)
		{
			al.reset();
			al = alloc::Linear<int, sizeof(int) * 4>();

			// TODO:
		}

		TEST_METHOD(Operators)
		{
			alloc::Linear<int, sizeof(int) * 4> ai4;
			alloc::Linear<char, sizeof(int) * 4> ac4;
			alloc::Linear<int, sizeof(int) * 8> ai8;

			bool sizeTypeEqulity = ai4 == ac4;
			bool sizesUnequal = ai4 == ai8;

			Assert::AreEqual(sizeTypeEqulity, true);
			Assert::AreEqual(sizesUnequal, false);

			sizeTypeEqulity = ai4 != ac4;
			sizesUnequal = ai4 != ai8;

			Assert::AreEqual(sizeTypeEqulity, false);
			Assert::AreEqual(sizesUnequal, true);
		}
	};
}