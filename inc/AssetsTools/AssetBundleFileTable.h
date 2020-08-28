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

	DWORD preloadArrayLen;
	PreloadData *preloadArray;

	DWORD containerArrayLen;
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
	
	ASSETSTOOLS_API bool ReadBundleFile(void *data, int dataLen, int *filePos, AssetTypeTemplateField *pBaseField, bool bigEndian);
	ASSETSTOOLS_API void ReadBundleFile(void *data, int dataLen, int *filePos, int assetsVersion, bool bigEndian);
	ASSETSTOOLS_API void FlushChanges();
	ASSETSTOOLS_API int GetFileSize();
	ASSETSTOOLS_API bool WriteBundleFile(void *buffer, int bufferLen, int *size, bool bigEndian);

	ASSETSTOOLS_API void SetModified();
	ASSETSTOOLS_API bool IsModified();

	ASSETSTOOLS_API int AddContainer(ContainerData *cd);
	ASSETSTOOLS_API void UpdatePreloadArray(DWORD containerIndex);
	ASSETSTOOLS_API void RemoveContainer(DWORD index);


private:
	ASSETSTOOLS_API void Clear();
};