#pragma once
#include "AssetsFileFormat.h"
#include <set>
#include <assert.h>

#include "defines.h"

class AssetFileInfoEx : public AssetFileInfo
{
	public:
		//AssetsHeader format < 0x10 : equals curFileTypeOrIndex
		//AssetsHeader format >= 0x10 : equals TypeTree.pTypes_Unity5[curFileTypeOrIndex].classId or (uint32_t)INT_MIN if the index is out of bounds
		uint32_t curFileType;
		QWORD absolutePos;

		//If the file type id is known to have a name field, it reads up to 100 characters (including the null-terminator) to "out".
		ASSETSTOOLS_API bool ReadName(AssetsFile *pFile, std::string &out, IAssetsReader *pReaderView = nullptr);
};

class AssetFileInfoEx_KeyRef
{
public:
	AssetFileInfoEx *pInfo;
	inline AssetFileInfoEx_KeyRef()
		: pInfo(nullptr)
	{}
	inline AssetFileInfoEx_KeyRef(AssetFileInfoEx *pInfo)
		: pInfo(pInfo)
	{}
	inline bool operator<(const AssetFileInfoEx_KeyRef other) const
	{
		assert(pInfo && other.pInfo);
		return pInfo->index < other.pInfo->index;
	}
	inline bool operator==(const AssetFileInfoEx_KeyRef other) const
	{
		assert(pInfo && other.pInfo);
		return pInfo->index == other.pInfo->index;
	}
};

//Provides additional info for assets and an O(log(assetFileInfoCount)) lookup by path ID.
//The names are always read from the asset itself if possible,
//	see AssetBundleFileTable or ResourceManagerFile for more detailed names (usually not set for all assets with m_Name fields).
class AssetsFileTable
{
	AssetsFile *pFile;
	IAssetsReader *pReader;

	std::set<AssetFileInfoEx_KeyRef> lookup;

	public:
		AssetFileInfoEx *pAssetFileInfo;
		unsigned int assetFileInfoCount;

	public:
		ASSETSTOOLS_API AssetsFileTable(AssetsFile *pFile);
		ASSETSTOOLS_API ~AssetsFileTable();

		ASSETSTOOLS_API AssetFileInfoEx *getAssetInfo(QWORD pathId);

		//Improves the performance of getAssetInfo but uses additional memory.
		ASSETSTOOLS_API bool GenerateQuickLookupTree();
		
		ASSETSTOOLS_API AssetsFile *getAssetsFile();
		ASSETSTOOLS_API IAssetsReader *getReader();
};
