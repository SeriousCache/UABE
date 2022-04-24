#include "stdafx.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "InternalAssetsReplacer.h"

GenericReplacer::~GenericReplacer()
{
}

#pragma region AssetsReplacerBase
AssetsEntryReplacerBase::AssetsEntryReplacerBase()
{
	this->fileID = 0;
	this->pathID = 0;
	this->classID = 0;
	this->monoScriptIndex = 0;
}
AssetsEntryReplacerBase::AssetsEntryReplacerBase(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex)
{
	this->fileID = fileID;
	this->pathID = pathID;
	this->classID = classID;
	this->monoScriptIndex = monoScriptIndex;
}
uint32_t AssetsEntryReplacerBase::GetFileID() const {return this->fileID;}
QWORD AssetsEntryReplacerBase::GetPathID() const {return this->pathID;}
int AssetsEntryReplacerBase::GetClassID() const {return this->classID;}
uint16_t AssetsEntryReplacerBase::GetMonoScriptID() const {return this->monoScriptIndex;}
void AssetsEntryReplacerBase::SetMonoScriptID(uint16_t scriptID) {this->monoScriptIndex = scriptID;}
bool AssetsEntryReplacerBase::GetPropertiesHash(Hash128 &propertiesHash) { propertiesHash = Hash128(); return false; }
bool AssetsEntryReplacerBase::SetPropertiesHash(const Hash128 &propertiesHash) { return false; }
bool AssetsEntryReplacerBase::GetScriptIDHash(Hash128 &scriptIDHash) { scriptIDHash = Hash128(); return false; }
bool AssetsEntryReplacerBase::SetScriptIDHash(const Hash128 &scriptIDHash) { return false; }
bool AssetsEntryReplacerBase::GetTypeInfo(std::shared_ptr<ClassDatabaseFile> &pFile, ClassDatabaseType *&pType)
{
	pFile.reset(); pType = nullptr;
	return false;
}
bool AssetsEntryReplacerBase::SetTypeInfo(std::shared_ptr<ClassDatabaseFile> pFile, ClassDatabaseType *pType)
{
	return false;
}

QWORD AssetsEntryReplacerBase::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	uint8_t fileVersion = 1;
	pos += pWriter->Write(pos, 1, &fileVersion);
	uint32_t writeFileID = 0;
	pos += pWriter->Write(pos, 4, &writeFileID);
	pos += pWriter->Write(pos, 8, &this->pathID);
	pos += pWriter->Write(pos, 4, &this->classID);
	pos += pWriter->Write(pos, 2, &this->monoScriptIndex);

	uint32_t preloadDepCount = (this->preloadDependencies.size() > 0xFFFFFFFFULL) ? 0xFFFFFFFF : (uint32_t)this->preloadDependencies.size();
	pos += pWriter->Write(pos, 4, &preloadDepCount);
	for (uint32_t i = 0; i < preloadDepCount; i++)
	{
		pos += pWriter->Write(pos, 4, &this->preloadDependencies[i].fileID);
		pos += pWriter->Write(pos, 8, &this->preloadDependencies[i].pathID);
	}
	return pos;
}
bool AssetsEntryReplacerBase::GetPreloadDependencies(const AssetPPtr *&pPreloadList, size_t &preloadListSize)
{
	preloadListSize = this->preloadDependencies.size();
	pPreloadList = this->preloadDependencies.data();
	return true;
}
bool AssetsEntryReplacerBase::SetPreloadDependencies(const AssetPPtr *pPreloadList, size_t preloadListSize)
{
	this->preloadDependencies.clear();
	this->preloadDependencies.resize(preloadListSize);
	memcpy(this->preloadDependencies.data(), pPreloadList, sizeof(AssetPPtr) * preloadListSize);
	return true;
}
bool AssetsEntryReplacerBase::AddPreloadDependency(const AssetPPtr &dependency)
{
	this->preloadDependencies.push_back(dependency);
	return true;
}
#pragma endregion

