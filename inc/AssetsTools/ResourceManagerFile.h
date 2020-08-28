#pragma once
#include "defines.h"

#define ASSETTYPE_RESOURCEMANAGER 147
struct ResourceManager_PPtr
{
	int fileId;
	__int64 pathId;
};
struct ResourceManager_ContainerData
{
	char *name;
	ResourceManager_PPtr ids;
};
struct ResourceManager_AssetDependencies
{
	ResourceManager_PPtr asset;
	int dependencyCount;
	ResourceManager_PPtr *dependencies;
};
class ResourceManagerFile
{
	bool isModified;
	bool isRead;
	int unityVersion;
public:
	int containerArrayLen;
	ResourceManager_ContainerData *containerArray;

	int dependenciesArrayLen;
	ResourceManager_AssetDependencies *assetDependency;
public:
	ASSETSTOOLS_API ResourceManagerFile();
	ASSETSTOOLS_API ~ResourceManagerFile();

	ASSETSTOOLS_API void Read(void *data, int dataLen, int *filePos, int assetsVersion, bool bigEndian);
	ASSETSTOOLS_API int GetFileSize();
	ASSETSTOOLS_API bool Write(void *buffer, int bufferLen, int *size);

	ASSETSTOOLS_API void SetModified();
	ASSETSTOOLS_API bool IsModified();

	ASSETSTOOLS_API int AddContainer(ResourceManager_ContainerData *cd);
	ASSETSTOOLS_API void RemoveContainer(int index);

private:
	ASSETSTOOLS_API void Clear();
};