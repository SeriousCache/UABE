#include "AssetContainerList.h"
#include <algorithm>
#include <assert.h>


class ContainerPPtrIdx : public ContainerPPtr
{
public:
	unsigned int containerIdx;
	ContainerPPtrIdx(ResourceManager_PPtr &rsrcPPtr, unsigned int containerIdx = -1)
		: ContainerPPtr((unsigned int)rsrcPPtr.fileId, rsrcPPtr.pathId), containerIdx(containerIdx)
	{}
	ContainerPPtrIdx(PreloadData &preloadPPtr, unsigned int containerIdx = -1)
		: ContainerPPtr((unsigned int)preloadPPtr.fileId, preloadPPtr.pathId), containerIdx(containerIdx)
	{}
	ContainerPPtrIdx(unsigned int fileID, long long int pathID, unsigned int containerIdx = -1)
		: ContainerPPtr(fileID, pathID), containerIdx(containerIdx)
	{}
};
namespace std
{
    template<> struct hash<ContainerPPtrIdx>
    {
        std::size_t operator()(ContainerPPtrIdx const& pptr) const
		{
			return hash<ContainerPPtr>()(pptr);
		}
    };
}

inline void prepareContainerEntry(ContainerData &containerData, ContainerEntry &containerEntry, size_t firstDep, size_t dependenciesSize)
{
	containerEntry.name.assign(containerData.name);
	containerEntry.fileID = (unsigned int)containerData.ids.fileId;
	containerEntry.pathID = containerData.ids.pathId;

	containerEntry.firstDependencyIdx = firstDep + (unsigned int)containerData.preloadIndex;
	containerEntry.dependencyCount = (unsigned int)containerData.preloadSize;
	if (containerEntry.firstDependencyIdx >= dependenciesSize)
	{
		containerEntry.firstDependencyIdx = dependenciesSize;
		containerEntry.dependencyCount = 0;
	}
	else if (containerEntry.firstDependencyIdx + (size_t)containerEntry.dependencyCount > dependenciesSize)
	{
		containerEntry.dependencyCount = dependenciesSize - containerEntry.firstDependencyIdx;
	}
}

