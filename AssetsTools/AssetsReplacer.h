#pragma once
#include "defines.h"
#include "AssetsFileFormat.h"
#include "AssetsFileReader.h"
#include "ClassDatabaseFile.h"
#include "AssetsFileFormatTypes.h"
#include <memory>

class GenericReplacer
{
	public:
		virtual ~GenericReplacer()
			#ifndef ASSETSTOOLS_EXPORTS
			= 0
			#endif
			;
};

enum AssetsReplacementType
{
	AssetsReplacement_AddOrModify, //class AssetsEntryReplacer
	AssetsReplacement_Remove, //class AssetsEntryReplacer
	AssetsReplacement_Dependencies, //class AssetsDependenciesReplacer
};
class AssetsReplacer : public GenericReplacer
{
	public:
		virtual AssetsReplacementType GetType() const = 0;

		//Base properties of the asset this replacer refers to.
		virtual uint32_t GetFileID() const = 0;
		
		//Outputs a binary representation of this replacer. The read counterpart of this function is ReadAssetsReplacer.
		//Always writes 0 for the file id.
		//pos : Absolute writer file position to write the data.
		//Returns the new absolute writer file position.
		virtual QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter) = 0;
};
class AssetsEntryReplacer : public AssetsReplacer
{
	public:
		//Base properties of the asset this replacer refers to.
		virtual QWORD GetPathID() const = 0;
		virtual int GetClassID() const = 0;
		virtual uint16_t GetMonoScriptID() const = 0;

		virtual void SetMonoScriptID(uint16_t scriptID) = 0;

		//For add and modify

		//Returns false if the replacer has no properties hash (always the case for removers in practice).
		//The properties hash is for the full type, also for MonoBehaviours.
		virtual bool GetPropertiesHash(Hash128 &propertiesHash) = 0;
		virtual bool SetPropertiesHash(const Hash128 &propertiesHash) = 0;
		//Returns false if the replacer has no script hash (always the case for removers and non-MonoBehaviour types in practice).
		//ScriptID is the hash for the specific MonoBehaviour type except the MonoBehaviour header.
		virtual bool GetScriptIDHash(Hash128 &scriptIDHash) = 0;
		virtual bool SetScriptIDHash(const Hash128 &scriptIDHash) = 0;
		
		virtual bool GetTypeInfo(std::shared_ptr<class ClassDatabaseFile> &pFile, class ClassDatabaseType *&pType) = 0;
		virtual bool SetTypeInfo(std::shared_ptr<class ClassDatabaseFile> pFile, class ClassDatabaseType *pType) = 0;

		//Retrieves the list of preload dependencies of this replacer; Do not reuse the preload list pointer after calling Set/Add.
		virtual bool GetPreloadDependencies(const struct AssetPPtr *&pPreloadList, size_t &preloadListSize) = 0;
		//Overwrites this replacers' list of preload dependencies.
		virtual bool SetPreloadDependencies(const struct AssetPPtr *pPreloadList, size_t preloadListSize) = 0;
		//Adds a new preload dependency. Does not check for duplicates.
		virtual bool AddPreloadDependency(const struct AssetPPtr &dependency) = 0;

		//Retrieves the data size of this replacer.
		virtual QWORD GetSize() const = 0;
 		//Outputs the data of this replacer into a writer.
		//pos : Absolute writer file position to write the data.
		//Returns the new absolute writer file position.
		virtual QWORD Write(QWORD pos, IAssetsWriter *pWriter) = 0;
};
class AssetsDependenciesReplacer : public AssetsReplacer
{
	public:
		virtual const std::vector<AssetsFileDependency>& GetDependencies() const = 0;
};

ASSETSTOOLS_API AssetsReplacer *ReadAssetsReplacer(QWORD &pos, IAssetsReader *pReader, bool prefReplacerInMemory = false);
ASSETSTOOLS_API AssetsReplacer *ReadAssetsReplacer(QWORD &pos, std::shared_ptr<IAssetsReader> pReader, bool prefReplacerInMemory = false);

ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetRemover(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex = 0xFFFF);

ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromReader(
		uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		IAssetsReader *pReader, QWORD size, QWORD readerPos=0, 
		size_t copyBufferLen=0, bool freeReader=false);

ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromMemory(
		uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		void *buffer, size_t size, cbFreeMemoryResource freeResourceCallback);

ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromFile(
		uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		FILE *pFile, QWORD offs, QWORD size, size_t copyBufferLen=0, bool freeFile=true);

ASSETSTOOLS_API AssetsDependenciesReplacer *MakeAssetsDependenciesReplacer(
	uint32_t fileID, std::vector<AssetsFileDependency> dependencies);

//Note: Equivalent to 'delete pReplacer'.
ASSETSTOOLS_API void FreeAssetsReplacer(AssetsReplacer *pReplacer);