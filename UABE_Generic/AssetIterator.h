#pragma once
#include "api.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "FileContextInfo.h"
#include <memory>
#include <stdint.h>

//Identifies an asset and optionally allows faster lookup than just fileID&pathID.
//Behaves like a snapshot once pFile and pAssetInfo/pReplacer are set by the constructor or by resolve(.) .
class AssetIdentifier
{
public:
	unsigned int fileID; //Mandatory
	pathid_t pathID; //Mandatory

	bool fileIDIsRelative; //Mandatory
	unsigned int referenceFromFileID; //(Mandatory <=> fileIDIsRelative)

	std::shared_ptr<class AssetsFileContextInfo> pFile; //Optional
	AssetFileInfoEx *pAssetInfo; //Optional, for existing assets
	std::shared_ptr<AssetsEntryReplacer> pReplacer; //Optional, for new assets and modified existing assets
	//Instantiate an AssetIdentifier with an absolute fileID and a pathID.
	inline AssetIdentifier()
		: fileID(0), pathID(0), fileIDIsRelative(false), referenceFromFileID(0), pFile(nullptr), pAssetInfo(nullptr)
	{}
	inline AssetIdentifier(unsigned int fileID, pathid_t pathID)
		: fileID(fileID), pathID(pathID), fileIDIsRelative(false), referenceFromFileID(0), pFile(nullptr), pAssetInfo(nullptr)
	{}
	UABE_Generic_API AssetIdentifier(unsigned int fileID, std::shared_ptr<AssetsEntryReplacer> &pReplacer);
	//Instantiate an AssetIdentifier with a relative fileID relative to the dependencies for an absolute fileID and a pathID.
	UABE_Generic_API AssetIdentifier(unsigned int referenceFromFileID, unsigned int relFileID, pathid_t pathID);
	//Instantiate an AssetIdentifier with a relative fileID (from pReferenceFromFile->references) and a pathID.
	UABE_Generic_API AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> &pReferenceFromFile, unsigned int relFileID, pathid_t pathID);
	UABE_Generic_API AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> pFile, pathid_t pathID);
	UABE_Generic_API AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> pFile, AssetFileInfoEx *pAssetInfo);
	UABE_Generic_API AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> pFile, std::shared_ptr<AssetsEntryReplacer> &pReplacer);
	UABE_Generic_API bool resolve(class AppContext &appContext);
	
	//Will return INT32_MIN if resolve was not called successfully (if a constructor with pathID was used)
	UABE_Generic_API int32_t getClassID();
	inline int32_t getClassID(class AppContext &appContext)
	{
		if (resolve(appContext)) return getClassID();
		return INT32_MIN;
	}
	//Will return 0xFFFF if resolve was not called successfully (if a constructor with pathID was used)
	//(Note: 0xFFFF is the general default if this asset is not a serialized script.)
	UABE_Generic_API uint16_t getMonoScriptID();
	inline uint16_t getMonoScriptID(class AppContext &appContext)
	{
		if (resolve(appContext)) return getMonoScriptID();
		return 0xFFFF;
	}

	//Will return 0 if resolve was not called successfully (if a constructor with pathID was used)
	UABE_Generic_API uint64_t getDataSize();
	inline uint64_t getDataSize(class AppContext &appContext)
	{
		if (resolve(appContext)) return getDataSize();
		return 0;
	}
	//Will return 0 also if resolve was not called successfully (if a constructor with pathID and fileID or relFileID was used)
	UABE_Generic_API size_t read(size_t size, void *buffer); //Returns the actually read bytes.
	inline size_t read(class AppContext &appContext, size_t size, void *buffer)
	{
		if (resolve(appContext)) return read(size, buffer);
		return 0;
	}
	//Will return nullptr if resolve was not called successfully (if a constructor with pathID and fileID or relFileID was used)
	//Generates a reader or reader view for the asset. Make sure that the reader pointer is destructed before freeing the AssetIdentifier.
	UABE_Generic_API IAssetsReader_ptr makeReader();
	inline IAssetsReader_ptr makeReader(class AppContext &appContext)
	{
		if (resolve(appContext)) return makeReader();
		return IAssetsReader_ptr(nullptr, DummyAssetsReaderDeleter);
	}

	UABE_Generic_API bool isBigEndian();
};

//Asset iterator class, intended for short use.
//Note: May keep a shared lock on the replacers list (if ignoreReplacers==false) for the entire object lifetime.
//-> Do not add a replacer to the file while holding an AssetIterator, else a deadlock can occur!
class AssetIterator
{
	class AssetsFileContextInfo *pContextInfo;
	AssetsFileTable *pAssetsFileTable;
	unsigned int assetIndex; std::shared_ptr<AssetsEntryReplacer> *pAssetReplacerHint;
	bool ignoreReplacers, ignoreExisting, ignoreRemoverReplacers;
	std::unordered_map<pathid_t, struct AssetsFileContextInfo::ReplacerEntry>::const_iterator replacersIterator;
	void updateAssetReplacerHint();
public:
	inline AssetIterator()
		: pContextInfo(nullptr), pAssetsFileTable(nullptr), pAssetReplacerHint(nullptr), assetIndex(0),
		ignoreReplacers(true), ignoreExisting(true), ignoreRemoverReplacers(true)
	{}
	//ignoreExisting : If set to true, original assets from the file will be skipped.
	//ignoreReplacers : If set to true, replacers will be skipped.
	//ignoreRemoverReplacers : If set to true, remover replacers and removed assets will be skipped.
	//    If set to false, remover replacers for original are iterated over if ignoreExisting is set to true and ignoreReplacers is set to false.
	//    Specifically, since removers are only stored for original assets, setting this option to false only has an effect if ignoreExisting is set to true.
	UABE_Generic_API AssetIterator(class AssetsFileContextInfo *pContextInfo, bool ignoreExisting = false, bool ignoreReplacers = false, bool ignoreRemoverReplacers = true);
	UABE_Generic_API ~AssetIterator();
	inline AssetIterator(const AssetIterator &other) { (*this) = other; }
	UABE_Generic_API AssetIterator(AssetIterator &&other);
	UABE_Generic_API AssetIterator &operator=(const AssetIterator &other);
	
	UABE_Generic_API AssetIterator &operator++();
	inline AssetIterator operator++(int) { AssetIterator ret(*this); ++(*this); return ret; }

	UABE_Generic_API bool isEnd() const;
	//Does not set identifier.pFile (and resets it if it does not point to the correct AssetsFileContextInfo).
	//If it was not set before, identifier.resolve(.) needs to be called to use it.
	UABE_Generic_API void get(AssetIdentifier &identifier);
};