#pragma region AssetModifierBase
AssetEntryModifierBase::AssetEntryModifierBase()
{
	this->hasPropertiesHash = false;
	this->hasScriptIDHash = false;
	this->pClassFile = nullptr;
	this->pClassType = nullptr;
}
AssetEntryModifierBase::AssetEntryModifierBase(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex)
	: AssetsEntryReplacerBase(fileID, pathID, classID, monoScriptIndex)
{
	this->hasPropertiesHash = false;
	this->hasScriptIDHash = false;
	this->pClassFile = nullptr;
	this->pClassType = nullptr;
}
AssetEntryModifierBase::~AssetEntryModifierBase()
{}
bool AssetEntryModifierBase::GetPropertiesHash(Hash128 &propertiesHash)
{
	propertiesHash = this->hasPropertiesHash ? this->propertiesHash : Hash128();
	return this->hasPropertiesHash;
}
bool AssetEntryModifierBase::SetPropertiesHash(const Hash128 &propertiesHash)
{
	this->propertiesHash = propertiesHash;
	this->hasPropertiesHash = true;
	return true;
}
bool AssetEntryModifierBase::GetScriptIDHash(Hash128 &scriptIDHash)
{
	scriptIDHash = this->hasScriptIDHash ? this->scriptIDHash : Hash128();
	return this->hasScriptIDHash;
}
bool AssetEntryModifierBase::SetScriptIDHash(const Hash128 &scriptIDHash)
{
	this->scriptIDHash = scriptIDHash;
	this->hasScriptIDHash = true;
	return true;
}
bool AssetEntryModifierBase::SetTypeInfo(std::shared_ptr<ClassDatabaseFile> pFile, ClassDatabaseType *pType)
{
	if ((pFile != nullptr) ^ (pType != nullptr)) //Exactly one of the two parameters is null, which is not allowed.
		return false;
	this->pClassFile = pFile;
	this->pClassType = pType;
	return true;
}
bool AssetEntryModifierBase::GetTypeInfo(std::shared_ptr<ClassDatabaseFile> &pFile, ClassDatabaseType *&pType)
{
	if (this->pClassFile && this->pClassType)
	{
		pFile = this->pClassFile;
		pType = this->pClassType;
		return true;
	}
	pFile.reset();
	pType = nullptr;
	return false;
}
QWORD AssetEntryModifierBase::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	pos = AssetsEntryReplacerBase::WriteReplacer(pos, pWriter);
	uint8_t fileVersion = 0;
	pos += pWriter->Write(pos, 1, &fileVersion);

	uint8_t hasPropertiesHash = this->hasPropertiesHash ? 1 : 0;
	pos += pWriter->Write(pos, 1, &hasPropertiesHash);
	if (hasPropertiesHash)
		pos += pWriter->Write(pos, 16, this->propertiesHash.bValue);

	uint8_t hasScriptIDHash = this->hasScriptIDHash ? 1 : 0;
	pos += pWriter->Write(pos, 1, &hasScriptIDHash);
	if (hasScriptIDHash)
		pos += pWriter->Write(pos, 16, this->scriptIDHash.bValue);

	uint8_t hasTypeInfo = 0;
	if (this->pClassFile && this->pClassType)
	{
		hasTypeInfo = 1;
		ClassDatabaseFile tempFile;
		ClassDatabaseFile *pWriteFile = this->pClassFile.get();
		if (this->pClassFile->classes.size() > 1)
		{
			if (tempFile.InsertFrom(this->pClassFile.get(), this->pClassType) && tempFile.classes.size() == 1)
				pWriteFile = &tempFile;
			else
				hasTypeInfo = 0;
		}
		pos += pWriter->Write(pos, 1, &hasTypeInfo);
		if (hasTypeInfo)
			pos = pWriteFile->Write(pWriter, pos);
	}
	else
		pos += pWriter->Write(pos, 1, &hasTypeInfo);
	return pos;
}
#pragma endregion

#pragma region AssetRemover
AssetRemover::AssetRemover(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex)
	: AssetsEntryReplacerBase(fileID, pathID, classID, monoScriptIndex)
{
}
AssetRemover::~AssetRemover()
{
}
AssetsReplacementType AssetRemover::GetType() const
{
	return AssetsReplacement_Remove;
}


