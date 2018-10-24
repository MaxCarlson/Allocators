#include "../Allocators/Slab.h"
#include "../Allocators/FreeList.h"

#include <memory>
#include "TestTypes.h"
#include "Tests.h"

// Allocators
DefaultAlloc defaultAl;
alloc::SlabMem<int> slabM;
alloc::SlabObj<int> slabO;
constexpr auto FreeListBytes = (sizeof(PartialInit) + sizeof(alloc::FreeList<PartialInit, 999999999>::OurHeader)) * (maxAllocs * 2); // TODO: Better size needs prediciton
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
	ALL_ALLOCS	= (1 << 6) - 1
};

enum BenchMasks
{
	ALLOC		= 1,
	AL_DE		= 1 << 1,
	R_AL_DE		= 1 << 2,
	SEQ_READ	= 1 << 3,
	R_READ		= 1 << 4,
	ALL_BENCH	= (1 << 5) - 1
};

// Wrapper for common dtor/deallocation in benchmarks
template<class Al, class Ptr>
void destroyDealloc(Al& al, Ptr* ptr)
{
	ptr->~Ptr();
	al.deallocate(ptr);
}

// Allocator allocate/deallocate wrappers.
// Needed so tests don't complain about compile time stuff 
// on benches where we can't use a particular allocator
decltype(auto) defWrappers()
{
	auto al = [](auto t, auto cnt = 1) { return defaultAl.allocate<decltype(t)>(cnt); };
	auto de = [](auto ptr) { destroyDealloc(defaultAl, ptr); };
	return std::pair(al, de);
}

template<class Al>
decltype(auto) flWrappers(Al& al)
{
	auto all = [&](auto t, auto cnt) { return al.allocate<decltype(t)>(cnt); };
	auto de  = [&](auto ptr) { destroyDealloc(al, ptr); };
	return std::pair(all, de);
}
decltype(auto) sMemWrappers()
{
	auto al = [&](auto t, auto cnt) { return slabM.allocate<decltype(t)>(cnt); };
	auto de = [&](auto ptr) { destroyDealloc(slabM, ptr); };
	return std::pair(al, de);
}

template<class Xtors>
decltype(auto) sObjWrappers()
{
	// Slab obj functions 
	auto al = [&](auto t, auto cnt) { return slabO.allocate<decltype(t), Xtors>(); };
	auto de = [&](auto ptr) { slabO.deallocate<typename std::remove_pointer<decltype(ptr)>::type, Xtors>(ptr); };
	return std::pair(al, de);
}

inline void avgScores(std::vector<double>& scores, int cnt)
{
	for (auto& s : scores)
		s /= cnt;
}

template<class Init, class Alloc, class Ctor>
decltype(auto) benchAlT(Init& init, Alloc& al, Ctor& ctor, int count, size_t bMask)
{
	// TODO: make vec of pair<string, double> to name tests
	std::vector<double> averages(5, 0.0);

	int i;
	for (i = 0; i < count; ++i)
	{
		if(bMask & BenchMasks::ALLOC)
			averages[0] += basicAlloc(init, al);
		if(bMask & BenchMasks::AL_DE)
			averages[1] += basicAlDea(init, al);
		if(bMask & BenchMasks::R_AL_DE)
			averages[2] += randomAlDe(init, al);
		if(bMask & BenchMasks::SEQ_READ)
			averages[3] += sMemAccess(init, al);
		if(bMask & BenchMasks::R_READ)
			averages[4] += rMemAccess(init, al);
	}
	
	avgScores(averages, i);
	return averages;
}

