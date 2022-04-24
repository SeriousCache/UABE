#pragma once
#include "defines.h"
#include "AssetTypeClass.h"

#define ASSETTYPE_ASSETBUNDLE 0x8E
struct PreloadData
{
	int fileId;
	__int64 pathId;
};
struct ContainerData
{
	char *name;

	int preloadIndex;
	int preloadSize;
	PreloadData ids;
};
struct ScriptCompatibilityData
{
	char *className;
	char *namespaceName;
	char *assemblyName;

	unsigned int hash;
};
struct ClassCompatibilityData
{
	int first;
	unsigned int second;
};
class AssetBundleAsset
{
	bool isModified;
	bool isRead;
	int unityVersion;
	AssetTypeInstance *pAssetType;
	//AssetTypeValueField *pBaseValueField;
public:
	char *name;

	uint32_t preloadArrayLen;
	PreloadData *preloadArray;

	uint32_t containerArrayLen;
	ContainerData *containerArray;
	ContainerData mainAsset; //this has no name field

	int scriptCompatibilityArrayLen; //before Unity 5
	ScriptCompatibilityData *scriptCompatibilityArray; //before Unity 5
	int classCompatibilityArrayLen; //before Unity 5
	ClassCompatibilityData *classCompatibilityArray; //before Unity 5

	unsigned int runtimeCompatibility;

	char *assetBundleName; //Unity 5

	int dependenciesArrayLen;
	char **dependencies; //Unity 5

	bool isStreamedSceneAssetBundle; //Unity 5

public:
	ASSETSTOOLS_API AssetBundleAsset();
	ASSETSTOOLS_API ~AssetBundleAsset();
	
	ASSETSTOOLS_API bool ReadBundleFile(void *data, size_t dataLen, size_t *filePos, AssetTypeTemplateField *pBaseField, bool bigEndian);
	ASSETSTOOLS_API void ReadBundleFile(void *data, size_t dataLen, size_t *filePos, int assetsVersion, bool bigEndian);
	ASSETSTOOLS_API void FlushChanges();
	ASSETSTOOLS_API int GetFileSize();
	ASSETSTOOLS_API bool WriteBundleFile(void *buffer, size_t bufferLen, size_t *size, bool bigEndian);

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

	ASSETSTOOLS_API int AddContainer(ContainerData *cd);
	ASSETSTOOLS_API void UpdatePreloadArray(uint32_t containerIndex);
	ASSETSTOOLS_API void RemoveContainer(uint32_t index);


private:
	ASSETSTOOLS_API void Clear();
};