AssetContainerList::AssetContainerList()
	: pDependencies(new std::vector<ContainerDependencyEntry>()),
	  dependencySet(1 << 14,
		hash_ContainerDependencyLookupEntry(*pDependencies),
		equality_ContainerDependencyLookupEntry(*pDependencies))
{
}
AssetContainerList &AssetContainerList::operator=(AssetContainerList &&other)
{
	containers = std::move(other.containers);
	containerMap = std::move(other.containerMap);
	dependencyStackHistory = std::move(other.dependencyStackHistory);
	pDependencies = std::move(other.pDependencies);
	dependencySet = std::move(other.dependencySet);
	return (*this);
}
AssetContainerList::~AssetContainerList()
{
}
bool AssetContainerList::LoadFrom(AssetBundleAsset &fileTable)
{
	size_t containerStart = this->containers.size();
	if (containerStart >= (0xFFFFFFFF - fileTable.containerArrayLen - 1)) //Count the potential main asset container.
	{
		assert(false);
#ifdef _DEBUG
		//MessageBoxA(NULL, "AssetContainerList::LoadFrom : Containers overflow.", "UABE", 16);
#endif
		return false;
	}
	size_t firstDep = this->pDependencies->size();
	if (firstDep >= (0xFFFFFFFF - fileTable.preloadArrayLen))
	{
		assert(false);
#ifdef _DEBUG
		//MessageBoxA(NULL, "AssetContainerList::LoadFrom : Dependencies overflow.", "UABE", 16);
#endif
		return false;
	}
	size_t stackHistoryStart = this->dependencyStackHistory.size();
	if (stackHistoryStart >= (0xFFFFFFFF - fileTable.containerArrayLen - 1)) //Count the potential main asset container.
	{
		assert(false);
#ifdef _DEBUG
		//MessageBoxA(NULL, "AssetContainerList::LoadFrom : Stack history overflow.", "UABE", 16);
#endif
		return false;
	}

	//Insert the preload entries.
	this->pDependencies->reserve(fileTable.preloadArrayLen);
	for (unsigned int k = 0; k < fileTable.preloadArrayLen; k++)
	{
		PreloadData &prelDep = fileTable.preloadArray[k];
		this->pDependencies->push_back(ContainerDependencyEntry((unsigned int)prelDep.fileId, prelDep.pathId, (unsigned int)stackHistoryStart, 0));
		//Link the dependency into dependencySet for quick lookup.
		unsigned int curMatchIdx = this->dependencySet.insert(ContainerDependencyLookupEntry((unsigned int)(firstDep + k))).first->lastRefIdx;
		if (curMatchIdx != (firstDep + k))
		{
			while ((*this->pDependencies)[curMatchIdx].nextOccurrence != 0)
				curMatchIdx = (*this->pDependencies)[curMatchIdx].nextOccurrence;
			(*this->pDependencies)[curMatchIdx].nextOccurrence = (unsigned int)(firstDep + k);
		}
	}

	//Temporary helpers to simplify the process of correcting activeStackHistorySize in dependencies.
	std::vector<unsigned int> dependencyStackPopLocations; dependencyStackPopLocations.reserve(fileTable.containerArrayLen + 1);
	std::vector<unsigned int> dependencyStackPushLocations; dependencyStackPushLocations.reserve(fileTable.containerArrayLen + 1);

	//Insert the containers and prepare dependencyStackHistory.
	this->dependencyStackHistory.reserve(this->dependencyStackHistory.size() + fileTable.containerArrayLen + 1);
	this->containers.resize(this->containers.size() + fileTable.containerArrayLen);
	for (unsigned int i = 0; i < fileTable.containerArrayLen; i++)
	{
		ContainerData &containerData = fileTable.containerArray[i];
		size_t newIndex = containerStart + i;
		ContainerEntry &containerEntry = this->containers[newIndex];
		prepareContainerEntry(containerData, containerEntry, firstDep, this->pDependencies->size());
		if (containerEntry.dependencyCount > 0)
		{
			this->dependencyStackHistory.push_back((unsigned int)newIndex);
			dependencyStackPushLocations.push_back((unsigned int)containerEntry.firstDependencyIdx);
			dependencyStackPopLocations.push_back((unsigned int)(containerEntry.firstDependencyIdx + containerEntry.dependencyCount));
			//This loop should work, however this would result in O(N*M) performance (N : container count, M : average dependency count),
			// which would defeat the point of this data structure - N*M can be quite large in practice.
			//For this reason, two internal vectors are used to mark the push and pop locations, 
			// by which the stack depth is calculated (see below).
			//for (unsigned int k = 0; k < containerEntry.dependencyCount; k++)
			//	this->dependencies[containerEntry.firstDependencyIdx + k].activeStackHistorySize++;
		}
		unsigned int curMatchIdx = this->containerMap.insert(std::make_pair(ContainerPPtr(containerEntry.fileID, containerEntry.pathID),(unsigned int)newIndex)).first->second;
		if (curMatchIdx != newIndex)
		{
			while (this->containers[curMatchIdx].nextOccurrence != 0)
				curMatchIdx = this->containers[curMatchIdx].nextOccurrence;
			this->containers[curMatchIdx].nextOccurrence = (unsigned int)newIndex;
		}
	}
	//Insert the main asset container, if needed.
	if (fileTable.mainAsset.ids.pathId != 0)
	{
		ContainerData &containerData = fileTable.mainAsset;
		this->containers.resize(this->containers.size() + 1);
		size_t newIndex = this->containers.size() - 1;
		ContainerEntry &containerEntry = this->containers[newIndex];
		prepareContainerEntry(containerData, containerEntry, firstDep, this->pDependencies->size());
		if (containerEntry.dependencyCount > 0)
		{
			this->dependencyStackHistory.push_back((unsigned int)newIndex);
			dependencyStackPushLocations.push_back((unsigned int)containerEntry.firstDependencyIdx);
			dependencyStackPopLocations.push_back((unsigned int)(containerEntry.firstDependencyIdx + containerEntry.dependencyCount));
			//for (unsigned int k = 0; k < containerEntry.dependencyCount; k++)
			//	this->dependencies[containerEntry.firstDependencyIdx + k].activeStackHistorySize++;
		}
		unsigned int curMatchIdx = this->containerMap.insert(std::make_pair(ContainerPPtr(containerEntry.fileID, containerEntry.pathID),(unsigned int)newIndex)).first->second;
		if (curMatchIdx != newIndex)
		{
			while (this->containers[curMatchIdx].nextOccurrence != 0)
				curMatchIdx = this->containers[curMatchIdx].nextOccurrence;
			this->containers[curMatchIdx].nextOccurrence = (unsigned int)newIndex;
		}
	}

	//Sort the new elements of dependencyStackHistory by firstDependencyIdx.
    struct StackHistoryComparer {
		std::vector<ContainerEntry> &containers;
		StackHistoryComparer(std::vector<ContainerEntry> &containers)
			: containers(containers)
		{}
        inline bool operator()(unsigned int a, unsigned int b) const
        {
			return containers[a].firstDependencyIdx < containers[b].firstDependencyIdx;
        }
    };
	StackHistoryComparer stackHistoryComparer = StackHistoryComparer(this->containers);
	//Sort dependencyStackHistory, so this->containers[this->dependencyStackHistory[i]].firstDependencyIdx is sorted ascendingly.
	std::sort(this->dependencyStackHistory.begin() + stackHistoryStart, this->dependencyStackHistory.end(), stackHistoryComparer);
	std::sort(dependencyStackPopLocations.begin(), dependencyStackPopLocations.end());
	std::sort(dependencyStackPushLocations.begin(), dependencyStackPushLocations.end());

	//Fix the dependency stack history indices in dependencies.
	size_t stackRefUpdate_UpperBound = this->pDependencies->size();
	for (size_t _i = this->dependencyStackHistory.size(); _i > stackHistoryStart; _i--)
	{
		size_t i = _i - 1;
		size_t firstDependencyIdx = this->containers[this->dependencyStackHistory[i]].firstDependencyIdx;
		for (size_t k = firstDependencyIdx; k < stackRefUpdate_UpperBound; k++)
		{
			(*this->pDependencies)[k].activeStackHistory = (unsigned int)(i);
		}
		stackRefUpdate_UpperBound = firstDependencyIdx;
	}
	
	bool shownBugMessage = false;
	//Fix the stack depths in dependencies.
	if (dependencyStackPushLocations.size() > 0)
	{
		size_t start = dependencyStackPushLocations[0];
		unsigned int curDepth = 1;
		size_t pushIdx = 1;
		size_t popIdx = 0;
		for (size_t i = start; i < this->pDependencies->size(); i++)
		{
			while (pushIdx < dependencyStackPushLocations.size() && i >= dependencyStackPushLocations[pushIdx])
			{
				curDepth++;
				pushIdx++;
			}
			while (popIdx < dependencyStackPopLocations.size() && i >= dependencyStackPopLocations[popIdx])
			{
				if (curDepth == 0)
				{
					if (!shownBugMessage)
					{
						assert(false);
#ifdef _DEBUG
						//MessageBox(NULL, TEXT("An error occured walking the container dependency stack."), TEXT("UABE"), 16); 
#endif
					}
					shownBugMessage = true;
				}
				else
					curDepth--;
				popIdx++;
			}
			(*this->pDependencies)[i].activeStackHistorySize = curDepth;
		}
	}
	return true;
}
bool AssetContainerList::LoadFrom(ResourceManagerFile &resourceFile)
{
	size_t containerStart = this->containers.size();
	std::unordered_set<ContainerPPtrIdx> containerSetTemp;
	//Copy containers.
	unsigned int containerArrayLen = resourceFile.containers.size();
	if (containerArrayLen > 0x7FFFFFFF) containerArrayLen = 0x7FFFFFFF;
	this->containers.resize(this->containers.size() + containerArrayLen);
	for (unsigned int i = 0; i < containerArrayLen; i++)
	{
		this->containers[containerStart + i].name.assign(resourceFile.containers[i].name);
		this->containers[containerStart + i].dependencyCount = 0;
		this->containers[containerStart + i].firstDependencyIdx = 0;
		ContainerPPtrIdx pptr(resourceFile.containers[i].ids, i);
		this->containers[containerStart + i].fileID = pptr.fileID;
		this->containers[containerStart + i].pathID = pptr.pathID;
		containerSetTemp.insert(pptr);

		unsigned int curMatchIdx = this->containerMap.insert(std::make_pair(ContainerPPtr(pptr.fileID, pptr.pathID),(unsigned int)(containerStart + i))).first->second;
		if (curMatchIdx != (containerStart + i))
		{
			while (this->containers[curMatchIdx].nextOccurrence != 0)
				curMatchIdx = this->containers[curMatchIdx].nextOccurrence;
			this->containers[curMatchIdx].nextOccurrence = (unsigned int)(containerStart + i);
		}
	}
	//Copy dependencies.
	unsigned int dependenciesArrayLen = resourceFile.dependencyLists.size();
	if (dependenciesArrayLen > 0x7FFFFFFF) dependenciesArrayLen = 0x7FFFFFFF;
	for (unsigned int i = 0; i < dependenciesArrayLen; i++)
	{
		ResourceManager_AssetDependencies &rsrcDepList = resourceFile.dependencyLists[i];
		unsigned int nextContainerIdx = (unsigned int)(this->containers.size() - containerStart);
		const ContainerPPtrIdx &pptr = *containerSetTemp.insert(ContainerPPtrIdx(rsrcDepList.asset, nextContainerIdx)).first;
		size_t containerIdx = containerStart + pptr.containerIdx;
		if (pptr.containerIdx == nextContainerIdx) //New item, treat like a container with an empty name.
		{
			this->containers.resize(this->containers.size() + 1);
			this->containers[containerIdx].fileID = pptr.fileID;
			this->containers[containerIdx].pathID = pptr.pathID;
		}
		if (this->containers[containerIdx].dependencyCount != 0)
		{
			//We are doomed.
			//This means that there are multiple dependency lists for the same PPtr.
			assert(false);
#ifdef _DEBUG
			//MessageBoxA(NULL, "AssetContainerList::LoadFrom : ResourceManager asset has multiple dependency lists for the same asset.", "UABE", 16);
			//This break intentionally is for debug mode only. There may be a use to continue if the following dependency lists are for new PPtrs.
			break;
#endif
			//Otherwise : Silent ignore.
		}
		else if (containerIdx > 0xFFFFFFFEU || rsrcDepList.dependencies.size() > 0xFFFFFFFEU && this->pDependencies->size() > (0xFFFFFFFEU - rsrcDepList.dependencies.size()))
		{
			//This is strange.
			//If there were this many containers and/or dependencies, they would take at least ~100GiB.
			assert(false);
#ifdef _DEBUG
			//MessageBoxA(NULL, "AssetContainerList::LoadFrom : Containers or dependencies overflow.", "UABE", 16);
#endif
			break;
		}
		else if (this->dependencyStackHistory.size() > 0xFFFFFFFFU)
		{
			//Should not happen after the previous checks.
			assert(false);
#ifdef _DEBUG
			//MessageBoxA(NULL, "AssetContainerList::LoadFrom : Stack history overflow.", "UABE", 16);
#endif
			break;
		}
		else if (rsrcDepList.dependencies.size() > 0)
		{
			this->containers[containerIdx].firstDependencyIdx = this->pDependencies->size();
			this->containers[containerIdx].dependencyCount = rsrcDepList.dependencies.size();
			this->dependencyStackHistory.push_back((unsigned int)containerIdx);
			unsigned int stackHistorySize = (unsigned int)this->dependencyStackHistory.size();

			size_t firstDep = this->pDependencies->size();
			this->pDependencies->reserve(rsrcDepList.dependencies.size());
			for (unsigned int k = 0; k < rsrcDepList.dependencies.size(); k++)
			{
				ResourceManager_PPtr &rsrcDep = rsrcDepList.dependencies[k];
				//Due to the way ResourceManager assets are structured, we have no overlapping dependency list parts.
				this->pDependencies->push_back(ContainerDependencyEntry((unsigned int)rsrcDep.fileId, rsrcDep.pathId, stackHistorySize - 1, 1));
				//Link the dependency into dependencySet for quick lookup.
				unsigned int curMatchIdx = this->dependencySet.insert(ContainerDependencyLookupEntry((unsigned int)(firstDep + k))).first->lastRefIdx;
				if (curMatchIdx != (firstDep + k))
				{
					while ((*this->pDependencies)[curMatchIdx].nextOccurrence != 0)
						curMatchIdx = (*this->pDependencies)[curMatchIdx].nextOccurrence;
					(*this->pDependencies)[curMatchIdx].nextOccurrence = (unsigned int)(firstDep + k);
				}
			}
		}
	}
	return true;
}

