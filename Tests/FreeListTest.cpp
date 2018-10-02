#include "stdafx.h"
#include "CppUnitTest.h"
#include "../Allocators/FreeList.h"

#include <vector>
#include <random>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{

	TEST_CLASS(FreeListTest)
	{
	public:

		using lType					= int; // Allocator allocation type for list pol
		static const lType alSize	= 512;
		using Allocator				= alloc::FreeList<int, alSize, alloc::ListPolicy>;
		using lsType				= Allocator::size_type; // Allocator size type for list pol
		using ListPol				= typename Allocator::OurPolicy::OurPolicy;
		using Header				= typename Allocator::OurPolicy::Header;

		Allocator allist;
		// This is the policy interface that interfaces with
		// different allocator policies
		Allocator::OurPolicy listItf;
		// A particular allocator policy. From here we can access
		// the list of free mem blocks, etc
		ListPol listPol;

		TEST_METHOD(AllocationList)
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

			Assert::IsTrue(static_cast<lType>(listItf.bytesFree) == alSize);
			Assert::IsTrue(failed);

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
			
			// Remaining bytes(what it should be) equal to what was allocated
			const lsType remainingBytes = alSize - static_cast<lsType>(count * (sizeof(Header) + sizeof(lType) * perAl));

			// Check byte count is correct
			Assert::IsTrue(listItf.bytesFree == remainingBytes);
			// Check remaining chunk mem size is matching our count
			Assert::IsTrue(listPol.availible.front().second == remainingBytes);

			// And test to make sure nothing is overwritten
			for (int i = 0; i < count; ++i)
				for (int j = 0; j < perAl; ++j)
					Assert::IsTrue(ptrs[i][j] == j);

			allist.freeAll();
		}

		TEST_METHOD(DeallocationList)
		{
			Assert::IsTrue(static_cast<lType>(listItf.bytesFree) == alSize);

			// Allocate close to the total amount possible
			constexpr lType perAl = 2;
			constexpr lType count = alSize / (sizeof(lType) + sizeof(Header)) / perAl;
			lType* ptrs[count];

			std::vector<int> idxs;
			for (int i = 0; i < count; ++i)
			{
				idxs.emplace_back(i);
				ptrs[i] = allist.allocate(perAl);
				for (int j = 0; j < perAl; ++j)
					ptrs[i][j] = j;
			}

			// Let's deallocate in a random order
			std::shuffle(std::begin(idxs), std::end(idxs), std::default_random_engine(1));

			for (const auto it : idxs)
				allist.deallocate(ptrs[it]);

			// Check byte count is correct
			Assert::IsTrue(listItf.bytesFree == static_cast<lsType>(alSize));

			// Should only be one large block
			Assert::IsTrue(listPol.availible.size() == 1);

			// Check remaining chunk mem size is matching our count
			Assert::IsTrue(listPol.availible.front().second == static_cast<lsType>(alSize));
		}

		TEST_METHOD(FreeAllList)
		{
			constexpr lType perAl = 2;
			constexpr lType count = alSize / (sizeof(lType) + sizeof(Header)) / perAl;
			for (int i = 0; i < count; ++i)
				allist.allocate(perAl);

			allist.freeAll();

			Assert::IsTrue(listItf.bytesFree == alSize);
			Assert::IsTrue(listPol.availible.size() == 1);
			Assert::IsTrue(listPol.availible.front().second == alSize);
		}

	};

}