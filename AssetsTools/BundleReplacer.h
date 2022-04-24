#pragma once
#include "defines.h"
#include "AssetsFileFormat.h"
#include "AssetBundleFileFormat.h"
#include "AssetsFileReader.h"
#include <memory>

enum BundleReplacementType
{
	BundleReplacement_AddOrModify,
	BundleReplacement_Rename,
	BundleReplacement_Remove
};
class BundleReplacer : public GenericReplacer
{
	public:
		virtual BundleReplacementType GetType() = 0;
		
		virtual unsigned int GetBundleListIndex() = 0; //use only if there are multiple entries with the name, is -1 if unknown

		//The name of the entry (before ANY rename)
		virtual const char *GetOriginalEntryName() = 0;
		virtual const char *GetEntryName() = 0;
		//returns -1 if the original entry data should be used; 
		//DO NOT rely on the value returned : some modifiers can't easily calculate the target size, so 0 is returned
		virtual QWORD GetSize() = 0;

		virtual bool Init(class AssetBundleFile *pBundleFile, 
			IAssetsReader *pEntryReader, 
			QWORD entryPos, QWORD entrySize,
			ClassDatabaseFile *typeMeta = NULL) = 0;
		virtual void Uninit() = 0;

		//should allow writes multiple times (although probably not used)
		virtual QWORD Write(QWORD pos, IAssetsWriter *pWriter) = 0;

		//writes a binary representation of the replacer
		virtual QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter) = 0;

		//if true, this is an .assets file (at least I don't think there are any "serialized" files that aren't .assets)
		virtual bool HasSerializedData() = 0;

		//if true, this replacer depends on the existing state as provided through Init(..).
		virtual bool RequiresEntryReader() = 0;
};

//use NULL for newName to keep the old name

ASSETSTOOLS_API BundleReplacer *ReadBundleReplacer(QWORD &pos, IAssetsReader *pEntryReader, bool prefReplacerInMemory = false);
ASSETSTOOLS_API BundleReplacer *ReadBundleReplacer(QWORD &pos, std::shared_ptr<IAssetsReader> pEntryReader, bool prefReplacerInMemory = false);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryRemover(const char *name, unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryRenamer(const char *oldName, const char *newName, bool hasSerializedData,
	unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifier(const char *oldName, const char *newName, bool hasSerializedData,
	IAssetsReader *pEntryReader, cbFreeReaderResource freeReaderCallback, QWORD size, QWORD readerPos=0, 
	size_t copyBufferLen=0,
	unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifier(const char *oldName, const char *newName, bool hasSerializedData,
	std::shared_ptr<IAssetsReader> pEntryReader, QWORD size, QWORD readerPos=0, 
	size_t copyBufferLen=0,
	unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromMem(const char *oldName, const char *newName, bool hasSerializedData,
	void *pMem, size_t size,
	unsigned int bundleListIndex = (unsigned int)-1,
	cbFreeMemoryResource freeResourceCallback = NULL);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromAssets(const char *oldName, const char *newName, 
	AssetsFile *pAssetsFile, AssetsReplacer **pReplacers, size_t replacerCount, uint32_t fileId,
	unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierFromAssets(const char *oldName, const char *newName, 
	std::shared_ptr<ClassDatabaseFile> typeMeta, std::vector<std::shared_ptr<AssetsReplacer>> pReplacers, uint32_t fileId,
	unsigned int bundleListIndex = (unsigned int)-1);
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromBundle(const char *oldName, const char *newName, 
	BundleReplacer **pReplacers, size_t replacerCount, 
	unsigned int bundleListIndex);
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierFromBundle(const char *oldName, const char *newName,
	std::vector<std::unique_ptr<BundleReplacer>> pReplacers, unsigned int bundleListIndex);


struct ReplacedResourceDesc
{
	//Invariant: outRangeBegin must be equal to the previous data range end location, or 0 for the first range.
	uint64_t outRangeBegin;
	//Size of the data range (both in and out).
	uint64_t rangeSize;
	//Replacement reader.
	//- non-null: Replacement data copied from reader.
	//- nullptr, fromOriginalFile true:  Data copied from original reader.
	//- nullptr, fromOriginalFile false: Empty block (zero data).
	std::shared_ptr<IAssetsReader> reader;
	//Start offset in the original file (i.e. fromOriginalFile set, reader nullptr).
	uint64_t inRangeBegin;
	//Set if this resource data range is copied directly from the source file (-> reader nullptr).
	bool fromOriginalFile;
};
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierByResources(const char* oldName, const char* newName,
	std::vector<ReplacedResourceDesc> resources, size_t copyBufferLen = 0,
	unsigned int bundleListIndex = (unsigned int)-1);

ASSETSTOOLS_API void FreeBundleReplacer(BundleReplacer *pReplacer);

//Tries to create a reader from a BundleReplacer.
//Works only for replacers created through MakeBundleEntryModifier or MakeBundleEntryModifierFromMem.
//For other kinds of replacers, nullptr will be returned.
//-> For MakeBundleEntryModifier, the internal reader will be reused.
//-> For MakeBundleEntryModifierFromMem, the internal buffer will be reused and the returned shared_ptr will also keep a BundleReplacer reference.
ASSETSTOOLS_API std::shared_ptr<IAssetsReader> MakeReaderFromBundleEntryModifier(std::shared_ptr<BundleReplacer> pReplacer);