template<class T>
void printScores(std::vector<std::vector<double>>& scores, size_t bMask, size_t tMask, bool isStruct = true)
{
	static constexpr int printWidth = 10;
	static const std::vector<std::string> benchNames	= { "Alloc", "Al/De", "R Al/De", "SeqRead", "RandRead" };
	static const std::vector<std::string> alNames		= { "Default: ", "FLstList: ", "FLstFlat: ", "FLstTree: ", "SlabMem: ", "SlabObj: " };

	std::vector<std::string> bNames;
	std::vector<std::string> alNames;

	
	// Print the struct name, without struct
	auto* name = isStruct ? &(typeid(T).name()[7]) : &typeid(T).name()[0];
	std::cout << name << ' ' << "scores: \n";

	for (const auto& name : benchNames)
	{
		if(name == benchNames[0])
			std::cout << std::setw(printWidth * 2) << name << ' ';
		else
			std::cout << std::setw(printWidth) << name << ' ';
	}
	std::cout << '\n';

	int i = 0;
	for (auto& v : scores)
	{
		std::cout << std::left << std::setw(printWidth) << alNames[i];
		for (const auto& s : v)
		{
			std::cout << std::right << std::setw(printWidth) << std::fixed << std::setprecision(1) << s << ' ';
		}
		std::cout << '\n';
		++i;
	}
	std::cout << '\n';
}

// TODO: If we're ever allocing simple types here: Ctor = SlabObjImpl::DefaultXtor
template<class T, class Ctor> 
decltype(auto) benchAllocs(Ctor& ctor, int runs, size_t testMask = ALL_ALLOCS, size_t bMask = ALL_BENCH)
{
	std::default_random_engine re{ 1 };
	std::vector<std::vector<double>> scores;

	// Default allocator
	if (testMask & DEFAULT)
	{
		auto[Al, De] = defWrappers();
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, defaultAl, ctor, runs, bMask));
	}
	
	// FreeList: ListPolicy
	if (testMask & FL_LIST)
	{
		auto[Al, De] = flWrappers(freeAlList);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlList, ctor, runs, bMask));
	}

	// FreeList: FlatPolicy
	if (testMask & FL_FLAT)
	{
		auto[Al, De] = flWrappers(freeAlFlat);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlFlat, ctor, runs, bMask));
	}

	// FreeList: TreePolicy
	if (testMask & FL_TREE)
	{
		auto[Al, De] = flWrappers(freeAlTree);
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, freeAlTree, ctor, runs, bMask));
	}

	// SlabMem
	if (testMask & SLAB_MEM)
	{
		auto[Al, De] = sMemWrappers();
		BenchT init(T{}, ctor, Al, De, re);
		scores.emplace_back(benchAlT(init, slabM, ctor, runs, bMask));
	}

	// SlabObj
	if (testMask & SLAB_OBJ)
	{
		auto[Al, De] = sObjWrappers<Ctor>();
		BenchT init(T{}, ctor, Al, De, re, true, false);
		scores.emplace_back(benchAlT(init, slabO, ctor, runs, bMask));
		slabO.freeAll<T, Ctor>(); // Needed to destroy cache of objects (as it's an object pool)
	}

	printScores<T>(scores);
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
	constexpr size_t testMask	= 0;			
	//constexpr size_t allocMask = ALL_ALLOCS;
	constexpr size_t allocMask = SLAB_MEM | SLAB_OBJ;	

	constexpr int numTests			= 5;

	// Add caches for slab allocator
	// Note: Less caches will make it faster
	for(int i = 5; i < 13; ++i)
		slabM.addCache(1 << i, cacheSz); 

	// Custom ctor for slabObj test structs
	// Note: Ctor is also used to construct the object for other allocators in tests
	alloc::CtorArgs piCtor(std::string("Init Test String"));
	alloc::CtorArgs ssCtor(1, 2, 3ULL, 4ULL);
	
	using piCtorT = decltype(piCtor);
	using ssCtorT = decltype(ssCtor);

	slabO.addCache<PartialInit, piCtorT>(cacheSz, piCtor);
	slabO.addCache<SimpleStruct, ssCtorT>(cacheSz, ssCtor);


	// Run benchmarks
	std::vector<std::vector<double>> scores;
	scores			= benchAllocs<SimpleStruct, ssCtorT>(ssCtor, numTests, allocMask, testMask);
	addScores(scores, benchAllocs<PartialInit,  piCtorT>(piCtor, numTests, allocMask, testMask));


	// Print the average of all benchmark scores for 
	// the allocators
	for (auto& v : scores)
		avgScores(v, 2);

	printScores<AveragedScores> (scores);

	std::cout << "\nOptimization var: " << TestV << '\n';
	return 0;
}