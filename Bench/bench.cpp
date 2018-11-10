#include "../Allocators/Slab.h"
#include "../Allocators/FreeList.h"
#include "../Allocators/SlabMulti.h"
#include <memory>
#include "TestTypes.h"
#include "Tests.h"

// Allocators
DefaultAlloc<int> defaultAl;
alloc::SlabMem<int> slabM;
alloc::SlabObj<int> slabO;
alloc::SlabMulti<int> multi;
constexpr auto FreeListBytes = (150 + sizeof(alloc::FreeList<PartialInit, 999999999>::OurHeader)) * (maxAllocs * 2); // TODO: Better size needs prediciton
alloc::FreeList<int, FreeListBytes, alloc::ListPolicy> freeAlList;
alloc::FreeList<int, FreeListBytes, alloc::FlatPolicy> freeAlFlat;
alloc::FreeList<int, FreeListBytes, alloc::TreePolicy> freeAlTree;

enum AllocMasks
{
	DEFAULT		= 1,
	FL_LIST		= 1 << 1,
	FL_FLAT		= 1 << 2,
	FL_TREE		= 1 << 3,
	SLAB_MEM	= 1 << 4,
	SLAB_OBJ	= 1 << 5,
	SLAB_MULTI	= 1 << 6,
	ALL_ALLOCS	= (1 << 7) - 1
};

enum BenchMasks
{
	ALLOC		= 1,
	AL_DE		= 1 << 1,
	R_AL_DE		= 1 << 2,
	SEQ_READ	= 1 << 3,
	R_READ		= 1 << 4,
	TYPE_MSK	= (1 << 5) - 1,			// Mask for all Type dependent benchmarks

	STR_AL_DE	= 1 << 5,
	ALL_BENCH	= (1 << 6) - 1,
	NON_T_MSK	= ALL_BENCH ^ TYPE_MSK,	// Mask for all non-type dependent benchmarks
};

// Wrapper for common dtor/deallocation in benchmarks
template<class Al, class Ptr>
void destroyDealloc(Al& al, Ptr* ptr, size_t n)
{
	ptr->~Ptr();
	al.deallocate(ptr, n);
}

// Allocator allocate/deallocate wrappers.
// Needed so tests don't complain about compile time stuff 
// on benches where we can't use a particular allocator

// The basic wrapper for most our allocators
template<class Al>
decltype(auto) alWrapper(Al& al)
{
	auto all = [&](auto t, auto cnt) { return al.allocate<decltype(t)>(cnt); };
	auto de  = [&](auto ptr, size_t n) { destroyDealloc(al, ptr, n); };
	return std::pair(all, de);
}

template<class Xtors>
decltype(auto) sObjWrappers()
{
	// Slab obj functions 
	auto al = [&](auto t, auto cnt) { return slabO.allocate<decltype(t), Xtors>(); };
	auto de = [&](auto ptr, size_t n) { slabO.deallocate<typename std::remove_pointer<decltype(ptr)>::type, Xtors>(ptr); };
	return std::pair(al, de);
}

inline void avgScores(std::vector<double>& scores, int cnt)
{
	for (auto& s : scores)
		s /= cnt;
}

static const std::map<size_t, std::vector<size_t>> disabledTests = { {SLAB_OBJ, {STR_AL_DE}} };

inline bool isValid(size_t alMask, size_t bMask, size_t testMask)
{
	if (!(bMask & testMask))
		return false;

	auto find = disabledTests.find(alMask);
	if (find == std::end(disabledTests))
		return true;

	auto vFind = std::find(std::begin(find->second), std::end(find->second), testMask);
	if (vFind == std::end(find->second))
		return true;

	return false;
}

template<class Init, class Alloc, class Ctor>
decltype(auto) benchAlT(Init& init, Alloc& al, Ctor& ctor, bool nonType, int count, size_t alMask, size_t bMask)
{
	std::vector<double> scores(6, 0.0);

	int i;
	for (i = 0; i < count; ++i)
	{
		if (!nonType)
		{
			if (isValid(alMask, bMask, BenchMasks::ALLOC))
				scores[0] += basicAlloc(init, al);
			if (isValid(alMask, bMask, BenchMasks::AL_DE))
				scores[1] += basicAlDea(init, al);
			if (isValid(alMask, bMask, BenchMasks::R_AL_DE))
				scores[2] += randomAlDe(init, al);
			if (isValid(alMask, bMask, BenchMasks::SEQ_READ))
				scores[3] += sMemAccess(init, al);
			if (isValid(alMask, bMask, BenchMasks::R_READ))
				scores[4] += rMemAccess(init, al);
		}
		else
		{
			if (isValid(alMask, bMask, BenchMasks::STR_AL_DE))
				scores[5] += stringAl(init, al);
		}
	}
	
	avgScores(scores, i);
	return scores;
}

// Build benchmark and alloc names based on masks
inline void buildNames(const std::vector<std::string>& allNames, 
	std::vector<std::string>& maskedNames, size_t mask)
{
	size_t idx = 1;
	for (const auto& n : allNames)
	{
		if (idx & mask)
			maskedNames.emplace_back(n);
		idx <<= 1;
	}
}