QWORD AssetRemover::GetSize() const{return 0;}
QWORD AssetRemover::Write(QWORD pos, IAssetsWriter *pWriter){return pos;}
QWORD AssetRemover::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	uint16_t replacerType = AssetsReplacer_AssetRemover;
	pos += pWriter->Write(pos, 2, &replacerType);
	uint8_t fileVersion = 1;
	pos += pWriter->Write(pos, 1, &fileVersion);
	pos = AssetsEntryReplacerBase::WriteReplacer(pos, pWriter);
	return pos;
}
#pragma endregion
#pragma region AssetModifierFromReader
AssetModifierFromReader::AssetModifierFromReader()
{
	this->pReader = nullptr;
	this->size = 0;
	this->readerPos = 0;
	this->copyBufferLen = 0;
	this->freeReader = false;
}
AssetModifierFromReader::AssetModifierFromReader(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex, 
	IAssetsReader *pReader, QWORD size, QWORD readerPos, 
	size_t copyBufferLen, bool freeReader,
	std::shared_ptr<IAssetsReader> ref_pReader)
	: AssetEntryModifierBase(fileID, pathID, classID, monoScriptIndex)
{
	this->pReader = pReader;
	this->size = size;
	this->readerPos = readerPos;
	this->copyBufferLen = copyBufferLen;
	this->freeReader = freeReader;
	this->ref_pReader = std::move(ref_pReader);
}
AssetModifierFromReader::~AssetModifierFromReader()
{
	if (this->freeReader && this->pReader)
	{
		Free_AssetsReader(this->pReader);
		this->pReader = nullptr;
	}
}
AssetsReplacementType AssetModifierFromReader::GetType() const
{
	return AssetsReplacement_AddOrModify;
}

QWORD AssetModifierFromReader::GetSize() const{return size;}
QWORD AssetModifierFromReader::Write(QWORD pos, IAssetsWriter *pWriter)
{
	if (size == 0 || pReader == nullptr)
		return pos;
	uint8_t stackBuffer[512];
	void *copyBuffer = stackBuffer;
	size_t curBufferLen = 512;
	if (copyBufferLen > 512)
	{
		copyBuffer = malloc(copyBufferLen);
		if (copyBuffer == NULL)
			copyBuffer = stackBuffer;
		else
			curBufferLen = copyBufferLen;
	}
	QWORD remainingBytes = size; QWORD curReaderPos = readerPos;
	while (remainingBytes > curBufferLen)
	{		
		QWORD curRead = pReader->Read(curReaderPos, curBufferLen, copyBuffer);
		if (!curRead)
		{
			remainingBytes = 0;
			break;
		}
		QWORD curWritten = pWriter->Write(pos, curRead, copyBuffer);
		remainingBytes -= curWritten;
		curReaderPos += curWritten;
		pos += curWritten;
		if (curWritten == 0)
			break;
	}
	if (remainingBytes > 0)
	{
		QWORD curRead = pReader->Read(curReaderPos, remainingBytes, copyBuffer);
		QWORD curWritten = pWriter->Write(pos, curRead, copyBuffer);
		pos += curWritten;
	}
	if (curBufferLen > 512)
		free(copyBuffer);
	return pos;
}
QWORD AssetModifierFromReader::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	//store it the as a modifier from memory (no difference)
	uint16_t replacerType = AssetsReplacer_AssetModifierFromMemory;
	pos += pWriter->Write(pos, 2, &replacerType);
	uint8_t fileVersion = 1;
	pos += pWriter->Write(pos, 1, &fileVersion);
	pos = AssetEntryModifierBase::WriteReplacer(pos, pWriter);
	pos += pWriter->Write(pos, 8, &this->size);
	pos = Write(pos, pWriter);
	return pos;
}
#pragma endregion
#pragma region AssetModifierFromMemory
AssetModifierFromMemory::AssetModifierFromMemory(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
	void *buffer, size_t size, cbFreeMemoryResource freeMemCallback)
	: AssetEntryModifierBase(fileID, pathID, classID, monoScriptIndex)
{
	this->buffer = buffer;
	this->size = size;
	this->freeMemCallback = freeMemCallback;
}
AssetModifierFromMemory::~AssetModifierFromMemory()
{
	if (freeMemCallback != NULL)
		freeMemCallback(this->buffer);
}
AssetsReplacementType AssetModifierFromMemory::GetType() const
{
	return AssetsReplacement_AddOrModify;
}

QWORD AssetModifierFromMemory::GetSize() const{return size;}
QWORD AssetModifierFromMemory::Write(QWORD pos, IAssetsWriter *pWriter)
{
	if (!size)
		return pos;
	return pos + pWriter->Write(pos, size, buffer);
}
QWORD AssetModifierFromMemory::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	uint16_t replacerType = AssetsReplacer_AssetModifierFromMemory;
	pos += pWriter->Write(pos, 2, &replacerType);
	uint8_t fileVersion = 1;
	pos += pWriter->Write(pos, 1, &fileVersion);
	pos = AssetEntryModifierBase::WriteReplacer(pos, pWriter);
	QWORD ullSize = this->size;
	pos += pWriter->Write(pos, 8, &ullSize);
	pos = Write(pos, pWriter);
	return pos;
}
#pragma endregion
#pragma region AssetModifierFromFile

