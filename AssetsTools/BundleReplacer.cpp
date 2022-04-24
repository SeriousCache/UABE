#include "stdafx.h"
#include "../AssetsTools/BundleReplacer.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "InternalBundleReplacer.h"
#include <unordered_map>

//class BundleEntryRemover : public BundleReplacer
//{
	BundleEntryRemover::BundleEntryRemover(const char *name, unsigned int bundleListIndex)
	{
		this->bundleListIndex = bundleListIndex;
		if (name)
		{
			size_t nameLen = strlen(name);
			originalEntryName = new char[nameLen + 1];
			memcpy(originalEntryName, name, nameLen+1);
		}
		else
			originalEntryName = NULL;
	}
	BundleReplacementType BundleEntryRemover::GetType() { return BundleReplacement_Remove; }
	BundleEntryRemover::~BundleEntryRemover()
	{
		if (originalEntryName)
			delete[] originalEntryName;
	}
		
	unsigned int BundleEntryRemover::GetBundleListIndex() { return bundleListIndex; }

	const char *BundleEntryRemover::GetOriginalEntryName() { return originalEntryName; }
	const char *BundleEntryRemover::GetEntryName() { return originalEntryName; }

	QWORD BundleEntryRemover::GetSize() { return 0; }

	bool BundleEntryRemover::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta){return true;}
	void BundleEntryRemover::Uninit(){}

	QWORD BundleEntryRemover::Write(QWORD pos, IAssetsWriter *pWriter) {return pos;}
	QWORD BundleEntryRemover::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryRemover;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		return pos;
	}

	bool BundleEntryRemover::HasSerializedData() { return false; }
	
	bool BundleEntryRemover::RequiresEntryReader() { return false; }
//};

//class BundleEntryRenamer : public BundleReplacer
//{
	BundleEntryRenamer::BundleEntryRenamer(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData)
	{
		this->bundleListIndex = bundleListIndex;
		if (oldName)
		{
			size_t nameLen = strlen(oldName);
			originalEntryName = new char[nameLen + 1];
			memcpy(originalEntryName, oldName, nameLen+1);
		}
		else
			originalEntryName = NULL;
		if (newName)
		{
			size_t nameLen = strlen(newName);
			newEntryName = new char[nameLen + 1];
			memcpy(newEntryName, newName, nameLen+1);
		}
		else
			newEntryName = originalEntryName;
		this->hasSerializedData = hasSerializedData;
	}
	BundleReplacementType BundleEntryRenamer::GetType() { return BundleReplacement_Rename; }
	BundleEntryRenamer::~BundleEntryRenamer()
	{
		if (newEntryName && (newEntryName != originalEntryName))
			delete[] newEntryName;
		if (originalEntryName)
			delete[] originalEntryName;
	}
		
	unsigned int BundleEntryRenamer::GetBundleListIndex() { return bundleListIndex; }

	const char *BundleEntryRenamer::GetOriginalEntryName() { return originalEntryName; }
	const char *BundleEntryRenamer::GetEntryName() { return newEntryName; }

	QWORD BundleEntryRenamer::GetSize() { return (QWORD)-1; }

	bool BundleEntryRenamer::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta){return true;}
	void BundleEntryRenamer::Uninit(){}

	QWORD BundleEntryRenamer::Write(QWORD pos, IAssetsWriter *pWriter) {return pos;}
	QWORD BundleEntryRenamer::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryRenamer;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		
		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		uint8_t temp = hasSerializedData ? 1 : 0;
		pos += pWriter->Write(pos, 1, &temp);
		return pos;
	}

	bool BundleEntryRenamer::HasSerializedData() { return hasSerializedData; }
	
	bool BundleEntryRenamer::RequiresEntryReader() { return false; }
//};

//class BundleEntryModifier : public BundleEntryRenamer
//{
	BundleEntryModifier::BundleEntryModifier(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData, 
		std::shared_ptr<IAssetsReader> pReader, QWORD size, QWORD readerPos, 
		size_t copyBufferLen)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, hasSerializedData)
	{
		this->pReader = std::move(pReader);
		this->size = size;
		this->readerPos = readerPos;
		this->copyBufferLen = copyBufferLen;
	}
	BundleReplacementType BundleEntryModifier::GetType() { return BundleReplacement_AddOrModify; }
	BundleEntryModifier::~BundleEntryModifier()
	{}

	QWORD BundleEntryModifier::GetSize() { return size; }

	bool BundleEntryModifier::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta){return true;}
	void BundleEntryModifier::Uninit(){}

	QWORD BundleEntryModifier::Write(QWORD writerPos, IAssetsWriter *pWriter)
	{
		QWORD curReaderPos = readerPos;
		uint8_t stackCopyBuffer[1024]; size_t actualBufferLen = copyBufferLen;
		void *pCopyBuffer = NULL;
		if (copyBufferLen)
			pCopyBuffer = malloc(copyBufferLen);
		if (!pCopyBuffer)
		{
			pCopyBuffer = stackCopyBuffer;
			actualBufferLen = 1024;
		}

		QWORD remainingSize = size;
		while (remainingSize)
		{
			size_t curSize = (remainingSize > actualBufferLen) ? actualBufferLen : remainingSize;
			QWORD len = pReader->Read(curReaderPos, curSize, pCopyBuffer);
			curReaderPos += len;
			writerPos += pWriter->Write(writerPos, len, pCopyBuffer);
			remainingSize -= len;
			if (!len)
				break; //read error
		}

		if (pCopyBuffer != stackCopyBuffer)
			free(pCopyBuffer);
		return writerPos;
	}
	QWORD BundleEntryModifier::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryModifier;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		
		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		uint8_t temp = hasSerializedData ? 1 : 0;
		pos += pWriter->Write(pos, 1, &temp);

		pos += pWriter->Write(pos, 8, &size);
		pos = Write(pos, pWriter);
		return pos;
	}
