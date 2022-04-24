#include "AssetIterator.h"
#include "AppContext.h"
#include <assert.h>

//Instantiate an AssetIdentifier with a relative fileID relative to the dependencies for an absolute fileID and a pathID.
AssetIdentifier::AssetIdentifier(unsigned int referenceFromFileID, unsigned int relFileID, pathid_t pathID)
	: fileID(relFileID), pathID(pathID), fileIDIsRelative(true), referenceFromFileID(referenceFromFileID), pFile(nullptr), pAssetInfo(nullptr), pReplacer(nullptr)
{

}
AssetIdentifier::AssetIdentifier(unsigned int fileID, std::shared_ptr<AssetsEntryReplacer> &pReplacer)
	: fileID(fileID), pathID(pReplacer->GetPathID()), fileIDIsRelative(false), pFile(nullptr), pAssetInfo(nullptr), pReplacer(pReplacer)
{}
//Instantiate an AssetIdentifier with a relative fileID (from pReferenceFromFile->references) and a pathID.
AssetIdentifier::AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> &pReferenceFromFile, unsigned int relFileID, pathid_t pathID)
	: fileID(pReferenceFromFile->resolveRelativeFileID(relFileID)), pathID(pathID), fileIDIsRelative(false), pFile(nullptr), pAssetInfo(nullptr), pReplacer(nullptr)
{
}
AssetIdentifier::AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> _pFile, pathid_t pathID)
	: fileID(_pFile->getFileID()), pathID(pathID), fileIDIsRelative(false), pFile(std::move(_pFile)), pAssetInfo(nullptr), pReplacer(nullptr)
{
}
AssetIdentifier::AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> _pFile, AssetFileInfoEx *pAssetInfo)
	: fileID(_pFile->getFileID()), pathID(pAssetInfo->index), fileIDIsRelative(false), pFile(std::move(_pFile)), pAssetInfo(pAssetInfo), pReplacer(nullptr)
{}
AssetIdentifier::AssetIdentifier(std::shared_ptr<class AssetsFileContextInfo> _pFile, std::shared_ptr<AssetsEntryReplacer> &pReplacer)
	: fileID(_pFile->getFileID()), pathID(pAssetInfo->index), fileIDIsRelative(false), pFile(std::move(_pFile)), pAssetInfo(nullptr), pReplacer(pReplacer)
{}
bool AssetIdentifier::resolve(class AppContext &appContext)
{
	if (!pFile)
	{
		unsigned int absFileID = fileID;
		if (fileIDIsRelative)
		{
			std::shared_ptr<FileContextInfo> pSourceContextInfoRaw = appContext.getContextInfo(referenceFromFileID);
			if (!pSourceContextInfoRaw)
				return false;
			AssetsFileContextInfo *pSourceContextInfo = dynamic_cast<AssetsFileContextInfo*>(pSourceContextInfoRaw.get());
			if (!pSourceContextInfo)
				return false;
			absFileID = pSourceContextInfo->resolveRelativeFileID(fileID);
		}
		if (absFileID == 0)
			return false;
		std::shared_ptr<FileContextInfo> pContextInfo = appContext.getContextInfo(absFileID);
		if (!pContextInfo)
			return false;
		pFile = std::dynamic_pointer_cast<AssetsFileContextInfo>(pContextInfo);
	}
	if (!pFile)
		return false;
	if (!pAssetInfo && !pReplacer)
	{
		pReplacer = pFile->getReplacer(pathID);
		if (!pReplacer)
			pAssetInfo = pFile->getAssetsFileContext()->getAssetsFileTable()->getAssetInfo(pathID);
	}
	if (!pAssetInfo && !pReplacer)
		return false;
	return true;
}
int32_t AssetIdentifier::getClassID()
{
	if (pReplacer)
		return pReplacer->GetClassID();
	if (pAssetInfo)
		return (int32_t)pAssetInfo->curFileType;
	return INT32_MIN;
}
uint16_t AssetIdentifier::getMonoScriptID()
{
	if (pReplacer)
		return pReplacer->GetMonoScriptID();
	if (pAssetInfo)
		return pAssetInfo->scriptIndex;
	return 0xFFFF;
}
uint64_t AssetIdentifier::getDataSize() //Will return 0 if resolve was not called successfully (unless the (pFile,pAssetInfo) constructor was called)
{
	if (pReplacer)
		return pReplacer->GetSize();
	if (pAssetInfo)
		return pAssetInfo->curFileSize;
	return 0;
}
size_t AssetIdentifier::read(size_t size, void *buffer) //Returns the actually read bytes.
{
	if (pReplacer)
	{
		IAssetsWriterToMemory *pWriter = Create_AssetsWriterToMemory(buffer, size);
		if (!pWriter)
			return 0;
		uint64_t length = pReplacer->Write(0, pWriter);
		Free_AssetsWriter(pWriter);
		return length;
	}
	if (pAssetInfo && pFile)
	{
		QWORD pos = pAssetInfo->absolutePos;
		uint32_t assetSize = pAssetInfo->curFileSize;
		if (size > assetSize)
			size = assetSize;
		//Unsafe is OK here since we explicitly state the file position in the Read call.
		IAssetsReader *pReader = pFile->getAssetsFileContext()->getReaderUnsafe();
		if (!pReader)
			return 0;
		uint64_t length = pReader->Read(pos, assetSize, buffer, false);
		return length;
	}
	return 0;
}