AssetModifierFromFile::AssetModifierFromFile(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
	FILE *pFile, QWORD offs, QWORD size, size_t copyBufferLen, bool freeFile)
	: AssetEntryModifierBase(fileID, pathID, classID, monoScriptIndex)
{
	this->pFile = pFile;
	this->offs = offs;
	this->size = size;
	this->copyBufferLen = copyBufferLen;
	this->freeFile = freeFile;
}
AssetModifierFromFile::~AssetModifierFromFile()
{
	if (freeFile)
		fclose(this->pFile);
}
AssetsReplacementType AssetModifierFromFile::GetType() const
{
	return AssetsReplacement_AddOrModify;
}

QWORD AssetModifierFromFile::GetSize() const{return size;}
QWORD AssetModifierFromFile::Write(QWORD pos, IAssetsWriter *pWriter)
{
	if (!size)
		return pos;
	uint8_t stackBuffer[256];
	void *copyBuffer = stackBuffer;
	size_t curBufferLen = 256;
	if (copyBufferLen > 256)
	{
		copyBuffer = malloc(copyBufferLen);
		if (copyBuffer == NULL)
			copyBuffer = stackBuffer;
		else
			curBufferLen = copyBufferLen;
	}
	QWORD remainingBytes = size; fseek(pFile, (long)this->offs, SEEK_SET);
	while (remainingBytes > curBufferLen)
	{
		fread(copyBuffer, curBufferLen, 1, pFile);
		remainingBytes -= curBufferLen;
		pWriter->Write(pos, curBufferLen, copyBuffer);
		pos += curBufferLen;
	}
	if (remainingBytes > 0)
	{
		fread(copyBuffer, (size_t)remainingBytes, 1, pFile);
		pWriter->Write(pos, remainingBytes, copyBuffer);
		pos += remainingBytes;
	}
	if (curBufferLen > 256)
		free(copyBuffer);
	return pos;
}
QWORD AssetModifierFromFile::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
{
	//store it the as a modifier from memory (no difference)
	uint16_t replacerType = AssetsReplacer_AssetModifierFromMemory;
	pos += pWriter->Write(pos, 2, &replacerType);
	uint8_t fileVersion = 1;
	pos += pWriter->Write(pos, 1, &fileVersion);
	pos = AssetEntryModifierBase::WriteReplacer(pos, pWriter);
	pos += pWriter->Write(pos, 8, &this->size);
	pos = Write(pos, pWriter);
	return pos;
}
#pragma endregion
#pragma region AssetsDependenciesReplacerImpl
AssetsDependenciesReplacerImpl::AssetsDependenciesReplacerImpl(uint32_t fileID, std::vector<AssetsFileDependency> _dependencies)
	: fileID(fileID), dependencies(std::move(_dependencies))
{}
AssetsReplacementType AssetsDependenciesReplacerImpl::GetType() const
{
	return AssetsReplacement_Dependencies;
}
uint32_t AssetsDependenciesReplacerImpl::GetFileID() const
{
	return fileID;
}
const std::vector<AssetsFileDependency>& AssetsDependenciesReplacerImpl::GetDependencies() const
{
	return dependencies;
}
QWORD AssetsDependenciesReplacerImpl::WriteReplacer(QWORD pos, IAssetsWriter* pWriter)
{
	//store it the as a modifier from memory (no difference)
	uint16_t replacerType = AssetsReplacer_Dependencies;
	pos += pWriter->Write(pos, 2, &replacerType);
	uint8_t fileVersion = 0;
	pos += pWriter->Write(pos, 1, &fileVersion);
	uint32_t writeFileID = 0;
	pos += pWriter->Write(pos, 4, &writeFileID);

	uint32_t dependenciesCount = (uint32_t)dependencies.size();
	pos += pWriter->Write(pos, 4, &dependenciesCount);
	for (uint32_t i = 0; i < dependenciesCount; ++i)
	{
		pos = dependencies[i].Write(pos, pWriter, 13, false);
	}
	return pos;
}
#pragma endregion