std::vector<const ContainerEntry*> AssetContainerList::getContainers(unsigned int fileID, long long pathID) const
{
	std::vector<const ContainerEntry*> ret;
	auto result = containerMap.find(ContainerPPtr(fileID, pathID));
	if (result != containerMap.end())
	{
		unsigned int entryIdx = result->second;
		do {
			const ContainerEntry &entry = this->containers[entryIdx];
			ret.push_back(&entry);
			entryIdx = entry.nextOccurrence;
		} while (entryIdx != 0);
	}
	return ret;
}

void AssetContainerList::getContainersFor(std::vector<const ContainerEntry*> &ret, unsigned int dependencyIdx) const
{
	unsigned int remStackDepth = (*this->pDependencies)[dependencyIdx].activeStackHistorySize;
	for (unsigned int i = (*this->pDependencies)[dependencyIdx].activeStackHistory; i != (unsigned int)-1 && remStackDepth > 0; i--)
	{
		const ContainerEntry &entry = this->containers[this->dependencyStackHistory[i]];
		if (entry.firstDependencyIdx + entry.dependencyCount >= dependencyIdx)
		{
			ret.push_back(&entry);
			remStackDepth--;
		}
	}
	assert(remStackDepth == 0);
}

static thread_local unsigned int curVirtualContainerLookupPlaceholder_fileID;
static thread_local long long curVirtualContainerLookupPlaceholder_pathID;