IAssetsReader_ptr AssetIdentifier::makeReader()
{
	IAssetsReader_ptr ret(nullptr, DummyAssetsReaderDeleter);
	if (pReplacer)
	{
		if (pReplacer->GetSize() > SIZE_MAX)
			return ret;
		IAssetsWriterToMemory *pWriter = Create_AssetsWriterToMemory((size_t)pReplacer->GetSize());
		if (!pWriter)
			return ret;
		uint64_t length = pReplacer->Write(0, pWriter);
		void *dataBuffer = nullptr; size_t dataSize = 0;
		IAssetsReader *pReader = nullptr;
		if (pWriter->GetBuffer(dataBuffer, dataSize))
		{
			pReader = Create_AssetsReaderFromMemory(dataBuffer, dataSize, false, Free_AssetsWriterToMemory_DynBuf);
			if (pReader)
				pWriter->SetFreeBuffer(false);
		}
		Free_AssetsWriter(pWriter);
		return IAssetsReader_ptr(pReader, Free_AssetsReader);// pReader;
	}
	if (pAssetInfo && pFile)
	{
		QWORD pos = pAssetInfo->absolutePos;
		uint32_t assetSize = pAssetInfo->curFileSize;
		//Unsafe is OK here since AssetsReaderFromReaderRange behaves like a view if alwaysSeek is set to true.
		IAssetsReader *pReader = pFile->getAssetsFileContext()->getReaderUnsafe();
		if (!pReader)
			return ret;
		return IAssetsReader_ptr(Create_AssetsReaderFromReaderRange(pReader, pos, assetSize, true), Free_AssetsReader);
	}
	return ret;
}
bool AssetIdentifier::isBigEndian()
{
	if (pFile)
	{ 
		bool result; pFile->getEndianness(result); return result;
	}
	return false;
}


