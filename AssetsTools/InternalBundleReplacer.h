#pragma once
#include "../AssetsTools/BundleReplacer.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/ClassDatabaseFile.h"

enum EBundleReplacers
{
	BundleReplacer_BundleEntryRemover,
	BundleReplacer_BundleEntryRenamer,
	BundleReplacer_BundleEntryModifier,
	BundleReplacer_BundleEntryModifierFromMem,
	BundleReplacer_BundleEntryModifierFromAssets,
	BundleReplacer_BundleEntryModifierFromBundle,
	BundleReplacer_BundleEntryModifierByResources,
	BundleReplacer_MAX,
};

class BundleEntryRemover : public BundleReplacer
{
	protected:
	unsigned int bundleListIndex;
	char *originalEntryName;
	
	public:
		BundleEntryRemover(const char *name, unsigned int bundleListIndex);
		BundleReplacementType GetType();
		~BundleEntryRemover();
		unsigned int GetBundleListIndex();
		const char *GetOriginalEntryName();
		const char *GetEntryName();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
		bool HasSerializedData();
		bool RequiresEntryReader();
};
class BundleEntryRenamer : public BundleReplacer
{
	protected:
	unsigned int bundleListIndex;
	char *originalEntryName;
	char *newEntryName;
	bool hasSerializedData;
	
	public:
		BundleEntryRenamer(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData);
		BundleReplacementType GetType();
		~BundleEntryRenamer();
		unsigned int GetBundleListIndex();
		const char *GetOriginalEntryName();
		const char *GetEntryName();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
		bool HasSerializedData();
		bool RequiresEntryReader();
};

class BundleEntryModifier : public BundleEntryRenamer
{
	public:
		std::shared_ptr<IAssetsReader> pReader;
		QWORD size;
		QWORD readerPos;
		size_t copyBufferLen;
	public:
		BundleEntryModifier(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData, 
			std::shared_ptr<IAssetsReader> pReader, QWORD size, QWORD readerPos, 
			size_t copyBufferLen);
		BundleReplacementType GetType();
		~BundleEntryModifier();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class BundleEntryModifierFromMem : public BundleEntryRenamer
{
	public:
	void *pMem;
	size_t size;
	cbFreeMemoryResource freeResourceCallback;

	public:
		//freeMemoryCallback is used for pMem and if (freeNames) for oldName/newName
		BundleEntryModifierFromMem(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData, 
			void *pMem, size_t size, cbFreeMemoryResource freeMemoryCallback);
		BundleReplacementType GetType();
		~BundleEntryModifierFromMem();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};
class BundleEntryModifierFromAssets : public BundleEntryRenamer
{
	protected:
	AssetsFile *pAssetsFile;
	std::vector<AssetsReplacer*> pReplacers;
	ClassDatabaseFile *typeMeta;
	uint32_t fileId;
	bool freeAssetsFile;

	// Optional reference holders.
	std::vector<std::shared_ptr<AssetsReplacer>> pReplacers_shared;
	std::shared_ptr<ClassDatabaseFile> typeMeta_shared;

	public:
		BundleEntryModifierFromAssets(const char *oldName, const char *newName, unsigned int bundleListIndex, 
			AssetsFile *pAssetsFile, AssetsReplacer **pReplacers, size_t replacerCount, uint32_t fileId);
		BundleEntryModifierFromAssets(const char *oldName, const char *newName, unsigned int bundleListIndex, 
			std::shared_ptr<ClassDatabaseFile> typeMeta, std::vector<std::shared_ptr<AssetsReplacer>> pReplacers, uint32_t fileId);
		BundleReplacementType GetType();
		~BundleEntryModifierFromAssets();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
		ASSETSTOOLS_API AssetsReplacer **GetReplacers(size_t &count);
		ASSETSTOOLS_API AssetsFile *GetAssignedAssetsFile();
		//Returns -1 if not set.
		ASSETSTOOLS_API uint32_t GetFileID();
		bool RequiresEntryReader();
};
class BundleEntryModifierFromBundle : public BundleEntryRenamer
{
	protected:
	std::vector<BundleReplacer*> pReplacers;
	AssetBundleFile *pBundleFile;
	IAssetsReader *pBundleReader;
	ClassDatabaseFile *typeMeta;
	bool freeBundleFile;
	
	// Optional reference holders.
	std::vector<std::unique_ptr<BundleReplacer>> pReplacers_unique;

	public:
		BundleEntryModifierFromBundle(const char *oldName, const char *newName, unsigned int bundleListIndex,
			BundleReplacer **pReplacers, size_t replacerCount);
		BundleEntryModifierFromBundle(const char *oldName, const char *newName, unsigned int bundleListIndex,
			std::vector<std::unique_ptr<BundleReplacer>> pReplacers);
		BundleReplacementType GetType();
		~BundleEntryModifierFromBundle();
		QWORD GetSize();
		bool Init(AssetBundleFile *pBundleFile,
			IAssetsReader *pEntryReader,
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL);
		void Uninit();
		QWORD Write(QWORD pos, IAssetsWriter *pWriter);
		QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
		ASSETSTOOLS_API BundleReplacer **GetReplacers(size_t &count);
		bool RequiresEntryReader();
};
class BundleEntryModifierByResources : public BundleEntryRenamer
{
	std::vector<ReplacedResourceDesc> resources;
	size_t copyBufferLen;
	IAssetsReader* pEntryReader = nullptr;
	QWORD entryPos = 0, entrySize = 0;
public:
	inline const std::vector<ReplacedResourceDesc>& getResources()
	{
		return resources;
	}
	inline QWORD getSize()
	{
		return resources.empty() ? 0 : (resources.back().outRangeBegin + resources.back().rangeSize);
	}
public:
	BundleEntryModifierByResources(const char* oldName, const char* newName, unsigned int bundleListIndex, 
		std::vector<ReplacedResourceDesc> resources,
		size_t copyBufferLen);
	BundleReplacementType GetType();
	~BundleEntryModifierByResources();
	QWORD GetSize();
	bool Init(AssetBundleFile* pBundleFile,
		IAssetsReader* pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile* typeMeta = NULL);
	void Uninit();
	QWORD Write(QWORD pos, IAssetsWriter* pWriter);
	QWORD WriteReplacer(QWORD pos, IAssetsWriter* pWriter);
	bool RequiresEntryReader();

};
