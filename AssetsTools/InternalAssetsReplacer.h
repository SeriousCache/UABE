#pragma once
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/defines.h"

enum EAssetsReplacers
{
	AssetsReplacer_AssetRemover,
	AssetsReplacer_AssetModifierFromReader,
	AssetsReplacer_AssetModifierFromMemory,
	AssetsReplacer_AssetModifierFromFile,
	AssetsReplacer_Dependencies
};

class AssetsEntryReplacerBase : public AssetsEntryReplacer
{
public:
	uint32_t fileID;
	QWORD pathID;
	int classID;
	uint16_t monoScriptIndex;
	std::vector<AssetPPtr> preloadDependencies;
	
	AssetsEntryReplacerBase();
	AssetsEntryReplacerBase(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex);

	uint32_t GetFileID() const;
	QWORD GetPathID() const;
	int GetClassID() const;
	uint16_t GetMonoScriptID() const;
	void SetMonoScriptID(uint16_t scriptID);
	
	bool GetPropertiesHash(Hash128 &propertiesHash);
	bool SetPropertiesHash(const Hash128 &propertiesHash);
	bool GetScriptIDHash(Hash128 &scriptIDHash);
	bool SetScriptIDHash(const Hash128 &scriptIDHash);
	
	bool GetTypeInfo(std::shared_ptr<ClassDatabaseFile> &pFile, ClassDatabaseType *&pType);
	bool SetTypeInfo(std::shared_ptr<ClassDatabaseFile> pFile, ClassDatabaseType *pType);

	bool GetPreloadDependencies(const AssetPPtr *&pPreloadList, size_t &preloadListSize);
	bool SetPreloadDependencies(const AssetPPtr *pPreloadList, size_t preloadListSize);
	bool AddPreloadDependency(const AssetPPtr &dependency);
	
	//Writes the elements of AssetsReplacerBase with the version headers (always writes fileID 0).
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetRemover : public AssetsEntryReplacerBase
{
public:
	AssetRemover(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex = 0xFFFF);
	~AssetRemover();
	AssetsReplacementType GetType() const;

	QWORD GetSize() const;
	QWORD Write(QWORD pos, IAssetsWriter *pWriter);
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetEntryModifierBase : public AssetsEntryReplacerBase
{
public:
	std::shared_ptr<ClassDatabaseFile> pClassFile;
	ClassDatabaseType *pClassType;

	Hash128 propertiesHash;
	Hash128 scriptIDHash;
	bool hasPropertiesHash, hasScriptIDHash;
public:
	AssetEntryModifierBase();
	AssetEntryModifierBase(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex);
	~AssetEntryModifierBase();
	
	bool GetPropertiesHash(Hash128 &propertiesHash);
	bool SetPropertiesHash(const Hash128 &propertiesHash);
	bool GetScriptIDHash(Hash128 &scriptIDHash);
	bool SetScriptIDHash(const Hash128 &scriptIDHash);
	
	bool GetTypeInfo(std::shared_ptr<ClassDatabaseFile> &pFile, ClassDatabaseType *&pType);
	bool SetTypeInfo(std::shared_ptr<ClassDatabaseFile> pFile, ClassDatabaseType *pType);
	
	//Writes the elements of AssetModifierBase and AssetsReplacerBase with their version headers.
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetModifierFromReader : public AssetEntryModifierBase
{
	std::shared_ptr<IAssetsReader> ref_pReader;
public:
	IAssetsReader *pReader;
	QWORD size;
	QWORD readerPos;
	size_t copyBufferLen;
	bool freeReader;

	AssetModifierFromReader();
	AssetModifierFromReader(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex, 
		IAssetsReader *pReader, QWORD size, QWORD readerPos=0, 
		size_t copyBufferLen=0, bool freeReader=false,
		std::shared_ptr<IAssetsReader> ref_pReader = nullptr);
	~AssetModifierFromReader();
	AssetsReplacementType GetType() const;

	QWORD GetSize() const;
	QWORD Write(QWORD pos, IAssetsWriter *pWriter);
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetModifierFromMemory : public AssetEntryModifierBase
{
public:
	void *buffer;
	size_t size;
	cbFreeMemoryResource freeMemCallback;

	AssetModifierFromMemory(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		void *buffer, size_t size, cbFreeMemoryResource freeMemCallback = NULL);
	~AssetModifierFromMemory();
	AssetsReplacementType GetType() const;

	QWORD GetSize() const;
	QWORD Write(QWORD pos, IAssetsWriter *pWriter);
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetModifierFromFile : public AssetEntryModifierBase
{
public:
	FILE *pFile;
	QWORD offs;
	QWORD size;
	size_t copyBufferLen;
	bool freeFile;

	AssetModifierFromFile(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		FILE *pFile, QWORD offs, QWORD size, size_t copyBufferLen=0, bool freeFile=true);
	~AssetModifierFromFile();
	AssetsReplacementType GetType() const;

	QWORD GetSize() const;
	QWORD Write(QWORD pos, IAssetsWriter *pWriter);
	QWORD WriteReplacer(QWORD pos, IAssetsWriter *pWriter);
};

class AssetsDependenciesReplacerImpl : public AssetsDependenciesReplacer
{
public:
	uint32_t fileID;
	std::vector<AssetsFileDependency> dependencies;

	AssetsDependenciesReplacerImpl(uint32_t fileID, std::vector<AssetsFileDependency> dependencies);

	AssetsReplacementType GetType() const;
	uint32_t GetFileID() const;

	const std::vector<AssetsFileDependency>& GetDependencies() const;

	QWORD WriteReplacer(QWORD pos, IAssetsWriter* pWriter);
};