AssetIterator::AssetIterator(AssetsFileContextInfo *pContextInfo, bool ignoreExisting, bool ignoreReplacers, bool ignoreRemoverReplacers)
	: pContextInfo(pContextInfo), pAssetsFileTable(nullptr), assetIndex(0), pAssetReplacerHint(nullptr),
	ignoreReplacers(ignoreReplacers), ignoreExisting(ignoreExisting), ignoreRemoverReplacers(ignoreRemoverReplacers)
{
	if (pContextInfo->pContext)
		pAssetsFileTable = pContextInfo->pContext->getAssetsFileTable();
	if (pAssetsFileTable && ignoreExisting)
		assetIndex = pAssetsFileTable->assetFileInfoCount;
	if (!ignoreReplacers)
	{
		pContextInfo->lockReplacersRead();
		replacersIterator = pContextInfo->pReplacersByPathID.cbegin();
		if (!ignoreExisting && pAssetsFileTable)
		{
			updateAssetReplacerHint();
			if (pAssetReplacerHint && (*pAssetReplacerHint)->GetType() == AssetsReplacement_Remove)
				++(*this); //Skip removed assets
		}
		if (ignoreExisting || !pAssetsFileTable || assetIndex >= pAssetsFileTable->assetFileInfoCount)
		{
			while (replacersIterator != pContextInfo->pReplacersByPathID.cend()
				&& (ignoreRemoverReplacers && replacersIterator->second.pReplacer->GetType() == AssetsReplacement_Remove)) //Skip removed assets
				++replacersIterator;
		}
	}
}
AssetIterator::~AssetIterator()
{
	if (!ignoreReplacers && pContextInfo)
		pContextInfo->unlockReplacersRead();
}
AssetIterator::AssetIterator(AssetIterator &&other)
{
	pContextInfo = other.pContextInfo;
	pAssetsFileTable = other.pAssetsFileTable;
	assetIndex = other.assetIndex;
	pAssetReplacerHint = other.pAssetReplacerHint;
	ignoreReplacers = other.ignoreReplacers;
	ignoreExisting = other.ignoreExisting;
	ignoreRemoverReplacers = other.ignoreRemoverReplacers;
	replacersIterator = other.replacersIterator;
	other.ignoreExisting = true;
	other.ignoreReplacers = true; //The old iterator must not unlock the replacers list.
}
AssetIterator &AssetIterator::operator=(const AssetIterator &other)
{
	if ((!ignoreReplacers && pContextInfo) && (other.ignoreReplacers || pContextInfo != other.pContextInfo))
	{
		if (pContextInfo) pContextInfo->unlockReplacersRead();
	}
	if ((ignoreReplacers || pContextInfo != other.pContextInfo) && !other.ignoreReplacers)
		other.pContextInfo->lockReplacersRead();
	pContextInfo = other.pContextInfo;
	pAssetsFileTable = other.pAssetsFileTable;
	assetIndex = other.assetIndex;
	pAssetReplacerHint = other.pAssetReplacerHint;
	ignoreExisting = other.ignoreExisting;
	ignoreReplacers = other.ignoreReplacers;
	ignoreRemoverReplacers = other.ignoreRemoverReplacers;
	replacersIterator = other.replacersIterator;
	return *this;
}
void AssetIterator::updateAssetReplacerHint()
{
	pAssetReplacerHint = nullptr;
	if (!ignoreReplacers && assetIndex < pAssetsFileTable->assetFileInfoCount)
	{
		//Look for replacers for this asset.
		auto it = pContextInfo->pReplacersByPathID.find((pathid_t)pAssetsFileTable->pAssetFileInfo[assetIndex].index);
		if (it != pContextInfo->pReplacersByPathID.end())
		{
			pAssetReplacerHint = &it->second.pReplacer;
			assert(it->second.replacesExistingAsset);
		}
	}
}
AssetIterator &AssetIterator::operator++()
{
	bool increment = true;
	if (!ignoreExisting && pAssetsFileTable && assetIndex < pAssetsFileTable->assetFileInfoCount)
	{
		if (assetIndex == pAssetsFileTable->assetFileInfoCount - 1)
			increment = false;
		while (++assetIndex < pAssetsFileTable->assetFileInfoCount && assetIndex != 0) //Also detect overflow
		{
			updateAssetReplacerHint();
			if (pAssetReplacerHint && (*pAssetReplacerHint)->GetType() == AssetsReplacement_Remove)
				continue; //Skip removed assets
			return (*this);
		}
		assetIndex = pAssetsFileTable->assetFileInfoCount;
	}
	if (!ignoreReplacers && pContextInfo)
	{
		if (increment) ++replacersIterator;
		while (replacersIterator != pContextInfo->pReplacersByPathID.cend()
			&& ((!ignoreExisting && replacersIterator->second.replacesExistingAsset) //Skip replacers that were already iterated over before.
			|| (replacersIterator->second.pReplacer->GetType() == AssetsReplacement_Remove))) //Skip removed assets
			++replacersIterator;
		return (*this);
	}
	return (*this);
}
bool AssetIterator::isEnd() const
{
	if (!ignoreExisting && pAssetsFileTable)
	{
		if (assetIndex < pAssetsFileTable->assetFileInfoCount)
			return false;
	}
	if (!ignoreReplacers && pContextInfo)
	{
		if (replacersIterator != pContextInfo->pReplacersByPathID.cend())
			return false;
	}
	return true;
}
void AssetIterator::get(AssetIdentifier &identifier)
{
	assert(pContextInfo != nullptr);
	assert(!isEnd());
	identifier.fileID = pContextInfo->getFileID();
	identifier.fileIDIsRelative = false;
	if (identifier.pFile.get() != pContextInfo)
		identifier.pFile = nullptr;
	if (!ignoreExisting && pAssetsFileTable && assetIndex < pAssetsFileTable->assetFileInfoCount)
	{
		identifier.pAssetInfo = &pAssetsFileTable->pAssetFileInfo[assetIndex];
		identifier.pathID = (pathid_t)identifier.pAssetInfo->index;
		if (pAssetReplacerHint)
			identifier.pReplacer = *pAssetReplacerHint;
		else
			identifier.pReplacer = nullptr;
		return;
	}
	if (!ignoreReplacers && pContextInfo)
	{
		identifier.pAssetInfo = nullptr;
		identifier.pReplacer = replacersIterator->second.pReplacer;
		identifier.pathID = (pathid_t)identifier.pReplacer->GetPathID();
		return;
	}
	assert(false);
	identifier.pAssetInfo = nullptr;
	identifier.pReplacer = nullptr;
	identifier.pathID = 0;
}