//};

//class BundleEntryModifierFromMem : public BundleEntryRenamer
//{
	BundleEntryModifierFromMem::BundleEntryModifierFromMem(const char *oldName, const char *newName, unsigned int bundleListIndex, bool hasSerializedData, 
		void *pMem, size_t size, cbFreeMemoryResource freeResourceCallback)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, hasSerializedData)
	{
		this->pMem = pMem;
		this->size = size;
		this->freeResourceCallback = freeResourceCallback;
	}
	BundleReplacementType BundleEntryModifierFromMem::GetType() { return BundleReplacement_AddOrModify; }
	BundleEntryModifierFromMem::~BundleEntryModifierFromMem()
	{
		if (pMem && freeResourceCallback)
		{
			freeResourceCallback(pMem);
			pMem = NULL;
			freeResourceCallback = NULL;
			size = 0;
		}
	}

	QWORD BundleEntryModifierFromMem::GetSize() { return size; }

	bool BundleEntryModifierFromMem::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta){return true;}
	void BundleEntryModifierFromMem::Uninit(){}

	QWORD BundleEntryModifierFromMem::Write(QWORD writerPos, IAssetsWriter *pWriter)
	{
		return writerPos + pWriter->Write(writerPos, size, pMem);
	}
	QWORD BundleEntryModifierFromMem::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryModifierFromMem;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		
		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		uint8_t temp = hasSerializedData ? 1 : 0;
		pos += pWriter->Write(pos, 1, &temp);

		QWORD qwSize = size;
		pos += pWriter->Write(pos, 8, &qwSize);
		pos = Write(pos, pWriter);
		return pos;
	}
//};

//class BundleEntryModifierFromAssets : public BundleEntryRenamer
//{
	BundleEntryModifierFromAssets::BundleEntryModifierFromAssets(const char *oldName, const char *newName, unsigned int bundleListIndex, 
		AssetsFile *pAssetsFile, AssetsReplacer **pReplacers, size_t replacerCount, uint32_t fileId)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, true)
	{
		this->pAssetsFile = pAssetsFile;
		this->pReplacers.assign(&pReplacers[0], &pReplacers[replacerCount]);
		this->fileId = fileId;
		this->freeAssetsFile = false;
		this->typeMeta = NULL;
	}
	BundleEntryModifierFromAssets::BundleEntryModifierFromAssets(const char *oldName, const char *newName, unsigned int bundleListIndex, 
		std::shared_ptr<ClassDatabaseFile> typeMeta, std::vector<std::shared_ptr<AssetsReplacer>> pReplacers, uint32_t fileId)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, true)
	{
		this->pAssetsFile = nullptr;
		this->typeMeta = typeMeta.get();
		this->typeMeta_shared = std::move(typeMeta);
		this->pReplacers.resize(pReplacers.size());
		for (size_t i = 0; i < pReplacers.size(); i++)
			this->pReplacers[i] = pReplacers[i].get();
		this->pReplacers_shared = std::move(pReplacers);
		this->freeAssetsFile = false;
		this->fileId = fileId;
	}
	BundleReplacementType BundleEntryModifierFromAssets::GetType() { return BundleReplacement_AddOrModify; }
	BundleEntryModifierFromAssets::~BundleEntryModifierFromAssets()
	{
		Uninit();
	}

	//Relatively precise; returns all raw asset sizes, the size plus alignment of the file table, the header size and the dependencies list size.
	//Does not count the TypeTree size and ignores dependency list replacers.
	QWORD BundleEntryModifierFromAssets::GetSize()
	{
		if (!pAssetsFile)
			return 0;
		QWORD ret = pAssetsFile->header.GetSizeBytes();
		ret += 5;
		AssetsFileDependencyList dependencyList = pAssetsFile->dependencies;
			
		AssetsFileTable fileTable = AssetsFileTable(pAssetsFile);
		for (size_t i = 0; i < fileTable.assetFileInfoCount; i++)
		{
			QWORD pathId = fileTable.pAssetFileInfo[i].index;
			QWORD fileSize = fileTable.pAssetFileInfo[i].curFileSize;
			bool isModified = false, isDeleted = false;
			for (size_t k = pReplacers.size(); k > 0; k--)
			{
				if (pReplacers[k-1]->GetType() == AssetsReplacement_Dependencies)
				{
					AssetsDependenciesReplacer* pDepReplacer =
						reinterpret_cast<AssetsDependenciesReplacer*>(pReplacers[k - 1]);
					const std::vector<AssetsFileDependency> &dependencies = pDepReplacer->GetDependencies();
					dependencyList.pDependencies = const_cast<AssetsFileDependency*>(dependencies.data());
					dependencyList.dependencyCount = (uint32_t)dependencies.size();
				}
				if (pReplacers[k-1]->GetType() != AssetsReplacement_AddOrModify
					&& pReplacers[k-1]->GetType() != AssetsReplacement_Remove)
					continue;
				AssetsEntryReplacer *pReplacer = reinterpret_cast<AssetsEntryReplacer*>(pReplacers[k-1]);
				if (pathId == pReplacer->GetPathID())
				{
					if (!isModified && (pReplacer->GetType() == AssetsReplacement_AddOrModify))
					{
						fileSize = pReplacer->GetSize();
						isModified = true;
						continue;
					}
					if (pReplacer->GetType() == AssetsReplacement_Remove)
					{
						isDeleted = true;
						break;
					}
				}
			}
			if (!isDeleted)
				ret += ((fileSize + 3) & (~3)) + ((fileTable.pAssetFileInfo[i].GetSize(pAssetsFile->header.format) + 3) & (~3));
		}

		for (uint32_t i = 0; i < dependencyList.dependencyCount; i++)
		{
			ret += strlen(dependencyList.pDependencies[i].assetPath) + 1;
			ret += strlen(dependencyList.pDependencies[i].bufferedPath) + 1;
			ret += 20; //GUID and type
		}

		return ret;
	}

	bool BundleEntryModifierFromAssets::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta)
	{
		Uninit();
		if (pAssetsFile)
			return true;
		if (pEntryReader == nullptr)
			return false;
		pEntryReader = Create_AssetsReaderFromReaderRange(pEntryReader, entryPos, entrySize);
		pAssetsFile = new AssetsFile(pEntryReader);
		if (!pAssetsFile->VerifyAssetsFile())
		{
			Free_AssetsReader(pEntryReader);
			delete pAssetsFile;
			pAssetsFile = NULL;
			return false;
		}
		freeAssetsFile = true;
		if (this->typeMeta == nullptr)
			this->typeMeta = typeMeta;
		return true;
	}
	void BundleEntryModifierFromAssets::Uninit()
	{
		if (pAssetsFile && freeAssetsFile)
		{
			Free_AssetsReader(pAssetsFile->pReader);
			delete pAssetsFile;
			pAssetsFile = NULL;
			freeAssetsFile = false;
		}
	}

	QWORD BundleEntryModifierFromAssets::Write(QWORD writerPos, IAssetsWriter *pWriter)
	{
		return pAssetsFile->Write(pWriter, writerPos, pReplacers.data(), pReplacers.size(), fileId, typeMeta);
	}
	QWORD BundleEntryModifierFromAssets::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryModifierFromAssets;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		
		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		bool temp = true;
		pos += pWriter->Write(pos, 1, &temp);

		QWORD ullReplacerCount = pReplacers.size();
		pos += pWriter->Write(pos, 8, &ullReplacerCount);
		for (size_t i = 0; i < pReplacers.size(); i++)
		{
			pos = pReplacers[i]->WriteReplacer(pos, pWriter);
		}
		return pos;
	}
	AssetsReplacer **BundleEntryModifierFromAssets::GetReplacers(size_t &count)
	{
		count = pReplacers.size();
		return pReplacers.data();
	}
	AssetsFile *BundleEntryModifierFromAssets::GetAssignedAssetsFile()
	{
		return pAssetsFile;
	}
	uint32_t BundleEntryModifierFromAssets::GetFileID()
	{
		return fileId;
	}
	
	bool BundleEntryModifierFromAssets::RequiresEntryReader() { return true; }
