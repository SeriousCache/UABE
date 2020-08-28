#pragma once
#include "defines.h"
#include "AssetsFileFormat.h"
#include "AssetsFileReader.h"
#include "ClassDatabaseFile.h"

enum AssetsReplacementType
{
	AssetsReplacement_AddOrModify,
	AssetsReplacement_Remove
};
class AssetsReplacer
{
	public:
		virtual AssetsReplacementType GetType() = 0;
		virtual ~AssetsReplacer()
			#ifndef ASSETSTOOLS_EXPORTS
			= 0
			#endif
			;

		//Base properties of the asset this replacer refers to.
		virtual DWORD GetFileID() = 0;
		virtual QWORD GetPathID() = 0;
		virtual int GetClassID() = 0;
		virtual WORD GetMonoScriptID() = 0;

		virtual void SetMonoScriptID(WORD scriptID) = 0;

		//For add and modify

		//Returns false if the replacer has no properties hash (always the case for removers in practice).
		//The properties hash is for the full type, also for MonoBehaviours.
		virtual bool GetPropertiesHash(Hash128 &propertiesHash) = 0;
		virtual bool SetPropertiesHash(Hash128 &propertiesHash) = 0;
		//Returns false if the replacer has no script hash (always the case for removers and non-MonoBehaviour types in practice).
		//ScriptID is the hash for the specific MonoBehaviour type except the MonoBehaviour header.
		virtual bool GetScriptIDHash(Hash128 &scriptIDHash) = 0;
		virtual bool SetScriptIDHash(Hash128 &scriptIDHash) = 0;
		
		//Do not use the returned free callback unless you used SetTypeInfo with (nullptr, nullptr, true, nullptr) before.
		virtual bool GetTypeInfo(ClassDatabaseFile *&pFile, ClassDatabaseType *&pType, OUT cbFreeClassDatabase *pFreeCallback = nullptr) = 0;
		//If freeCallback is not null, frees the given database file/type after use (even if SetTypeInfo fails).
		virtual bool SetTypeInfo(ClassDatabaseFile *pFile, ClassDatabaseType *pType, bool localCopy, cbFreeClassDatabase freeCallback = nullptr) = 0;

		//Retrieves the list of preload dependencies of this replacer; Do not reuse the preload list pointer after calling Set/Add.
		virtual bool GetPreloadDependencies(const struct AssetPPtr *&pPreloadList, size_t &preloadListSize) = 0;
		//Overwrites this replacers' list of preload dependencies.
		virtual bool SetPreloadDependencies(const struct AssetPPtr *pPreloadList, size_t preloadListSize) = 0;
		//Adds a new preload dependency. Does not check for duplicates.
		virtual bool AddPreloadDependency(const struct AssetPPtr &dependency) = 0;

		//Retrieves the data size of this replacer.
		virtual QWORD GetSize() = 0;
 		//Outputs the data of this replacer into a writer.
		//pos : Absolute writer file position to write the data.
		//Returns the new absolute writer file position.
		virtual QWORD Write(QWORD pos, IAssetsWriter *pWriter) = 0;
		
		//Outputs a binary representation of this replacer. The read counterpart of this function is ReadAssetsReplacer.
		//Always writes 0 for the file id.
		//pos : Absolute writer file position to write the data.
		//Returns the new absolute writer file position.
		virtual QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter) = 0;
};

ASSETSTOOLS_API AssetsReplacer *ReadAssetsReplacer(QWORD &pos, IAssetsReader *pReader, bool prefReplacerInMemory = false);

ASSETSTOOLS_API AssetsReplacer *MakeAssetRemover(DWORD fileID, QWORD pathID, int classID, WORD monoScriptIndex = 0xFFFF);

ASSETSTOOLS_API AssetsReplacer *MakeAssetModifierFromReader(
		DWORD fileID, QWORD pathID, int classID, WORD monoScriptIndex,
		IAssetsReader *pReader, QWORD size, QWORD readerPos=0, 
		size_t copyBufferLen=0, bool freeReader=false);

ASSETSTOOLS_API AssetsReplacer *MakeAssetModifierFromMemory(
		DWORD fileID, QWORD pathID, int classID, WORD monoScriptIndex,
		void *buffer, size_t size, cbFreeMemoryResource freeResourceCallback);

ASSETSTOOLS_API AssetsReplacer *MakeAssetModifierFromFile(
		DWORD fileID, QWORD pathID, int classID, WORD monoScriptIndex,
		FILE *pFile, QWORD offs, QWORD size, size_t copyBufferLen=0, bool freeFile=true);


ASSETSTOOLS_API void FreeAssetsReplacer(AssetsReplacer *pReplacer);