std::vector<const ContainerEntry*> AssetContainerList::getParentContainers(unsigned int fileID, long long pathID) const
{
	std::size_t targetHash = std::hash<ContainerDependencyEntry>()(ContainerDependencyEntry(fileID, pathID, 0, 0));
	std::vector<const ContainerEntry*> ret;

	//Set the thread local placeholder PPtr for the hash and equality functions to use.
	// -> We aren't allowed (and shouldn't) insert anything into dependencySet in this function,
	//       so we define UINT_MAX as a placeholder index
	//       that the hasher and equality pred of dependencySet translate to curVirtualContainerLookupPlaceholder.
	curVirtualContainerLookupPlaceholder_fileID = fileID;
	curVirtualContainerLookupPlaceholder_pathID = pathID;

	auto dependencySetIt = dependencySet.find(ContainerDependencyLookupEntry(UINT_MAX));

	if (dependencySetIt != dependencySet.end())
	{
		unsigned int curMatchIdx = dependencySetIt->lastRefIdx;
		const ContainerDependencyEntry &entry = (*this->pDependencies)[dependencySetIt->lastRefIdx];
		assert(entry.fileID == fileID && entry.pathID == pathID);
		if (entry.fileID == fileID && entry.pathID == pathID)
		{
			getContainersFor(ret, curMatchIdx);
			while ((curMatchIdx = (*this->pDependencies)[curMatchIdx].nextOccurrence) != 0)
				getContainersFor(ret, curMatchIdx);
		}
	}
	return ret;
}