template<class T>
void printScores(std::vector<std::vector<double>>& scores, size_t alMask, size_t bMask, bool isStruct = true)
{
	static constexpr int printWidth = 10;
	static const std::vector<std::string> benchNames	= { "Alloc", "Al/De", "R Al/De", "SeqRead", "RandRead", "StrAl/De" };
	static const std::vector<std::string> allocNames	= { "Default: ", "FLstList: ", "FLstFlat: ", "FLstTree: ", "SlabMem: ", "SlabObj: " };

	std::vector<std::string> bNames;
	std::vector<std::string> alNames;

	buildNames(allocNames, alNames, alMask);
	buildNames(benchNames, bNames,	 bMask);
	
	// Print the struct name, without struct
	auto* name = isStruct ? &(typeid(T).name()[7]) : &typeid(T).name()[0];
	std::cout << name << ' ' << "scores: \n";

	for (const auto& name : bNames)
	{
		if(name == bNames[0])
			std::cout << std::setw(printWidth * 2) << name << ' ';
		else
			std::cout << std::setw(printWidth) << name << ' ';
	}
	std::cout << '\n';

	int i = 0;
	for (auto& v : scores)
	{
		std::cout << std::left << std::setw(printWidth) << alNames[i];
		size_t iMask = 1;
		for (const auto& s : v)
		{
			if(iMask & bMask)
				std::cout << std::right << std::setw(printWidth) << std::fixed << std::setprecision(1) << s << ' ';
			iMask <<= 1;
		}
		std::cout << '\n';
		++i;
	}
	std::cout << '\n';
}

// TODO: If we're ever allocing simple types here: Ctor = SlabObjImpl::DefaultXtor
template<class T, class Ctor> 
decltype(auto) benchAllocs(Ctor& ctor, int runs, size_t alMask = ALL_ALLOCS, size_t bMask = ALL_BENCH)
{
	std::default_random_engine re{ 1 };
	std::vector<std::vector<double>> scores;

	constexpr bool nonType = std::is_same_v<NonType, T>;

	// Default allocator
	if (alMask & DEFAULT)
	{
		auto[Al, De] = alWrapper(defaultAl);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, defaultAl, ctor, nonType, runs, DEFAULT, bMask));
	}
	
	// FreeList: ListPolicy
	if (alMask & FL_LIST)
	{
		auto[Al, De] = alWrapper(freeAlList);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlList, ctor, nonType, runs, FL_LIST, bMask));
	}

	// FreeList: FlatPolicy
	if (alMask & FL_FLAT)
	{
		auto[Al, De] = alWrapper(freeAlFlat);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlFlat, ctor, nonType, runs, FL_FLAT, bMask));
	}

	// FreeList: TreePolicy
	if (alMask & FL_TREE)
	{
		auto[Al, De] = alWrapper(freeAlTree);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlTree, ctor, nonType, runs, FL_TREE, bMask));
	}

	// SlabMem
	if (alMask & SLAB_MEM)
	{
		auto[Al, De] = alWrapper(slabM);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, slabM, ctor, nonType, runs, SLAB_MEM, bMask));
	}

	// SlabObj
	if (alMask & SLAB_OBJ)
	{
		auto[Al, De] = sObjWrappers<Ctor>();
		BenchT init(T{}, ctor, Al, De, re, true, false);
		scores.emplace_back(benchAlT(init, slabO, ctor, nonType, runs, SLAB_OBJ, bMask));
		slabO.freeAll<T, Ctor>(); // Needed to destroy cache of objects (as it's an object pool)
	}

	if (alMask & SLAB_MULTI)
	{
		auto[Al, De] = alWrapper(multi);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, multi, ctor, nonType, runs, SLAB_MEM, bMask));
	}

	printScores<T>(scores, alMask, bMask);
	return scores;
}

inline void addScores(std::vector<std::vector<double>>& first,
	std::vector<std::vector<double>> second)
{
	for (int i = 0; i < first.size(); ++i)
		for (int j = 0; j < first[i].size(); ++j)
			first[i][j] += second[i][j];
}

// TODO: As it stands now, SlabObj and SlabMem's internal Slab's
// lists get changed and never reset after the first test, affecting future results (especially locality tests)
//
//
// Benchmark the allocators
int main()
{
	//constexpr size_t allocMask = ALL_ALLOCS;

	//constexpr size_t allocMask		= SLAB_OBJ | SLAB_MEM;	
	constexpr size_t allocMask		= AllocMasks::ALL_ALLOCS; // SLAB_MEM | SLAB_OBJ;	
	constexpr size_t benchMask		= BenchMasks::ALL_BENCH;

	constexpr int numTests			= 2;

	slabM.addCache2(1 << 5, 1 << 13, cacheSz);

	// Custom ctor for slabObj test structs
	// Note: Ctor is also used to construct the object for other allocators in tests
	alloc::CtorArgs piCtor(std::string("Init Test String"));
	alloc::CtorArgs ssCtor(1, 2, 3ULL, 4ULL);
	
	using piCtorT	= decltype(piCtor);
	using ssCtorT	= decltype(ssCtor);
	using defCtorT	= decltype(alloc::defaultXtor);

	slabO.addCache<PartialInit, piCtorT>(cacheSz, piCtor);
	slabO.addCache<SimpleStruct, ssCtorT>(cacheSz, ssCtor);


	// Run benchmarks
	std::vector<std::vector<double>> scores;
	scores			= benchAllocs<SimpleStruct,  ssCtorT>(ssCtor, numTests, allocMask, benchMask & BenchMasks::TYPE_MSK);
	addScores(scores, benchAllocs<PartialInit,	 piCtorT>(piCtor, numTests, allocMask, benchMask & BenchMasks::TYPE_MSK));

	// Print the average of all benchmark scores for 
	// the allocators
	for (auto& v : scores)
		avgScores(v, 2);

	printScores<AveragedScores> (scores, allocMask, benchMask & BenchMasks::TYPE_MSK);

	// Non type based tests
	benchAllocs<NonType, defCtorT>(alloc::defaultXtor, numTests, allocMask, benchMask & BenchMasks::NON_T_MSK);

	std::cout << "\nOptimization var: " << TestV << '\n';
	return 0;
}