//};

//class BundleEntryModifierFromBundle : public BundleEntryRenamer
//{
	BundleEntryModifierFromBundle::BundleEntryModifierFromBundle(const char *oldName, const char *newName, unsigned int bundleListIndex, 
			BundleReplacer **pReplacers, size_t replacerCount)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, true)
	{
		this->pBundleFile = nullptr;
		this->pBundleReader = nullptr;
		this->pReplacers.assign(&pReplacers[0], &pReplacers[replacerCount]);
		this->freeBundleFile = false;
		this->typeMeta = nullptr;
	}
	BundleEntryModifierFromBundle::BundleEntryModifierFromBundle(const char *oldName, const char *newName, unsigned int bundleListIndex,
			std::vector<std::unique_ptr<BundleReplacer>> pReplacers)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, true)
	{
		this->pBundleFile = nullptr;
		this->pBundleReader = nullptr;
		this->pReplacers.resize(pReplacers.size());
		for (size_t i = 0; i < pReplacers.size(); i++)
			this->pReplacers[i] = pReplacers[i].get();
		this->pReplacers_unique = std::move(pReplacers);
		this->freeBundleFile = false;
		this->typeMeta = NULL;
	}
	BundleReplacementType BundleEntryModifierFromBundle::GetType() { return BundleReplacement_AddOrModify; }
	BundleEntryModifierFromBundle::~BundleEntryModifierFromBundle()
	{
		Uninit();
	}

	//Returns a rough estimate of the new file size.
	QWORD BundleEntryModifierFromBundle::GetSize()
	{
		if (!pBundleFile)
			return 0;
		QWORD ret = 0;
		uint32_t directorySize = 0;
		if (pBundleFile->bundleHeader3.fileVersion == 3)
		{
			ret += 128 + pBundleFile->bundleHeader3.blockCount * 8;
			directorySize = (pBundleFile->assetsLists3 != nullptr) ? pBundleFile->assetsLists3->count : 0;
		}
		else if (pBundleFile->bundleHeader6.fileVersion >= 6)
		{
			ret += pBundleFile->bundleHeader6.GetFileDataOffset();
			if ((pBundleFile->bundleHeader6.flags & 0x80) != 0)
				ret += pBundleFile->bundleHeader6.decompressedSize;
			directorySize = (pBundleFile->bundleInf6 != nullptr) ? pBundleFile->bundleInf6->directoryCount : 0;
		}
		std::vector<BundleReplacer*> newReplacers;
		std::vector<BundleReplacer*> directoryToReplacerMapping(directorySize);
		for (size_t i = 0; i < this->pReplacers.size(); i++)
		{
			unsigned int listIndex = this->pReplacers[i]->GetBundleListIndex();
			if (listIndex < directorySize)
				directoryToReplacerMapping[listIndex] = this->pReplacers[i];
			else if (this->pReplacers[i]->GetType() == BundleReplacement_AddOrModify)
				newReplacers.push_back(this->pReplacers[i]);
		}

		for (uint32_t i = 0; i < directorySize; i++)
		{
			QWORD entryByteLen = 0;
			const char *newEntryName = nullptr;
			if (pBundleFile->bundleHeader3.fileVersion == 3)
			{
				entryByteLen = pBundleFile->assetsLists3->ppEntries[i]->length;
				newEntryName = pBundleFile->assetsLists3->ppEntries[i]->name;
			}
			else if (pBundleFile->bundleHeader3.fileVersion >= 6)
			{
				entryByteLen = pBundleFile->bundleInf6->dirInf[i].decompressedSize;
				newEntryName = pBundleFile->bundleInf6->dirInf[i].name;
			}
			if (directoryToReplacerMapping[i] == nullptr)
			{ }
			else if (directoryToReplacerMapping[i]->GetType() == BundleReplacement_AddOrModify)
			{
				QWORD entrySizeVal = directoryToReplacerMapping[i]->GetSize();
				if (entrySizeVal != (QWORD)-1)
					entryByteLen = entrySizeVal;
			}
			else if (directoryToReplacerMapping[i]->GetType() == BundleReplacement_Remove)
				continue;
			
			if (directoryToReplacerMapping[i] != nullptr)
			{
				const char *replacerNewName = directoryToReplacerMapping[i]->GetEntryName();
				if (replacerNewName != nullptr)
					newEntryName = replacerNewName;
			}
			ret += ((newEntryName == nullptr) ? 0 : strlen(newEntryName)) + 20;
			ret += entryByteLen + 16;
		}
		for (size_t i = 0; i < newReplacers.size(); i++)
		{
			const char *newEntryName = newReplacers[i]->GetEntryName();
			QWORD entryByteLen = newReplacers[i]->GetSize();
			if (entryByteLen == (QWORD)-1) 
				entryByteLen = 0;
			ret += ((newEntryName == nullptr) ? 0 : strlen(newEntryName)) + 20;
			ret += entryByteLen + 16;
		}

		return ret;
	}

	bool BundleEntryModifierFromBundle::Init(AssetBundleFile *pBundleFile,
		IAssetsReader *pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile *typeMeta)
	{
		Uninit();
		if (pEntryReader == nullptr)
			return false;
		this->pBundleReader = Create_AssetsReaderFromReaderRange(pEntryReader, entryPos, entrySize);
		this->pBundleFile = new AssetBundleFile();
		if (!this->pBundleFile->Read(this->pBundleReader) || 
			(this->pBundleFile->bundleHeader3.fileVersion == 3 && this->pBundleFile->assetsLists3 == nullptr) ||
			(this->pBundleFile->bundleHeader6.fileVersion >= 6 && this->pBundleFile->bundleInf6 == nullptr) ||
			(this->pBundleFile->bundleHeader6.fileVersion < 6 && this->pBundleFile->bundleHeader3.fileVersion != 3))
		{
			Free_AssetsReader(this->pBundleReader);
			this->pBundleReader = nullptr;
			delete this->pBundleFile;
			this->pBundleFile = nullptr;
			return false;
		}
		this->freeBundleFile = true;
		this->typeMeta = typeMeta;
		//Call Init for all child bundle replacers.
		for (size_t i = 0; i < this->pReplacers.size(); i++)
		{
			this->pReplacers[i]->Uninit();
			unsigned int bundleListIdx = this->pReplacers[i]->GetBundleListIndex();
			uint64_t bundleEntryPos = 0, bundleEntryLen = 0;
			if (bundleListIdx == (unsigned int)-1)
			{
				const char *entryName = this->pReplacers[i]->GetOriginalEntryName();
				if (entryName == nullptr)
					continue;
				if (this->pBundleFile->bundleHeader3.fileVersion == 3)
				{
					for (uint32_t iDirEntry = 0; iDirEntry < this->pBundleFile->assetsLists3->count; iDirEntry++)
					{
						if (!strcmp(entryName, this->pBundleFile->assetsLists3->ppEntries[iDirEntry]->name))
						{
							bundleListIdx = iDirEntry;
							break;
						}
					}
				}
				else if (this->pBundleFile->bundleHeader6.fileVersion >= 6)
				{
					for (uint32_t iDirEntry = 0; iDirEntry < this->pBundleFile->bundleInf6->directoryCount; iDirEntry++)
					{
						if (!strcmp(entryName, this->pBundleFile->bundleInf6->dirInf[iDirEntry].name))
						{
							bundleListIdx = iDirEntry;
							break;
						}
					}
				}
				if (bundleListIdx == (unsigned int)-1)
					continue;
			}
			if (this->pBundleFile->bundleHeader3.fileVersion == 3)
			{
				bundleEntryPos = this->pBundleFile->assetsLists3->ppEntries[bundleListIdx]->GetAbsolutePos(pBundleFile);
				bundleEntryLen = this->pBundleFile->assetsLists3->ppEntries[bundleListIdx]->length;
			}
			else if (this->pBundleFile->bundleHeader6.fileVersion >= 6)
			{
				bundleEntryPos = this->pBundleFile->bundleInf6->dirInf[bundleListIdx].GetAbsolutePos(pBundleFile);
				bundleEntryLen = this->pBundleFile->bundleInf6->dirInf[bundleListIdx].decompressedSize;
			}
			else
			{
				assert(false);
				continue;
			}
			this->pReplacers[i]->Init(this->pBundleFile, this->pBundleReader, bundleEntryPos, bundleEntryLen, typeMeta);
		}
		return true;
	}
	void BundleEntryModifierFromBundle::Uninit()
	{
		for (size_t i = 0; i < this->pReplacers.size(); i++)
			this->pReplacers[i]->Uninit();
		if (this->pBundleFile && freeBundleFile)
		{
			Free_AssetsReader(this->pBundleReader);
			delete this->pBundleFile;
			freeBundleFile = false;
		}
		this->pBundleReader = nullptr;
		this->pBundleFile = nullptr;
	}

	QWORD BundleEntryModifierFromBundle::Write(QWORD writerPos, IAssetsWriter *pWriter)
	{
		IAssetsWriterToWriterOffset *pWriterWrapper = Create_AssetsWriterToWriterOffset(pWriter, writerPos);
		pBundleFile->Write(this->pBundleReader, pWriterWrapper, this->pReplacers.data(), this->pReplacers.size(), nullptr, this->typeMeta);
		QWORD ret = writerPos;
		pWriterWrapper->Tell(ret);
		Free_AssetsWriter(pWriterWrapper);
		return ret + writerPos;
	}
	QWORD BundleEntryModifierFromBundle::WriteReplacer(QWORD pos, IAssetsWriter *pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryModifierFromBundle;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}
		
		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		bool temp = true;
		pos += pWriter->Write(pos, 1, &temp);

		QWORD ullReplacerCount = this->pReplacers.size();
		pos += pWriter->Write(pos, 8, &ullReplacerCount);
		for (size_t i = 0; i < this->pReplacers.size(); i++)
		{
			pos = pReplacers[i]->WriteReplacer(pos, pWriter);
		}
		return pos;
	}
	BundleReplacer **BundleEntryModifierFromBundle::GetReplacers(size_t &count)
	{
		count = this->pReplacers.size();
		return this->pReplacers.data();
	}
	
	bool BundleEntryModifierFromBundle::RequiresEntryReader() { return true; }
