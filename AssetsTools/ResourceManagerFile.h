#pragma once
#include "defines.h"
#include "AssetTypeClass.h"
#include <string>
#include <vector>

#define ASSETTYPE_RESOURCEMANAGER 147
struct ResourceManager_PPtr
{
	int fileId = 0;
	__int64 pathId = 0;
};
struct ResourceManager_ContainerData
{
	std::string name;
	ResourceManager_PPtr ids;
};
struct ResourceManager_AssetDependencies
{
	ResourceManager_PPtr asset;
	std::vector<ResourceManager_PPtr> dependencies;
};
class ResourceManagerFile
{
	bool isModified;
	bool isRead;
	int unityVersion;
public:
	std::vector<ResourceManager_ContainerData> containers;
	
	std::vector<ResourceManager_AssetDependencies> dependencyLists;
public:
	ASSETSTOOLS_API ResourceManagerFile();
	ASSETSTOOLS_API ~ResourceManagerFile();
	
	ASSETSTOOLS_API bool Read(AssetTypeValueField *pBase);
	ASSETSTOOLS_API void Read(void *data, size_t dataLen, size_t *filePos, int assetsVersion, bool bigEndian);
	ASSETSTOOLS_API size_t GetFileSize();
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, size_t *size = nullptr);
	

	inline void SetModified()
	{
		isModified = true;
	}
	inline bool IsModified() const
	{
		return isModified;
	}
	inline bool IsRead() const
	{
		return isRead;
	}

	inline void AddContainer(ResourceManager_ContainerData &&cd)
	{
		containers.emplace_back(cd);
	}
	inline void AddContainer(ResourceManager_ContainerData &cd)
	{
		containers.push_back(cd);
	}
	inline void RemoveContainer(size_t index)
	{
		containers.erase(containers.begin() + index);
	}

private:
	ASSETSTOOLS_API void Clear();
};