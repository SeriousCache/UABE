#pragma once
#include "AssetsFileFormat.h"

#include "defines.h"

class AssetFileInfoEx : public AssetFileInfo
{
	public:
		//AssetsHeader format < 0x10 : equals curFileTypeOrIndex
		//AssetsHeader format >= 0x10 : equals TypeTree.pTypes_Unity5[curFileTypeOrIndex].classId or (DWORD)-2 if the index is out of bounds
		DWORD curFileType;
		QWORD absolutePos;

		//The name field has been removed in 2.2, please use ReadName instead.

		//If the file type id is known to have a name field, it reads up to 100 characters (including the null-terminator) to "out".
		ASSETSTOOLS_API bool ReadName(AssetsFile *pFile, char *out);
};

//File tables make searching assets easier
//The names are always read from the asset itself if possible,
//	see AssetBundleFileTable or ResourceManagerFile for more/better names
class AssetsFileTable
{
	AssetsFile *pFile;
	IAssetsReader *pReader;
	
	BinarySearchTree_LookupTreeNode<unsigned long long> *pLookupBase;

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
