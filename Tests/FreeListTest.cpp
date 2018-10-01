#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/FreeList.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

	TEST_CLASS(FreeListTest)
	{
	public:


		using lType	= int;
		static const lType alSize = 512;

		using Allocator = alloc::FreeList<int, alSize, alloc::ListPolicy>;
		Allocator allist;

		// Policy is similar to an allocator in that
		// it has all statically defined variables (so this is identical to
		// the policy inside our above allocator)
		Allocator::OurPolicy policy;

		using ListPol = typename Allocator::OurPolicy::OurPolicy;
		ListPol listPol;


		using Header = typename Allocator::OurPolicy::Header;

		TEST_METHOD(Allocation)
		{
			// Should be able to allocate full size, 
			// we need to store header info
			bool failed = false;
			try
			{
				lType* p = allist.allocate(alSize);
			}
			catch (std::bad_alloc b)
			{
				failed = true;
			}

			Assert::AreEqual(static_cast<lType>(policy.bytesFree), alSize);
			Assert::AreEqual(failed, true);

			// Allocate close to the total amount possible
			constexpr lType perAl = 2;
			constexpr lType count = alSize / (sizeof(lType) + sizeof(Header)) / perAl;
			lType* ptrs[count];
			
			for (int i = 0; i < count; ++i)
			{
				ptrs[i] = allist.allocate(perAl);
				for (int j = 0; j < perAl; ++j)
					ptrs[i][j] = j;
			}
			
			// Remaining bytes equal to what was allocated
			const lType remainingBytes = alSize - static_cast<lType>(count * (sizeof(Header) + sizeof(lType) * perAl));

			// Check byte count is correct
			Assert::AreEqual(static_cast<lType>(policy.bytesFree), remainingBytes);
			// Check counter in only list item is matching
			Assert::AreEqual(static_cast<lType>(listPol.availible.front().second), remainingBytes);

			// And test to make sure nothing is overwritten
			for (int i = 0; i < count; ++i)
				for (int j = 0; j < perAl; ++j)
					Assert::AreEqual(ptrs[i][j], j);

			allist.freeAll();
		}

		TEST_METHOD(Deallocation)
		{
			Assert::AreEqual(static_cast<lType>(policy.bytesFree), alSize);

		}
	};

}