//};

//class BundleEntryModifierByResources : public BundleEntryRenamer
//{
	BundleEntryModifierByResources::BundleEntryModifierByResources(const char* oldName, const char* newName, unsigned int bundleListIndex,
		std::vector<ReplacedResourceDesc> _resources,
		size_t copyBufferLen)
		: BundleEntryRenamer(oldName, newName, bundleListIndex, false)
	{
		this->resources = std::move(_resources);
		this->copyBufferLen = copyBufferLen;
	}
	BundleReplacementType BundleEntryModifierByResources::GetType() { return BundleReplacement_AddOrModify; }
	BundleEntryModifierByResources::~BundleEntryModifierByResources()
	{}

	QWORD BundleEntryModifierByResources::GetSize() { return getSize(); }

	bool BundleEntryModifierByResources::Init(AssetBundleFile* pBundleFile,
		IAssetsReader* pEntryReader,
		QWORD entryPos, QWORD entrySize,
		ClassDatabaseFile* typeMeta)
	{
		this->pEntryReader = pEntryReader;
		this->entryPos = entryPos;
		this->entrySize = entrySize;
		return (pEntryReader != nullptr || !RequiresEntryReader());
	}
	void BundleEntryModifierByResources::Uninit()
	{
		this->pEntryReader = nullptr;
	}
	bool BundleEntryModifierByResources::RequiresEntryReader()
	{
		for (size_t i = 0; i < resources.size(); ++i)
		{
			if (resources[i].fromOriginalFile)
				return true;
		}
		return false;
	}

	QWORD BundleEntryModifierByResources::Write(QWORD writerPos, IAssetsWriter* pWriter)
	{
		std::vector<uint8_t> zeroBuffer;
		QWORD writerPos_pre = writerPos;
		for (size_t i = 0; i < resources.size(); ++i)
		{
			IAssetsReader* pReader = nullptr;
			QWORD readerPos = 0, readerRange = 0;
			if (resources[i].reader != nullptr)
			{
				pReader = resources[i].reader.get();
				readerPos = resources[i].inRangeBegin;
				readerRange = resources[i].rangeSize;
			}
			else if (resources[i].fromOriginalFile)
			{
				pReader = pEntryReader;
				readerPos = entryPos + resources[i].inRangeBegin;
				readerRange = resources[i].rangeSize;
				if (resources[i].inRangeBegin + readerRange > entrySize)
				{
					if (resources[i].inRangeBegin > entrySize)
						readerRange = 0;
					else
						readerRange = entrySize - resources[i].inRangeBegin;
				}
			}
			if (readerRange > 0 && pReader != nullptr)
			{
				BundleEntryModifier tmpCopier(nullptr, nullptr, (unsigned int)-1, false,
					std::shared_ptr<IAssetsReader>(pReader, [](IAssetsReader*) {}),
					readerRange, readerPos, copyBufferLen);
				writerPos = tmpCopier.Write(writerPos, pWriter);
			}
			QWORD remaining = (resources[i].outRangeBegin + resources[i].rangeSize) - (writerPos - writerPos_pre);
			if (resources.size() > (i + 1) && resources[i + 1].outRangeBegin > (writerPos - writerPos_pre))
			{
				remaining = resources[i + 1].outRangeBegin - (writerPos - writerPos_pre);
			}
			zeroBuffer.resize((size_t)std::min<QWORD>(copyBufferLen, remaining), 0);
			while (remaining > 0)
			{
				size_t curCopyDepth = (size_t)std::min<QWORD>(copyBufferLen, remaining);
				QWORD written = pWriter->Write(writerPos, curCopyDepth, zeroBuffer.data());
				if (written == 0)
					return writerPos;
				writerPos += written;
				assert(written <= remaining);
				remaining -= written;
			}
		}
		return writerPos;
	}
	QWORD BundleEntryModifierByResources::WriteReplacer(QWORD pos, IAssetsWriter* pWriter)
	{
		uint16_t replacerType = BundleReplacer_BundleEntryModifierByResources;
		pos += pWriter->Write(pos, 2, &replacerType);
		uint8_t fileVersion = 1;
		pos += pWriter->Write(pos, 1, &fileVersion);

		uint8_t hasOriginalName = (this->originalEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasOriginalName);
		if (hasOriginalName)
		{
			uint16_t originalNameLen = (uint16_t)strlen(this->originalEntryName);
			pos += pWriter->Write(pos, 2, &originalNameLen);
			pos += pWriter->Write(pos, originalNameLen, originalEntryName);
		}

		uint8_t hasNewName = (this->newEntryName != nullptr) ? 1 : 0;
		pos += pWriter->Write(pos, 1, &hasNewName);
		if (hasNewName)
		{
			uint16_t newNameLen = (uint16_t)strlen(newEntryName);
			pos += pWriter->Write(pos, 2, &newNameLen);
			pos += pWriter->Write(pos, newNameLen, newEntryName);
		}

		uint8_t temp = hasSerializedData ? 1 : 0;
		pos += pWriter->Write(pos, 1, &temp);

		uint32_t numResources = (uint32_t)std::min<size_t>(resources.size(), std::numeric_limits<uint32_t>::max());
		pos += pWriter->Write(pos, 4, &numResources);

		uint32_t i = 0;
		uint64_t curRangePos = 0;
		for (auto resIt = resources.begin(); resIt != resources.end() && i < numResources; ++resIt, ++i)
		{
			assert(resIt->outRangeBegin == curRangePos);
			pos += pWriter->Write(pos, 8, &resIt->rangeSize);

			uint8_t resourceFlags =
				((resIt->reader != nullptr) ? (1 << 0) : 0)
				| ((resIt->fromOriginalFile) ? (1 << 1) : 0);
			pos += pWriter->Write(pos, 1, &resourceFlags);

			if (resIt->reader != nullptr)
			{
				BundleEntryModifier tmpCopier(nullptr, nullptr, (unsigned int)-1, false,
					resIt->reader,
					resIt->rangeSize, resIt->inRangeBegin, copyBufferLen);
				pos = tmpCopier.Write(pos, pWriter);
			}
			if (resIt->fromOriginalFile)
			{
				pos += pWriter->Write(pos, 8, &resIt->inRangeBegin);
			}

			curRangePos += resIt->rangeSize;
		}
		return pos;
	}
