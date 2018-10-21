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
alloc::FreeList<int, FreeListBytes> freeAl;

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

decltype(auto) flWrappers()
{
	auto al = [&](auto t, auto cnt) { return freeAl.allocate<decltype(t)>(cnt); };
	auto de = [&](auto ptr) { destroyDealloc(freeAl, ptr); };
	return std::pair(al, de);
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
decltype(auto) benchAlT(Init& init, Alloc& al, Ctor& ctor, int count)
{
	// TODO: make vec of pair<string, double> to name tests
	std::vector<double> averages(5, 0.0);

	int i;
	for (i = 0; i < count; ++i)
	{
		averages[0] += basicAlloc(init, al);
		averages[1] += basicAlDea(init, al);
		averages[2] += randomAlDe(init, al);
		averages[3] += sMemAccess(init, al);
		averages[4] += rMemAccess(init, al);
	}
	
	avgScores(averages, i);
	return averages;
}

// TODO: Label scores
template<class T>
void printScores(std::vector<std::vector<double>>& scores, bool isStruct = true)
{
	static constexpr int printWidth = 10;
	static const std::vector<std::string> benchNames	= { "Alloc", "Al/De", "R Al/De", "SeqRead", "RandRead" };
	static const std::vector<std::string> alNames		= { "Default: ", "FreeList: ", "SlabMem: ", "SlabObj: " };
	
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
decltype(auto) benchAllocs(Ctor& ctor, int runs)
{
	std::default_random_engine re{ 1 };
	std::vector<std::vector<double>> scores;

	auto[dAl, dDe] = defWrappers();
	BenchT initD(T{}, ctor, dAl, dDe, re);
	scores.emplace_back(benchAlT(initD, defaultAl, ctor, runs));

	auto[fAl, fDe] = flWrappers();
	BenchT initF(T{}, ctor, fAl, fDe, re);
	scores.emplace_back(benchAlT(initF, freeAl, ctor, runs));

	auto[memAl, memDe] = sMemWrappers();
	BenchT initM(T{}, ctor, memAl, memDe, re);
	scores.emplace_back(benchAlT(initM, slabM, ctor, runs));

	auto[objAl, objDe] = sObjWrappers<Ctor>();
	BenchT initO(T{}, ctor, objAl, objDe, re, true, false);
	scores.emplace_back(benchAlT(initO, slabO, ctor, runs));
	slabO.freeAll<T, Ctor>(); // Needed to destroy cache of objects (as it's an object pool)

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

// Benchmark the allocators
int main()
{
	constexpr int numTests = 10;

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
	scores			= benchAllocs<SimpleStruct, ssCtorT>(ssCtor, numTests);
	addScores(scores, benchAllocs<PartialInit,  piCtorT>(piCtor, numTests));


	// Print the average of all benchmark scores for 
	// the allocators
	for (auto& v : scores)
		avgScores(v, 2);
	printScores<AveragedScores>(scores);

	std::cout << "\nOptimization var: " << TestV << '\n';
	return 0;
}