void FreeMemoryResource_ReadAssetsReplacer(void *pResource)
{
	if (pResource)
		free(pResource);
}
static AssetsReplacer *ReadAssetsEntryReplacer(QWORD& pos, IAssetsReader* pReader, std::shared_ptr<IAssetsReader> ref_pReader, bool prefReplacerInMemory,
	uint16_t replacerType)
{
	uint8_t replacerFileVersion = 0;
	pos += pReader->Read(pos, 1, &replacerFileVersion);

	uint8_t replacerBaseVersion = 0;
	if (replacerFileVersion >= 1)
	{
		//See AssetsReplacerBase::WriteReplacer
		pos += pReader->Read(pos, 1, &replacerBaseVersion);
		if (replacerBaseVersion > 1)
			return NULL;
	}
	uint32_t fileID = 0; QWORD pathID = 0; int classID = 0; uint16_t monoScriptIndex = 0;
	std::vector<AssetPPtr> preloadDependencies;
	pos += pReader->Read(pos, 4, &fileID);
	pos += pReader->Read(pos, 8, &pathID);
	pos += pReader->Read(pos, 4, &classID);
	pos += pReader->Read(pos, 2, &monoScriptIndex);
	if (replacerBaseVersion >= 1)
	{
		uint32_t count = 0;
		pos += pReader->Read(pos, 4, &count);
		preloadDependencies.resize(count);
		for (uint32_t i = 0; i < count; i++)
		{
			pos += pReader->Read(pos, 4, &preloadDependencies[i].fileID);
			pos += pReader->Read(pos, 8, &preloadDependencies[i].pathID);
		}
	}
	switch (replacerType)
	{
	case AssetsReplacer_AssetRemover:
		{
			if (replacerFileVersion > 1)
				return NULL;
			AssetsEntryReplacer *pReplacer = new AssetRemover(fileID, pathID, classID, monoScriptIndex);
			pReplacer->SetPreloadDependencies(preloadDependencies.data(), (uint32_t)preloadDependencies.size());
			return pReplacer;
		}
	case AssetsReplacer_AssetModifierFromReader:
	case AssetsReplacer_AssetModifierFromMemory:
	case AssetsReplacer_AssetModifierFromFile:
		{
			if (replacerFileVersion > 1)
				return NULL;

			Hash128 propertiesHash = Hash128(); uint8_t hasPropertiesHash = 0;
			Hash128 scriptIDHash = Hash128(); uint8_t hasScriptIDHash = 0;
			uint8_t hasTypeInfo = 0;
			std::shared_ptr<ClassDatabaseFile> pClassFile; ClassDatabaseType *pClassType = nullptr;
			if (replacerFileVersion >= 1)
			{
				//See AssetModifierBase::WriteReplacer
				uint8_t modifierBaseVersion = 0;
				pos += pReader->Read(pos, 1, &modifierBaseVersion);
				if (modifierBaseVersion > 0)
					return NULL;

				pos += pReader->Read(pos, 1, &hasPropertiesHash);
				if (hasPropertiesHash)
					pos += pReader->Read(pos, 16, propertiesHash.bValue);

				pos += pReader->Read(pos, 1, &hasScriptIDHash);
				if (hasScriptIDHash)
					pos += pReader->Read(pos, 16, scriptIDHash.bValue);
				
				pos += pReader->Read(pos, 1, &hasTypeInfo);
				if (hasTypeInfo)
				{
					pClassFile = std::make_shared<ClassDatabaseFile>();
					pos = pClassFile->Read(pReader, pos);
					if (pClassFile->IsValid() && pClassFile->classes.size() == 1)
					{
						pClassType = &pClassFile->classes[0];
					}
					else
					{
						pClassFile.reset();
						hasTypeInfo = 0;
					}
				}
			}

			QWORD size = 0;
			pos += pReader->Read(pos, 8, &size);
			AssetsEntryReplacer *pReplacer = NULL;
			if (prefReplacerInMemory)
			{
				void *pMem = malloc(size);
				if (pMem)
				{
					QWORD actualRead = pReader->Read(pos, size, pMem);
					memset(&((uint8_t*)pMem)[actualRead], 0, size - actualRead);
					pReplacer = new AssetModifierFromMemory(fileID, pathID, classID, monoScriptIndex, pMem, size, 
						FreeMemoryResource_ReadAssetsReplacer);
				}
			}
			if (!pReplacer)
				pReplacer =
					new AssetModifierFromReader(fileID, pathID, classID, monoScriptIndex, pReader, size, pos, 0, false, ref_pReader);
			if (hasPropertiesHash)
				pReplacer->SetPropertiesHash(propertiesHash);
			if (hasScriptIDHash)
				pReplacer->SetScriptIDHash(scriptIDHash);
			if (pClassFile && pClassType)
				pReplacer->SetTypeInfo(std::move(pClassFile), pClassType);
			pReplacer->SetPreloadDependencies(preloadDependencies.data(), (uint32_t)preloadDependencies.size());
			pos += size;
			return pReplacer;
		}
	}
	return nullptr;
}
static AssetsReplacer *ReadAssetsReplacer(QWORD &pos, IAssetsReader *pReader, std::shared_ptr<IAssetsReader> ref_pReader, bool prefReplacerInMemory)
{
	uint16_t replacerType = 0xFFFF;
	pos += pReader->Read(pos, 2, &replacerType);
	switch (replacerType)
	{
	case AssetsReplacer_AssetRemover:
	case AssetsReplacer_AssetModifierFromReader:
	case AssetsReplacer_AssetModifierFromMemory:
	case AssetsReplacer_AssetModifierFromFile:
		return ReadAssetsEntryReplacer(pos, pReader, std::move(ref_pReader), prefReplacerInMemory, replacerType);
	case AssetsReplacer_Dependencies:
		{
			uint8_t fileVersion = 0;
			pos += pReader->Read(pos, 1, &fileVersion);
			if (fileVersion > 0)
				return nullptr; //Unsupported
			uint32_t fileID = 0;
			pos += pReader->Read(pos, 4, &fileID);

			uint32_t dependenciesCount = 0;
			pos += pReader->Read(pos, 4, &dependenciesCount);
			std::vector<AssetsFileDependency> dependencies(dependenciesCount);
			for (uint32_t i = 0; i < dependenciesCount; ++i)
			{
				pos = dependencies[i].Read(pos, pReader, 13, false);
			}
			return new AssetsDependenciesReplacerImpl(fileID, std::move(dependencies));
		}
	}
	return nullptr;
}
ASSETSTOOLS_API AssetsReplacer *ReadAssetsReplacer(QWORD &pos, IAssetsReader *pReader, bool prefReplacerInMemory)
{
	return ReadAssetsReplacer(pos, pReader, nullptr, prefReplacerInMemory);
}
ASSETSTOOLS_API AssetsReplacer *ReadAssetsReplacer(QWORD &pos, std::shared_ptr<IAssetsReader> pReader, bool prefReplacerInMemory)
{
	IAssetsReader *_pReader = pReader.get();
	return ReadAssetsReplacer(pos, _pReader, std::move(pReader), prefReplacerInMemory);
}
ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetRemover(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex)
{
	return new AssetRemover(fileID, pathID, classID, monoScriptIndex);
}

ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromReader(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex, 
		IAssetsReader *pReader, QWORD size, QWORD readerPos, 
		size_t copyBufferLen, bool freeReader)
{
	return new AssetModifierFromReader(fileID, pathID, classID, monoScriptIndex, pReader, size, readerPos, copyBufferLen, freeReader);
}
ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromMemory(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex, 
		void *buffer, size_t size, cbFreeMemoryResource freeResourceCallback)
{
	return new AssetModifierFromMemory(fileID, pathID, classID, monoScriptIndex, buffer, size, freeResourceCallback);
}
ASSETSTOOLS_API AssetsEntryReplacer *MakeAssetModifierFromFile(uint32_t fileID, QWORD pathID, int classID, uint16_t monoScriptIndex,
		FILE *pFile, QWORD offs, QWORD size, size_t copyBufferLen, bool freeFile)
{
	return new AssetModifierFromFile(fileID, pathID, classID, monoScriptIndex, pFile, offs, size, copyBufferLen, freeFile);
}
ASSETSTOOLS_API AssetsDependenciesReplacer *MakeAssetsDependenciesReplacer(uint32_t fileID, std::vector<AssetsFileDependency> dependencies)
{
	return new AssetsDependenciesReplacerImpl(fileID, std::move(dependencies));
}
ASSETSTOOLS_API void FreeAssetsReplacer(AssetsReplacer *pReplacer)
{
	delete pReplacer;
}