//};

void _cdecl FreeMemoryResource_ReadBundleReplacer(void *pResource)
{
	if (pResource)
		free(pResource);
}
//On allocation error for the names, it returns a valid replacer with NULL names (or doesn't return because the new operator fails)
static BundleReplacer *ReadBundleReplacer(QWORD &pos, IAssetsReader *pReader, std::shared_ptr<IAssetsReader> ref_pReader, bool prefReplacerInMemory)
{
	uint16_t replacerType = 0xFFFF;
	pos += pReader->Read(pos, 2, &replacerType);
	if (replacerType >= BundleReplacer_MAX)
		return NULL;
	uint8_t replacerFileVersion = 0;
	pos += pReader->Read(pos, 1, &replacerFileVersion);
	uint8_t hasOriginalName = 1;
	if (replacerFileVersion >= 1)
		pos += pReader->Read(pos, 1, &hasOriginalName);
	uint16_t originalNameLen = 0;
	std::unique_ptr<char[]> originalName;
	if (hasOriginalName)
	{
		pos += pReader->Read(pos, 2, &originalNameLen);
		originalName.reset(new char[(uint32_t)originalNameLen + 1]);
		pos += pReader->Read(pos, originalNameLen, originalName.get());
		originalName[originalNameLen] = 0;
	}
	std::unique_ptr<char[]> newName;
	uint16_t newNameLen = 0;
	bool hasSerializedData = false;
	if (replacerType != BundleReplacer_BundleEntryRemover)
	{
		uint8_t hasNewName = 1;
		if (replacerFileVersion >= 1)
			pos += pReader->Read(pos, 1, &hasNewName);
		if (hasNewName)
		{
			pos += pReader->Read(pos, 2, &newNameLen);
			newName.reset(new char[(uint32_t)newNameLen + 1]);
			pos += pReader->Read(pos, newNameLen, newName.get());
			newName[newNameLen] = 0;
		}

		pos += pReader->Read(pos, 1, &hasSerializedData);
	}

	switch (replacerType)
	{
	case BundleReplacer_BundleEntryRemover:
		{
			if (replacerFileVersion > 1)
				return NULL;
			BundleReplacer *ret = new BundleEntryRemover(originalName.get(), (unsigned int)-1);
			return ret;
		}
	case BundleReplacer_BundleEntryRenamer:
		{
			if (replacerFileVersion > 1)
				return NULL;
			BundleReplacer *ret = new BundleEntryRenamer(originalName.get(), newName.get(), (unsigned int)-1, hasSerializedData);
			return ret;
		}
	case BundleReplacer_BundleEntryModifier:
	case BundleReplacer_BundleEntryModifierFromMem:
		{
			if (replacerFileVersion > 1)
				return NULL;
			QWORD size = 0;
			pos += pReader->Read(pos, 8, &size);
			BundleReplacer *ret = NULL;
			if (prefReplacerInMemory)
			{
				void *pMem = malloc(size);
				if (pMem)
				{
					QWORD actualRead = pReader->Read(pos, size, pMem);
					memset(&((uint8_t*)pMem)[actualRead], 0, size - actualRead);
					ret = new BundleEntryModifierFromMem(originalName.get(), newName.get(), (unsigned int)-1, hasSerializedData, pMem, size, 
						FreeMemoryResource_ReadBundleReplacer);
				}
			}
			if (!ret)
			{
				struct {
					void operator()(IAssetsReader*){}
				} nullDeleter;
				std::shared_ptr<IAssetsReader> inner_pReader = ref_pReader;
				if (inner_pReader == nullptr)
					inner_pReader = std::shared_ptr<IAssetsReader>(pReader, nullDeleter);
				else
					inner_pReader = std::shared_ptr<IAssetsReader>(inner_pReader, pReader);
				ret = new BundleEntryModifier(originalName.get(), newName.get(), (unsigned int)-1, hasSerializedData, inner_pReader, size, pos, 0);
			}
			pos += size;
			return ret;
		}
	case BundleReplacer_BundleEntryModifierFromAssets:
		{
			if (replacerFileVersion > 1)
				return NULL;
			QWORD ullReplacerCount = 0;
			pos += pReader->Read(pos, 8, &ullReplacerCount);
			size_t replacerCount = (size_t)ullReplacerCount;
			std::vector<std::shared_ptr<AssetsReplacer>> pReplacers(replacerCount);
			for (size_t i = 0; i < replacerCount; i++)
			{
				if (ref_pReader)
					pReplacers[i].reset(ReadAssetsReplacer(pos, ref_pReader));
				else
					pReplacers[i].reset(ReadAssetsReplacer(pos, pReader));
			}
			BundleReplacer *ret = 
				new BundleEntryModifierFromAssets(originalName.get(), newName.get(), (unsigned int)-1, NULL, std::move(pReplacers), (uint32_t)-1);
			return ret;
		}
	case BundleReplacer_BundleEntryModifierFromBundle:
		{
			if (replacerFileVersion > 1)
				return NULL;
			QWORD ullReplacerCount = 0;
			pos += pReader->Read(pos, 8, &ullReplacerCount);
			size_t replacerCount = (size_t)ullReplacerCount;
			std::vector<std::unique_ptr<BundleReplacer>> pReplacers(replacerCount);
			for (size_t i = 0; i < replacerCount; i++)
			{
				if (ref_pReader)
					pReplacers[i].reset(ReadBundleReplacer(pos, ref_pReader));
				else
					pReplacers[i].reset(ReadBundleReplacer(pos, pReader));
			}
			BundleReplacer *ret = 
				new BundleEntryModifierFromBundle(originalName.get(), newName.get(), (unsigned int)-1, std::move(pReplacers));
			return ret;
		}
	case BundleReplacer_BundleEntryModifierByResources:
		{
			if (replacerFileVersion > 1)
				return NULL;
			uint32_t resourceCount = 0;
			pos += pReader->Read(pos, 4, &resourceCount);
			std::vector<ReplacedResourceDesc> resources;
			resources.resize(resourceCount);
			uint64_t curPos = 0;
			for (uint32_t i = 0; i < resourceCount; ++i)
			{
				resources[i].outRangeBegin = curPos;
				pos += pReader->Read(pos, 8, &resources[i].rangeSize);
				resources[i].fromOriginalFile = false;
				uint8_t resourceFlags = 0;
				pos += pReader->Read(pos, 1, &resourceFlags);
				if (resourceFlags & (1 << 0))
				{
					//Resource is defined by following data.
					if (prefReplacerInMemory)
					{
						void* pMem = malloc(resources[i].rangeSize);
						if (pMem)
						{
							QWORD actualRead = pReader->Read(pos, resources[i].rangeSize, pMem);
							memset(&((uint8_t*)pMem)[actualRead], 0, resources[i].rangeSize - actualRead);
							resources[i].reader = std::shared_ptr<IAssetsReader>(
								Create_AssetsReaderFromMemory(pMem, resources[i].rangeSize, false, [](void* buf) {if (buf)free(buf); }));
							resources[i].inRangeBegin = 0;
						}
					}
					if (!resources[i].reader)
					{
						struct {
							void operator()(IAssetsReader*) {}
						} nullDeleter;
						std::shared_ptr<IAssetsReader> inner_pReader = ref_pReader;
						if (inner_pReader == nullptr)
							resources[i].reader = std::shared_ptr<IAssetsReader>(pReader, nullDeleter);
						else
							resources[i].reader = std::shared_ptr<IAssetsReader>(inner_pReader, pReader);
						resources[i].inRangeBegin = pos;
					}
					pos += resources[i].rangeSize;
				}
				if (resourceFlags & (1 << 1))
				{
					//Resource starts at an offset of its reader
					// (only used if based on some resource in the original file).
					uint64_t inRangeBegin = 0;
					pos += pReader->Read(pos, 8, &inRangeBegin);
					if (resources[i].reader == nullptr)
					{
						resources[i].inRangeBegin = inRangeBegin;
						resources[i].fromOriginalFile = true;
					}
				}
				curPos += resources[i].rangeSize;
			}
			BundleReplacer *ret = 
				new BundleEntryModifierByResources(originalName.get(), newName.get(), (unsigned int)-1, std::move(resources), 0);
			return ret;
		}
	}
	return NULL;
}
ASSETSTOOLS_API BundleReplacer *ReadBundleReplacer(QWORD &pos, IAssetsReader *pReader, bool prefReplacerInMemory)
{
	return ReadBundleReplacer(pos, pReader, nullptr, prefReplacerInMemory);
}
ASSETSTOOLS_API BundleReplacer *ReadBundleReplacer(QWORD &pos, std::shared_ptr<IAssetsReader> pReader, bool prefReplacerInMemory)
{
	IAssetsReader *_pReader = pReader.get();
	return ReadBundleReplacer(pos, _pReader, std::move(pReader), prefReplacerInMemory);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryRemover(const char *name, 
	unsigned int bundleListIndex)
{
	return new BundleEntryRemover(name, bundleListIndex);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryRenamer(const char *oldName, const char *newName, bool hasSerializedData, 
	unsigned int bundleListIndex)
{
	return new BundleEntryRenamer(oldName, newName, bundleListIndex, hasSerializedData);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifier(const char *oldName, const char *newName, bool hasSerializedData, 
	IAssetsReader *pReader, cbFreeReaderResource freeReaderCallback, QWORD size, QWORD readerPos, 
	size_t copyBufferLen,
	unsigned int bundleListIndex)
{
	struct {
		void operator()(IAssetsReader*){}
	} nullDeleter;
	std::shared_ptr<IAssetsReader> pReader_shared;
	if (freeReaderCallback == nullptr)
		pReader_shared = std::shared_ptr<IAssetsReader>(pReader, nullDeleter);
	else
		pReader_shared = std::shared_ptr<IAssetsReader>(pReader, freeReaderCallback);
	return new BundleEntryModifier(oldName, newName, bundleListIndex, hasSerializedData, std::move(pReader_shared), size, readerPos, copyBufferLen);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifier(const char *oldName, const char *newName, bool hasSerializedData, 
	std::shared_ptr<IAssetsReader> pReader, QWORD size, QWORD readerPos, 
	size_t copyBufferLen,
	unsigned int bundleListIndex)
{
	return new BundleEntryModifier(oldName, newName, bundleListIndex, hasSerializedData, std::move(pReader), size, readerPos, copyBufferLen);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromMem(const char *oldName, const char *newName, bool hasSerializedData, 
	void *pMem, size_t size,
	unsigned int bundleListIndex, cbFreeMemoryResource freeResourceCallback)
{
	return new BundleEntryModifierFromMem(oldName, newName, bundleListIndex, hasSerializedData,
		pMem, size, freeResourceCallback);
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromAssets(const char *oldName, const char *newName, 
	AssetsFile *pAssetsFile, AssetsReplacer **pReplacers, size_t replacerCount, uint32_t fileId,
	unsigned int bundleListIndex)
{
	return new BundleEntryModifierFromAssets(oldName, newName, bundleListIndex,
		pAssetsFile, pReplacers, replacerCount, fileId);
}
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierFromAssets(const char *oldName, const char *newName, 
	std::shared_ptr<ClassDatabaseFile> typeMeta, std::vector<std::shared_ptr<AssetsReplacer>> pReplacers, uint32_t fileId,
	unsigned int bundleListIndex)
{
	return std::unique_ptr<BundleReplacer>(new BundleEntryModifierFromAssets(oldName, newName, bundleListIndex,
		std::move(typeMeta), std::move(pReplacers), fileId));
}
ASSETSTOOLS_API BundleReplacer *MakeBundleEntryModifierFromBundle(const char *oldName, const char *newName, 
	BundleReplacer **pReplacers, size_t replacerCount, 
	unsigned int bundleListIndex)
{
	return new BundleEntryModifierFromBundle(oldName, newName, bundleListIndex, pReplacers, replacerCount);
}
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierFromBundle(const char *oldName, const char *newName, 
	std::vector<std::unique_ptr<BundleReplacer>> pReplacers, 
	unsigned int bundleListIndex)
{
	return std::unique_ptr<BundleReplacer>(
		new BundleEntryModifierFromBundle(oldName, newName, bundleListIndex, std::move(pReplacers)));
}
ASSETSTOOLS_API std::unique_ptr<BundleReplacer> MakeBundleEntryModifierByResources(const char* oldName, const char* newName,
	std::vector<ReplacedResourceDesc> resources, size_t copyBufferLen,
	unsigned int bundleListIndex)
{
	return std::unique_ptr<BundleReplacer>(
		new BundleEntryModifierByResources(oldName, newName, bundleListIndex, std::move(resources), copyBufferLen));
}
ASSETSTOOLS_API void FreeBundleReplacer(BundleReplacer *pReplacer)
{
	delete pReplacer;
}

//Tries to create a reader from a BundleReplacer.
//Works only for replacers created through MakeBundleEntryModifier or MakeBundleEntryModifierFromMem.
//For other kinds of replacers, nullptr will be returned.
//-> For MakeBundleEntryModifier, the internal reader will be reused.
//-> For MakeBundleEntryModifierFromMem, the internal buffer will be reused and the returned shared_ptr will also keep a BundleReplacer reference.
ASSETSTOOLS_API std::shared_ptr<IAssetsReader> MakeReaderFromBundleEntryModifier(std::shared_ptr<BundleReplacer> pReplacer)
{
	if (BundleEntryModifierFromMem *pEntryModifier = dynamic_cast<BundleEntryModifierFromMem*>(pReplacer.get()))
	{
		struct {
			std::shared_ptr<BundleReplacer> pReplacer;
			void operator()(IAssetsReader* pReader) { Free_AssetsReader(pReader); pReplacer.reset(); }
		} deleter;
		deleter.pReplacer = pReplacer;
		return std::shared_ptr<IAssetsReader>(
			Create_AssetsReaderFromMemory(pEntryModifier->pMem, pEntryModifier->size, false),
			deleter);
	}
	else if (BundleEntryModifier *pEntryModifier = dynamic_cast<BundleEntryModifier*>(pReplacer.get()))
	{
		struct {
			std::shared_ptr<IAssetsReader> pSourceReader;
			void operator()(IAssetsReader* pReader) { Free_AssetsReader(pReader); pSourceReader.reset(); }
		} deleter;
		deleter.pSourceReader = pEntryModifier->pReader;
		return std::shared_ptr<IAssetsReader>(
			Create_AssetsReaderFromReaderRange(pEntryModifier->pReader.get(), pEntryModifier->readerPos, pEntryModifier->size, true),
			deleter);
	}
	return nullptr;
}