namespace std
{
    std::size_t hash<ContainerPPtr>::operator()(ContainerPPtr const& pptr) const
    {
		static std::hash<unsigned int> uintHash;
		static std::hash<long long int> llHash;
		//static std::hash<size_t> sizetHash;
        size_t fidHash = uintHash(pptr.fileID);
		size_t pidHash = llHash(pptr.pathID);
        return (fidHash << 1) ^ pidHash; //sizetHash((fidHash << 1) ^ pidHash);
    }
    std::size_t hash<ContainerDependencyEntry>::operator()(ContainerDependencyEntry const& pptr) const
    {
		static std::hash<unsigned int> uintHash;
		static std::hash<long long int> llHash;
		//static std::hash<size_t> sizetHash;
        size_t fidHash = uintHash(pptr.fileID);
		size_t pidHash = llHash(pptr.pathID);
        return (fidHash << 1) ^ pidHash; //sizetHash((fidHash << 1) ^ pidHash);
    }
}

hash_ContainerDependencyLookupEntry::hash_ContainerDependencyLookupEntry(std::vector<ContainerDependencyEntry> &dependencies)
		: pDependencies(&dependencies)
{}
std::size_t hash_ContainerDependencyLookupEntry::operator()(ContainerDependencyLookupEntry const& entry) const
{
	if (entry.lastRefIdx == UINT_MAX)
		return std::hash<ContainerPPtr>()(ContainerPPtr(curVirtualContainerLookupPlaceholder_fileID, curVirtualContainerLookupPlaceholder_pathID));
	return entryHash((*pDependencies)[entry.lastRefIdx]);
}

equality_ContainerDependencyLookupEntry::equality_ContainerDependencyLookupEntry(std::vector<ContainerDependencyEntry> &dependencies)
		: pDependencies(&dependencies)
{}
bool equality_ContainerDependencyLookupEntry::operator()(ContainerDependencyLookupEntry const& entryrefA, ContainerDependencyLookupEntry const& entryrefB) const
{
	ContainerPPtr pptrA = ContainerPPtr(curVirtualContainerLookupPlaceholder_fileID, curVirtualContainerLookupPlaceholder_pathID);
	if (entryrefA.lastRefIdx != UINT_MAX)
	{
		ContainerDependencyEntry const &entryA = (*pDependencies)[entryrefA.lastRefIdx];
		pptrA.fileID = entryA.fileID;
		pptrA.pathID = entryA.pathID;
	}
	ContainerPPtr pptrB = ContainerPPtr(curVirtualContainerLookupPlaceholder_fileID, curVirtualContainerLookupPlaceholder_pathID);
	if (entryrefB.lastRefIdx != UINT_MAX)
	{
		ContainerDependencyEntry const &entryB = (*pDependencies)[entryrefB.lastRefIdx];
		pptrB.fileID = entryB.fileID;
		pptrB.pathID = entryB.pathID;
	}
	return (pptrA.fileID == pptrB.fileID && pptrA.pathID == pptrB.pathID);
}
