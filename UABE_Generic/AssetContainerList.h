#pragma once
#include "api.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "AssetBundleFileTable.h"
#include "ResourceManagerFile.h"

#if (defined(_MSC_VER) && _MSC_VER<=1600)
#define thread_local __declspec(thread)
#endif

//Somewhat complex way to store the container and dependency data.
//Meant to allow quick access to a) the list of dependencies of a container
// and b) all parent containers of any asset without having O(n^2) worst case memory usage for the dependency reverse list (n : amount of containers).
class ContainerDependencyLookupEntry
{
public:
	inline ContainerDependencyLookupEntry(unsigned int lastRefIdx)
		: lastRefIdx(lastRefIdx)
	{}
	unsigned int lastRefIdx; //Index into a AssetContainerList::dependencies vector. Placeholder: UINT_MAX
	inline bool operator==(const ContainerDependencyLookupEntry &other) const
	{
		return lastRefIdx == other.lastRefIdx;
	}
};
class ContainerDependencyEntry
{
public:
	inline ContainerDependencyEntry(unsigned int fileID, long long int pathID, unsigned int activeStackHistory, unsigned int activeStackHistorySize)
		: fileID(fileID), pathID(pathID), activeStackHistory(activeStackHistory), activeStackHistorySize(activeStackHistorySize), nextOccurrence(0)
	{}
	//Size of the stack history that applies to this entry
	// (i.e. the last history index where the dependency entry index is >= container.firstDependencyIdx).
	unsigned int activeStackHistory;
	unsigned int activeStackHistorySize; //Dependency stack depth at this point. Limits the amount of backtracking on the stack history.
	unsigned int nextOccurrence; //Index of the next entry with the same fileID/pathID, or 0.
	unsigned int fileID;
	long long int pathID;
};
class ContainerEntry
{
public:
	inline ContainerEntry()
		: firstDependencyIdx(0), dependencyCount(0), nextOccurrence(0), fileID(0), pathID(0)
	{}
	std::string name; //Set to "" if this entry does not actually have a name.
	size_t firstDependencyIdx;
	unsigned int dependencyCount;
	unsigned int nextOccurrence;
	unsigned int fileID;
	long long int pathID;
};
class ContainerPPtr
{
public:
	inline ContainerPPtr() : fileID(0), pathID(0) {}
	inline ContainerPPtr(unsigned int fileID, long long int pathID)
		: fileID(fileID), pathID(pathID)
	{}
	unsigned int fileID;
	long long int pathID;
	inline bool operator==(const ContainerPPtr &other) const
	{
		return fileID == other.fileID && pathID == other.pathID;
	}
};
class AssetContainerList;
namespace std
{
    template<> struct hash<ContainerDependencyEntry>
    {
		UABE_Generic_API std::size_t operator()(ContainerDependencyEntry const& pptr) const;
    };
    template<> struct hash<ContainerPPtr>
    {
		UABE_Generic_API std::size_t operator()(ContainerPPtr const& pptr) const;
    };
}
//ContainerDependencyLookupEntry are intended as proxies to a ContainerDependencyEntry.
// Hash and equality for the unordered_set in AssetContainerList are defined over the fileID&pathID of the corresponding entry.
struct equality_ContainerDependencyLookupEntry
{
	std::vector<ContainerDependencyEntry> *pDependencies;
	std::hash<ContainerDependencyEntry> entryHash;
	UABE_Generic_API equality_ContainerDependencyLookupEntry(std::vector<ContainerDependencyEntry> &dependencies);

	UABE_Generic_API bool operator()(ContainerDependencyLookupEntry const& pptrref_a, ContainerDependencyLookupEntry const& pptrref_b) const;

};
struct hash_ContainerDependencyLookupEntry
{
	std::vector<ContainerDependencyEntry> *pDependencies;
	std::hash<ContainerDependencyEntry> entryHash;
	UABE_Generic_API hash_ContainerDependencyLookupEntry(std::vector<ContainerDependencyEntry> &dependencies);

	UABE_Generic_API std::size_t operator()(ContainerDependencyLookupEntry const& pptr) const;
};
//Not thread safe! Make sure that if one thread calls a non-const method, nothing else is called in parallel.
class AssetContainerList
{

	std::vector<ContainerEntry> containers;
	std::unordered_map<ContainerPPtr, unsigned int> containerMap;
	//Indices into the containers vector, sorted by containers.firstDependencyIdx.
	//Each entry stands for a 'push', while 'pop' is implicit at the dependency index : container.firstDependencyIdx+container.dependencyCount.
	std::vector<unsigned int> dependencyStackHistory;
	std::unique_ptr<std::vector<ContainerDependencyEntry>> pDependencies;

	//Hash set containing indices into the vector dependencies, using the fileID and pathID members of the corresponding entry.
	//This contains one entry per fileID/pathID pair. Since the pairs can occur multiple times, use dependency.nextOccurence to look for more matches.
	std::unordered_set<ContainerDependencyLookupEntry, hash_ContainerDependencyLookupEntry, equality_ContainerDependencyLookupEntry> dependencySet;

private:
	void getContainersFor(std::vector<const ContainerEntry*> &ret, unsigned int dependencyIdx) const;
public:
	UABE_Generic_API AssetContainerList();
	UABE_Generic_API AssetContainerList &operator=(AssetContainerList &&other);
	UABE_Generic_API ~AssetContainerList();
	inline void Clear()
	{
		containers.clear();
		containerMap.clear();
		dependencyStackHistory.clear();
		pDependencies->clear();
		dependencySet.clear();
	}
	UABE_Generic_API bool LoadFrom(AssetBundleAsset &fileTable);
	UABE_Generic_API bool LoadFrom(ResourceManagerFile &resourceFile);
	inline const ContainerEntry* getContainers() const
	{
		return containers.data();
	}
	inline size_t getContainerCount() const
	{
		return containers.size();
	}
	UABE_Generic_API std::vector<const ContainerEntry*> getContainers(unsigned int fileID, long long pathID) const;
	UABE_Generic_API std::vector<const ContainerEntry*> getParentContainers(unsigned int fileID, long long pathID) const;
};