#include "stdafx.h"
#include "AssetBundleFileFormat.h"
#include "AssetsFileReader.h"
#include "AssetsFileFormat.h"
#include "../inc/LZMA/LzmaDec.h"
#include "../inc/LZMA/LzmaEnc.h"
#include "../libCompression/lz4.h"
#include "../libCompression/lz4dec.h"
#include "../libCompression/lz4enc.h"
#include <assert.h>
#include <unordered_map>
#include <future>

ASSETSTOOLS_API bool AssetBundleBlockAndDirectoryList06::Read(QWORD filePos, IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger)
{
	/*uint8_t *dataBuf = (uint8_t*)malloc(sizeComp);
	if (!dataBuf)
		goto __goto_allocerror;
	if (!reader(filePos, sizeComp, dataBuf, lPar))
		goto __goto_readerror;
	switch (compressionType)
	{
	case 2:
	case 3:
		//LZ4
		uint8_t *outBuf = (uint8_t*)malloc(sizeUncomp);
		if (!outBuf)
			goto __goto_allocerror;
		if (LZ4_decompress_safe((char*)dataBuf, (char*)outBuf, (int)sizeComp & 0x7FFFFFFF, (int)sizeUncomp & 0x7FFFFFFF) != sizeUncomp)
		{
			free(dataBuf);
			free(outBuf);
			goto __goto_decomperror;
		}
		free(dataBuf);
		dataBuf = outBuf;
		break;
	case 1:
		//LZMA
		//maybe support this, maybe don't (reading this structure isn't really required in our case if the bundle is compressed)
		free(dataBuf);
		return false;
		break;
	default:
		break;
	}*/
	if (!pReader->Read(filePos, 8, &this->checksumLow))
		goto __goto_readerror;
	filePos += 8;
	if (!pReader->Read(-1, 8, &this->checksumHigh))
		goto __goto_readerror;
	filePos += 8;

	if (!pReader->Read(-1, 4, &this->blockCount))
		goto __goto_readerror;
	filePos += 4;
	SwapEndians_(this->blockCount);
	blockInf = (AssetBundleBlockInfo06*)malloc(this->blockCount * sizeof(AssetBundleBlockInfo06));
	if (!blockInf)
		goto __goto_allocerror;
	for (uint32_t i = 0; i < this->blockCount; i++)
	{
		if (!pReader->Read(-1, 4, &blockInf[i].decompressedSize))
			goto __goto_readerror;
		SwapEndians_(blockInf[i].decompressedSize);
		if (!pReader->Read(-1, 4, &blockInf[i].compressedSize))
			goto __goto_readerror;
		SwapEndians_(blockInf[i].compressedSize);
		if (!pReader->Read(-1, 2, &blockInf[i].flags))
			goto __goto_readerror;
		SwapEndians_(blockInf[i].flags);
		filePos += 10;
	}

	if (!pReader->Read(-1, 4, &this->directoryCount))
		goto __goto_readerror;
	filePos += 4;
	SwapEndians_(this->directoryCount);
	dirInf = (AssetBundleDirectoryInfo06*)malloc(this->directoryCount * sizeof(AssetBundleDirectoryInfo06));
	if (!dirInf)
		goto __goto_allocerror;
	for (uint32_t i = 0; i < this->directoryCount; i++)
	{
		if (!pReader->Read(-1, 8, &dirInf[i].offset))
			goto __goto_readerror;
		SwapEndians_(dirInf[i].offset);
		if (!pReader->Read(-1, 8, &dirInf[i].decompressedSize))
			goto __goto_readerror;
		SwapEndians_(dirInf[i].decompressedSize);
		if (!pReader->Read(-1, 4, &dirInf[i].flags))
			goto __goto_readerror;
		SwapEndians_(dirInf[i].flags);
		filePos += 20;

		char nameBuffer[40];
		bool eosFound = false; size_t strLen = 0;
		while (!eosFound)
		{
			QWORD nRead = pReader->Read(-1, 40, &nameBuffer);
			if (!nRead)
				goto __goto_readerror;
			for (size_t i = 0; i < (size_t)nRead; i++)
			{
				if (nameBuffer[i] == 0)
				{
					eosFound = true;
					strLen += (i+1);
					break;
				}
			}
			if (!eosFound)
				strLen += (size_t)nRead;
		}
		dirInf[i].name = (char*)malloc(strLen);
		if (dirInf[i].name == NULL)
			goto __goto_allocerror;
		if (pReader->Read(filePos, strLen, const_cast<char*>(dirInf[i].name)) != strLen)
			goto __goto_readerror;
		const_cast<char*>(dirInf[i].name)[strLen-1] = 0;
		filePos += strLen;
	}
	return true;
	__goto_allocerror:
	if (errorLogger) errorLogger("AssetBundleBlockAndDirectoryList06 : Out of memory!");
	return false;
	__goto_readerror:
	if (errorLogger) errorLogger("AssetBundleBlockAndDirectoryList06 : A file read error occured!");
	return false;
	/*__goto_decomperror:
	if (errorLogger) errorLogger("AssetBundleBlockAndDirectoryList06 : A decompress error occured!");
	return false;*/
}
ASSETSTOOLS_API void AssetBundleBlockAndDirectoryList06::Free()
{
	if (this->blockInf)
		free(this->blockInf);
	if (this->dirInf)
	{
		for (uint32_t i = 0; i < this->directoryCount; i++)
		{
			if (this->dirInf[i].name)
				free(const_cast<char*>(this->dirInf[i].name));
		}
		free(this->dirInf);
	}
	memset(this, 0, sizeof(AssetBundleBlockAndDirectoryList06));
}
//Write doesn't compress
ASSETSTOOLS_API bool AssetBundleBlockAndDirectoryList06::Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger)
{
	uint32_t dwTmp;
	if (!pWriter->Write(curFilePos, 8, &this->checksumLow))
		goto __goto_writeerror;
	curFilePos += 8;
	if (!pWriter->Write(-1, 8, &this->checksumHigh))
		goto __goto_writeerror;
	curFilePos += 8;

	dwTmp = SwapEndians(this->blockCount);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	for (uint32_t i = 0; i < this->blockCount; i++)
	{
		dwTmp = SwapEndians(this->blockInf[i].decompressedSize);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
		dwTmp = SwapEndians(this->blockInf[i].compressedSize);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
		uint16_t wTmp = SwapEndians(this->blockInf[i].flags);
		if (!pWriter->Write(-1, 2, &wTmp))
			goto __goto_writeerror;
		curFilePos += 2;
	}

	dwTmp = SwapEndians(this->directoryCount);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	
	for (uint32_t i = 0; i < this->directoryCount; i++)
	{
		QWORD qwTmp = SwapEndians(this->dirInf[i].offset);
		if (!pWriter->Write(-1, 8, &qwTmp))
			goto __goto_writeerror;
		curFilePos += 8;
		qwTmp = SwapEndians(this->dirInf[i].decompressedSize);
		if (!pWriter->Write(-1, 8, &qwTmp))
			goto __goto_writeerror;
		curFilePos += 8;
		dwTmp = SwapEndians(this->dirInf[i].flags);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;

		size_t curStrLen = strlen(this->dirInf[i].name)+1;
		curFilePos += curStrLen;
		if (!pWriter->Write(-1, curStrLen, this->dirInf[i].name))
			goto __goto_writeerror;
	}

	return true;
	__goto_writeerror:
	if (errorLogger) errorLogger("AssetBundleHeader06 : A file write error occured!");
	return false;
}
ASSETSTOOLS_API bool AssetBundleHeader06::ReadInitial(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger)
{
	QWORD curFilePos = 0;
	char curChar;
	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(curFilePos, 1, &curChar))
			goto __goto_readerror;
		curFilePos++;
		if (i < 13) signature[i] = curChar;
		if (curChar == 0) break;
	}
	signature[12] = 0;
	
	if (!strcmp(signature, "UnityArchive"))
		this->fileVersion = 6;
	else
	{
		if (!pReader->Read(-1, 4, &this->fileVersion))
			goto __goto_readerror;
		SwapEndians_(this->fileVersion);
	}

	return true;
	__goto_readerror:
	if (errorLogger) errorLogger("AssetBundleHeader06 : A file read error occured!");
	return false;
}
ASSETSTOOLS_API bool AssetBundleHeader06::Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger)
{
	QWORD curPos = 0;
	char curChar;
	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(curPos, 1, &curChar))
			goto __goto_readerror;
		curPos++;
		if (i < 13) signature[i] = curChar;
		if (curChar == 0) break;
	}
	signature[12] = 0;
	
	if (!strcmp(signature, "UnityArchive"))
		this->fileVersion = 6;
	else
	{
		if (!pReader->Read(-1, 4, &this->fileVersion))
			goto __goto_readerror;
		SwapEndians_(this->fileVersion);
		if (this->fileVersion != 6 && this->fileVersion != 7)
		{
			if (errorLogger) errorLogger("That file version is unknown!");
			return false;
		}
	}

	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(-1, 1, &curChar))
			goto __goto_readerror;
		if (i < sizeof(minPlayerVersion)) minPlayerVersion[i] = curChar;
		if (curChar == 0) break;
	}
	minPlayerVersion[sizeof(minPlayerVersion) - 1] = 0;

	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(-1, 1, &curChar))
			goto __goto_readerror;
		if (i < sizeof(fileEngineVersion)) fileEngineVersion[i] = curChar;
		if (curChar == 0) break;
	}
	fileEngineVersion[sizeof(fileEngineVersion) - 1] = 0;
	
	if (!pReader->Read(-1, 8, &this->totalFileSize))
		goto __goto_readerror;
	SwapEndians_(this->totalFileSize);
	
	if (!pReader->Read(-1, 4, &this->compressedSize))
		goto __goto_readerror;
	SwapEndians_(this->compressedSize);
	if (!pReader->Read(-1, 4, &this->decompressedSize))
		goto __goto_readerror;
	SwapEndians_(this->decompressedSize);

	if (!pReader->Read(-1, 4, &this->flags))
		goto __goto_readerror;
	SwapEndians_(this->flags);
	
	return true;
	__goto_readerror:
	if (errorLogger) errorLogger("AssetBundleHeader06 : A file read error occured!");
	return false;
}
ASSETSTOOLS_API bool AssetBundleHeader06::Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger)
{
	uint32_t dwTmp;
	QWORD qwTmp;
	QWORD startPos = curFilePos;
	size_t curStrLen = strlen(this->signature)+1;
	if (!pWriter->Write(curFilePos, curStrLen, &this->signature))
		goto __goto_writeerror;
	curFilePos += curStrLen;

	dwTmp = SwapEndians(this->fileVersion);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	curStrLen = strlen(this->minPlayerVersion)+1;
	curFilePos += curStrLen;
	if (!pWriter->Write(-1, curStrLen, &this->minPlayerVersion))
		goto __goto_writeerror;

	curStrLen = strlen(this->fileEngineVersion)+1;
	curFilePos += curStrLen;
	if (!pWriter->Write(-1, curStrLen, &this->fileEngineVersion))
		goto __goto_writeerror;

	qwTmp = SwapEndians(this->totalFileSize);
	if (!pWriter->Write(-1, 8, &qwTmp))
		goto __goto_writeerror;
	curFilePos += 8;

	dwTmp = SwapEndians(this->compressedSize);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	dwTmp = SwapEndians(this->decompressedSize);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	dwTmp = SwapEndians(this->flags);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	if (!strcmp(signature, "UnityWeb") || !strcmp(signature, "UnityRaw"))
	{
		dwTmp = 0;
		if (!pWriter->Write(-1, 1, &dwTmp))
			goto __goto_writeerror;
		curFilePos ++;
	}
	if (this->fileVersion >= 7)
	{
		QWORD alignmentLen = (((curFilePos - startPos) + 15) & ~15) - (curFilePos - startPos);
		uint64_t alignment[2] = {0,0};
		if (!pWriter->Write(-1, alignmentLen, &alignment[0]))
			goto __goto_writeerror;
		curFilePos += alignmentLen;
	}

	return true;
	__goto_writeerror:
	if (errorLogger) errorLogger("AssetBundleHeader06 : A file write error occured!");
	return false;
}

ASSETSTOOLS_API bool AssetBundleHeader03::Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger)
{
	if (!pReader->Read(0, 9, &this->signature))
		goto __goto_readerror;
	if (_strnicmp(this->signature, "UnityRaw", 9) && _strnicmp(this->signature, "UnityWeb", 9))
	{
		/*if (!_strnicmp(this->signature, "UnityWeb", 9))
		{
			if (errorLogger) errorLogger("AssetBundleHeader : I can't decompress compressed files!");
			return false;
		}*/
		if (errorLogger) errorLogger("AssetBundleHeader : Unknown file type!");
		return false;
	}

	if (!pReader->Read(-1, 4, &this->fileVersion))
		goto __goto_readerror;
	SwapEndians_(this->fileVersion);
	if (this->fileVersion != 3)
	{
		if (errorLogger) errorLogger("AssetBundleHeader : Either that file is invalid or it uses an unknown file version!");
		return false;
	}

	char curChar;
	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(-1, 1, &curChar))
			goto __goto_readerror;
		if (i < sizeof(minPlayerVersion)) minPlayerVersion[i] = curChar;
		if (curChar == 0) break;
	}
	minPlayerVersion[sizeof(minPlayerVersion)-1] = 0;

	for (unsigned int i = 0;; i++)
	{
		if (!pReader->Read(-1, 1, &curChar))
			goto __goto_readerror;
		if (i < sizeof(fileEngineVersion)) fileEngineVersion[i] = curChar;
		if (curChar == 0) break;
	}
	fileEngineVersion[sizeof(fileEngineVersion)-1] = 0;

	if (!pReader->Read(-1, 4, &this->minimumStreamedBytes))
		goto __goto_readerror;
	SwapEndians_(this->minimumStreamedBytes);
	if (!pReader->Read(-1, 4, &this->bundleDataOffs))
		goto __goto_readerror;
	SwapEndians_(this->bundleDataOffs);
	if (!pReader->Read(-1, 4, &this->numberOfAssetsToDownload))
		goto __goto_readerror;
	SwapEndians_(this->numberOfAssetsToDownload);
	if (!pReader->Read(-1, 4, &this->blockCount))
		goto __goto_readerror;
	SwapEndians_(this->blockCount);
	if (this->pBlockList != NULL) free(this->pBlockList);
	this->pBlockList = (AssetBundleOffsetPair*)malloc(sizeof(AssetBundleOffsetPair) * blockCount);
	if (this->pBlockList == NULL) //out of memory
		goto __goto_outofmemory;
	for (uint32_t i = 0; i < blockCount; i++)
	{
		if (!pReader->Read(-1, 4, &this->pBlockList[i].compressed))
			goto __goto_readerror;
		this->pBlockList[i].compressed = SwapEndians(this->pBlockList[i].compressed);
		if (!pReader->Read(-1, 4, &this->pBlockList[i].uncompressed))
			goto __goto_readerror;
		this->pBlockList[i].uncompressed = SwapEndians(this->pBlockList[i].uncompressed);
	}
	if (this->fileVersion >= 2)
	{
		if (!pReader->Read(-1, 4, &this->fileSize2))
			goto __goto_readerror;
		SwapEndians_(this->fileSize2);
	}
	if (this->fileVersion >= 3)
	{
		if (!pReader->Read(-1, 4, &this->unknown2))
			goto __goto_readerror;
		this->unknown2 = SwapEndians(this->unknown2);
	}
	if (!pReader->Read(-1, 1, &this->unknown3))
		goto __goto_readerror;
	return true;

	__goto_readerror:
	if (errorLogger) errorLogger("AssetBundleHeader : A file read error occured!");
	return false;

	__goto_outofmemory:
	if (errorLogger) errorLogger("AssetBundleHeader : Out of memory!");
	return false;
}
ASSETSTOOLS_API bool AssetBundleHeader03::Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger)
{
	uint32_t dwTmp;
	size_t curStrLen;
	if (!pWriter->Write(curFilePos, 9, &this->signature))
		goto __goto_writeerror;
	curFilePos += 9;
	dwTmp = SwapEndians(this->fileVersion);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	curStrLen = strlen(this->minPlayerVersion)+1;
	curFilePos += curStrLen;
	if (!pWriter->Write(-1, curStrLen, &this->minPlayerVersion))
		goto __goto_writeerror;
	curFilePos += curStrLen;
	curStrLen = strlen(this->fileEngineVersion)+1;
	if (!pWriter->Write(-1, curStrLen, &this->fileEngineVersion))
		goto __goto_writeerror;

	dwTmp = SwapEndians(this->minimumStreamedBytes);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	dwTmp = SwapEndians(this->bundleDataOffs);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	dwTmp = SwapEndians(this->numberOfAssetsToDownload);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	dwTmp = SwapEndians(this->blockCount);
	if (!pWriter->Write(-1, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;
	for (uint32_t i = 0; i < blockCount; i++)
	{
		dwTmp = SwapEndians(this->pBlockList[i].compressed);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
		dwTmp = SwapEndians(this->pBlockList[i].uncompressed);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
	}
	if (this->fileVersion >= 2)
	{
		dwTmp = SwapEndians(this->fileSize2);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
	}
	if (this->fileVersion >= 3)
	{
		dwTmp = SwapEndians(this->unknown2);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 4;
	}
	if (!pWriter->Write(-1, 1, &this->unknown3))
		goto __goto_writeerror;
	curFilePos++;
	return true;
	__goto_writeerror:
	if (errorLogger) errorLogger("AssetBundleHeader : A file write error occured!");
	return false;
}

ASSETSTOOLS_API QWORD AssetBundleDirectoryInfo06::GetAbsolutePos(AssetBundleHeader06 *pHeader)
{
	return (this->offset + pHeader->GetFileDataOffset());
}
ASSETSTOOLS_API QWORD AssetBundleDirectoryInfo06::GetAbsolutePos(class AssetBundleFile *pFile)
{
	return GetAbsolutePos(&pFile->bundleHeader6);
}

ASSETSTOOLS_API unsigned int AssetBundleEntry::GetAbsolutePos(AssetBundleHeader03 *pHeader)//, uint32_t listIndex)
{
	unsigned int ret = (offset + pHeader->bundleDataOffs);
	/*for (uint32_t i = 0; i < listIndex; i++)
	{
		ret += pHeader->pLevelList[i].uncompressed;
	}*/
	return ret;
}
ASSETSTOOLS_API unsigned int AssetBundleEntry::GetAbsolutePos(class AssetBundleFile *pFile)//, uint32_t listIndex)
{
	return GetAbsolutePos(&pFile->bundleHeader3);//, listIndex);
}
ASSETSTOOLS_API bool AssetsList::Read(IAssetsReader *pReader, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger)
{
	this->pos = (uint32_t)curFilePos;
	uint32_t dwTmp;
	if (!pReader->Read(curFilePos, 4, &dwTmp))
		goto __goto_readerror;
	curFilePos += 4;
	this->count = SwapEndians(dwTmp);
	
	for (uint32_t i = this->count; i < allocatedCount; i++)
	{
		free(ppEntries[i]);
	}
	ppEntries = (AssetBundleEntry**)realloc(ppEntries, this->count * sizeof(AssetBundleEntry*));
	if (ppEntries == NULL)
		goto __goto_outofmemory;
	for (uint32_t i = allocatedCount; i < this->count; i++)
		ppEntries[i] = NULL;

	for (unsigned int i = 0; i < this->count; i++)
	{
		char nameBuffer[40];
		bool eosFound = false; size_t strLen = 0;
		while (!eosFound)
		{
			QWORD nRead = pReader->Read(-1, 40, &nameBuffer);
			if (!nRead)
				goto __goto_readerror;
			for (size_t i = 0; i < (size_t)nRead; i++)
			{
				if (nameBuffer[i] == 0)
				{
					eosFound = true;
					strLen += (i+1);
					break;
				}
			}
			if (!eosFound)
				strLen += (size_t)nRead;
		}
		ppEntries[i] = (AssetBundleEntry*)realloc(ppEntries[i], sizeof(AssetBundleEntry) - 1 + strLen);
		if (ppEntries[i] == NULL)
			goto __goto_outofmemory;
		if (pReader->Read(curFilePos, strLen, ppEntries[i]->name) != strLen)
			goto __goto_readerror;
		ppEntries[i]->name[strLen-1] = 0;
		if (!pReader->Read(-1, 4, &ppEntries[i]->offset))
			goto __goto_readerror;
		SwapEndians_(ppEntries[i]->offset);
		if (!pReader->Read(-1, 4, &ppEntries[i]->length))
			goto __goto_readerror;
		SwapEndians_(ppEntries[i]->length);
		curFilePos += (strLen + 4 + 4);
	}
	allocatedCount = this->count;
	return true;

	__goto_readerror:
	if (errorLogger) errorLogger("AssetsList : A file read error occured!");
	return false;
	__goto_outofmemory:
	if (errorLogger) errorLogger("AssetsList : Out of memory!");
	return false;
}
ASSETSTOOLS_API void AssetsList::Free()
{
	for (uint32_t i = 0; i < allocatedCount; i++)
	{
		free(ppEntries[i]);
	}
	if (ppEntries)
		free(ppEntries);
	ppEntries = NULL;
	allocatedCount = 0;
}

ASSETSTOOLS_API bool AssetsList::Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger)
{
	uint32_t dwTmp;
	dwTmp = SwapEndians(this->count);
	if (!pWriter->Write(curFilePos, 4, &dwTmp))
		goto __goto_writeerror;
	curFilePos += 4;

	for (unsigned int i = 0; i < this->count; i++)
	{
		uint32_t nameStringLen = (uint32_t)strlen(ppEntries[i]->name)+1;
		if (!pWriter->Write(-1, nameStringLen, &ppEntries[i]->name))
			goto __goto_writeerror;	
		curFilePos += nameStringLen;
		dwTmp = SwapEndians(ppEntries[i]->offset);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		dwTmp = SwapEndians(ppEntries[i]->length);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curFilePos += 8;
	}

	__goto_writeerror:
	if (errorLogger) errorLogger("AssetsList : A file write error occured!");
	return false;
}
ASSETSTOOLS_API bool AssetsList::Write(IAssetsReader *pReader, 
	IAssetsWriter *pWriter, bool doWriteAssets, QWORD &curFilePos, QWORD *curWritePos,
	AssetsFileVerifyLogger errorLogger)
{
	uint32_t estimatedBeginOffs, curEstimatedOffset;
	QWORD writePos = curWritePos ? (*curWritePos) : (QWORD)-1;
	uint32_t dwTmp;
	dwTmp = SwapEndians(this->count);
	if (!pWriter->Write(writePos, 4, &dwTmp))
		goto __goto_writeerror;
	estimatedBeginOffs = 4;
	for (unsigned int i = 0; i < this->count; i++)
	{
		estimatedBeginOffs += (uint32_t)strlen(ppEntries[i]->name)+1 + 8;
	}
	curEstimatedOffset = estimatedBeginOffs;
	if (curWritePos)
		*curWritePos += curEstimatedOffset;

	for (unsigned int i = 0; i < this->count; i++)
	{
		uint32_t nameStringLen = (uint32_t)strlen(ppEntries[i]->name)+1;
		if (!pWriter->Write(-1, nameStringLen, &ppEntries[i]->name))
			goto __goto_writeerror;	
		curEstimatedOffset = (curEstimatedOffset + 3) & (~3);
		dwTmp = SwapEndians(curEstimatedOffset/*ppEntries[i]->offset*/);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		dwTmp = SwapEndians(ppEntries[i]->length);
		if (!pWriter->Write(-1, 4, &dwTmp))
			goto __goto_writeerror;
		curEstimatedOffset += ppEntries[i]->length;
	}

	if (doWriteAssets)
	{
		uint8_t _stackTransferBuffer[256];
		uint32_t transferBufferLen = 256;
		uint8_t *pTransferBuffer = (uint8_t*)malloc(1024 * 1024);
		if (!pTransferBuffer)
			pTransferBuffer = _stackTransferBuffer;
		else
			transferBufferLen = 1024 * 1024;

		QWORD relPos = estimatedBeginOffs;

		for (unsigned int i = 0; i < this->count; i++)
		{
			uint32_t nullCount = 3 - (((relPos & 3) - 1) & 3);
			dwTmp = 0;
			pWriter->Write(-1, nullCount, &dwTmp);
			uint32_t remaining = ppEntries[i]->length;
			bool setReadPos = false;
			while (remaining > transferBufferLen)
			{
				pReader->Read(setReadPos ? -1 : (this->pos + ppEntries[i]->offset), transferBufferLen, pTransferBuffer);
				pWriter->Write(-1, transferBufferLen, pTransferBuffer);
				setReadPos = true;
			}
			if (remaining)
			{
				pReader->Read(setReadPos ? -1 : (this->pos + ppEntries[i]->offset), remaining, pTransferBuffer);
				pWriter->Write(-1, remaining, pTransferBuffer);
			}
			relPos += nullCount + ppEntries[i]->length;
		}
		if (pTransferBuffer != _stackTransferBuffer)
			free(pTransferBuffer);

		curFilePos += relPos;
	}
	return true;

	__goto_writeerror:
	if (errorLogger) errorLogger("AssetsList : A file write error occured!");
	return false;
}

ASSETSTOOLS_API AssetBundleFile::AssetBundleFile()
{
	bundleHeader3.blockCount = 0;
	bundleHeader3.pBlockList = NULL;
	assetsLists3 = NULL;
}
ASSETSTOOLS_API AssetBundleFile::~AssetBundleFile()
{
	this->Close();
}
ASSETSTOOLS_API void AssetBundleFile::Close()
{
	if (bundleHeader3.fileVersion < 6)
	{
		if (bundleHeader3.blockCount > 0)
		{
			free(bundleHeader3.pBlockList);
			bundleHeader3.pBlockList = NULL;
		}
		if (assetsLists3)
		{
			bundleHeader3.blockCount = 0;
			assetsLists3->Free();
			free(assetsLists3);
		}
		assetsLists3 = NULL;
	}
	else
	{
		if (bundleInf6)
		{
			bundleInf6->Free();
			free(bundleInf6);
		}
		bundleInf6 = NULL;
	}
}
static void *SzAlloc(void *p, size_t size) { (p); return malloc(size); }
static void SzFree(void *p, void *address) {  (p); free(address); }
static ISzAlloc g_Alloc = { SzAlloc, SzFree };

static SRes LZMADecompressBlock(QWORD *fileInPos, QWORD *fileOutPos, QWORD compressedSize, IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	#define SizePerBuffer (1024*1024)
	unsigned char header[LZMA_PROPS_SIZE/* + 8*/];
	if (pReader->Read(*fileInPos, sizeof(header), header) < sizeof(header))
	{
		return SZ_ERROR_FAIL;
	}
	CLzmaDec dec;
	LzmaDec_Construct(&dec);
	SRes res = LzmaDec_Allocate(&dec, header, LZMA_PROPS_SIZE, &g_Alloc);
	if (res != SZ_OK)
	{
		return SZ_ERROR_FAIL;
	}
	LzmaDec_Init(&dec);
	QWORD compProcessCount = sizeof(header);
	QWORD decompCount = 0;
	void *pCompBuf = malloc((compressedSize > SizePerBuffer) ? SizePerBuffer : compressedSize);
	if (!pCompBuf)
	{
		LzmaDec_Free(&dec, &g_Alloc);
		return SZ_ERROR_MEM;
	}
	void *pDecompBuf = malloc(SizePerBuffer);
	if (!pDecompBuf)
	{
		LzmaDec_Free(&dec, &g_Alloc);
		free(pCompBuf);
		return SZ_ERROR_MEM;
	}

	while (compProcessCount < compressedSize)
	{
		QWORD bytesToProcess = compressedSize - compProcessCount;
		bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
		QWORD bytesAvailable = pReader->Read(*fileInPos + compProcessCount, bytesToProcess, pCompBuf);
		if (bytesAvailable != bytesToProcess)
		{
			LzmaDec_Free(&dec, &g_Alloc);
			free(pCompBuf);
			free(pDecompBuf);
			return SZ_ERROR_INPUT_EOF;
		}
		
		SizeT compLen = bytesAvailable;
		SizeT decompLen = SizePerBuffer;
		ELzmaStatus stat = LZMA_STATUS_NOT_SPECIFIED;
		res = LzmaDec_DecodeToBuf(&dec, (Byte*)pDecompBuf, &decompLen, (Byte*)pCompBuf, &compLen, 
			LZMA_FINISH_ANY, &stat);
		//if ((res == SZ_OK) && !decompLen)
		//	res = SZ_ERROR_FAIL;
		if (res != SZ_OK)
		{
			LzmaDec_Free(&dec, &g_Alloc);
			free(pCompBuf);
			free(pDecompBuf);
			return res;
		}
		compProcessCount += compLen;
		if (pWriter->Write(*fileOutPos + decompCount, decompLen, pDecompBuf) < decompLen)
		{
			LzmaDec_Free(&dec, &g_Alloc);
			free(pCompBuf);
			free(pDecompBuf);
			return SZ_ERROR_OUTPUT_EOF;
		}
		decompCount += decompLen;
	}
	LzmaDec_Free(&dec, &g_Alloc);
	free(pCompBuf);
	free(pDecompBuf);
	*fileInPos += compProcessCount;
	*fileOutPos += decompCount;
	return SZ_OK;
}
struct LZ4DecompressBlock_Read_User_t
{
	QWORD dataPos;
	QWORD dataSize;
	IAssetsReader *pReader;
};
static int LZ4DecompressBlock_Read(void *buffer, int size, LZ4e_instream_t *stream)
{
	if (size <= 0) return 0;
	LZ4DecompressBlock_Read_User_t *user = (LZ4DecompressBlock_Read_User_t*)stream->user;

	QWORD readSize = (QWORD)size;
	if (stream->pos >= user->dataSize) return 0;
	if ((stream->pos + size) > user->dataSize)
		readSize = user->dataSize - stream->pos;

	return (int) user->pReader->Read(stream->pos + user->dataPos, readSize, buffer, false);
}
static int LZ4DecompressBlock_Write(const void *buffer, int size, LZ4e_outstream_t *stream)
{
	if (size <= 0) return 0;
	const uint8_t *pCur = (const uint8_t*)buffer; int remaining = size; int lastRead;
	while (remaining && (lastRead = (int) ((IAssetsWriter*)stream->user)->Write((QWORD)size, buffer)) > 0)
	{
		pCur += lastRead;
		remaining -= lastRead;
	}
	return size - remaining;//(int) ((IAssetsWriter*)stream->user)->Write((QWORD)size, buffer);
}
static bool LZ4DecompressBlock(QWORD *fileInPos, QWORD *fileOutPos, QWORD compressedSize, IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	static const size_t compBufferSize = (1*1024*1024);
	static const size_t decompBufferSize = (1*1024*1024);
	LZ4DecompressBlock_Read_User_t inuser;
	LZ4e_instream_t instream;
	LZ4e_outstream_t outstream;
	void *pCompBuf = malloc(compBufferSize + decompBufferSize);
	if (!pCompBuf)
		return false;
	void *pDecompBuf = ((uint8_t*)pCompBuf) + compBufferSize;

	instream.pos = 0;
	instream.callback = LZ4DecompressBlock_Read;
	instream.user = &inuser;
	inuser.dataPos = *fileInPos;
	inuser.dataSize = compressedSize;
	inuser.pReader = pReader;

	outstream.callback = LZ4DecompressBlock_Write;
	outstream.user = pWriter;

	QWORD oldWriterPos = *fileOutPos;
	pWriter->Tell(oldWriterPos);

	bool ret = LZ4e_decompress_safe((char*)pCompBuf, (char*)pDecompBuf, compBufferSize, decompBufferSize, &instream, &outstream) > 0;

	QWORD newWriterPos = oldWriterPos;
	pWriter->Tell(newWriterPos);
	*fileOutPos += (newWriterPos - oldWriterPos);

	*fileInPos += compressedSize;

	free(pCompBuf);
	return ret;
}
static bool LZ4CompressBlock(QWORD *fileInPos, QWORD *fileOutPos, QWORD decompressedSize, IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	if (decompressedSize > 0x7FFFFFFF)
		return false;
	LZ4DecompressBlock_Read_User_t inuser;
	LZ4e_instream_t instream;
	LZ4e_outstream_t outstream;

	instream.pos = 0;
	instream.callback = LZ4DecompressBlock_Read;
	instream.user = &inuser;
	inuser.dataPos = *fileInPos;
	inuser.dataSize = decompressedSize;
	inuser.pReader = pReader;

	outstream.callback = LZ4DecompressBlock_Write;
	outstream.user = pWriter;

	QWORD oldWriterPos = *fileOutPos;
	pWriter->Tell(oldWriterPos);

	bool ret = LZ4e_compress_fast(&instream, &outstream, 1, (unsigned int)decompressedSize) > 0;

	QWORD newWriterPos = oldWriterPos;
	pWriter->Tell(newWriterPos);
	*fileOutPos += (newWriterPos - oldWriterPos);

	*fileInPos += decompressedSize;
	return ret;
}

ASSETSTOOLS_API bool AssetBundleFile::Unpack(IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	if (!Read(pReader, NULL, true))
		return false;
	//bundleHeader6.fileVersion == bundleHeader3.fileVersion
	if (bundleHeader6.fileVersion >= 6)
	{
		uint8_t compressionType = (bundleHeader6.flags & 0x3F);
		if (compressionType < 4)
		{
			QWORD curFilePos = 0;
			QWORD curUpFilePos = 0;
			if (bundleHeader6.flags & 0x100) //originally was UnityWeb
				strcpy(bundleHeader6.signature, "UnityWeb");
			bundleHeader6.Write(pWriter, curFilePos);
			if (bundleHeader6.flags & 0x100) //originally was UnityWeb
				strcpy(bundleHeader6.signature, "UnityFS");
			curFilePos = bundleHeader6.GetBundleInfoOffset();
			curUpFilePos = curFilePos;
			
			void *fileTableBuf = malloc(bundleHeader6.decompressedSize);
			if (!fileTableBuf)
				return false;
			IAssetsWriter *pTempWriter = Create_AssetsWriterToMemory(fileTableBuf, bundleHeader6.decompressedSize);
			if (!pTempWriter)
			{
				free(fileTableBuf);
				return false;
			}
			curUpFilePos = 0;
			bool decompressSuccess = false;
			switch (compressionType)
			{
			case 0:
				if (bundleHeader6.compressedSize == bundleHeader6.decompressedSize
					&& pReader->Read(curFilePos, bundleHeader6.compressedSize, fileTableBuf) == bundleHeader6.decompressedSize)
					decompressSuccess = true;
				break;
			case 1: //LZMA
				if (LZMADecompressBlock(&curFilePos, &curUpFilePos, bundleHeader6.compressedSize, pReader, pTempWriter) == SZ_OK)
					decompressSuccess = true;
				break;
			case 2: case 3: //LZ4
				if (LZ4DecompressBlock(&curFilePos, &curUpFilePos, bundleHeader6.compressedSize, pReader, pTempWriter))
					decompressSuccess = true;
				break;
			}
			Free_AssetsWriter(pTempWriter);
			if (!decompressSuccess || curUpFilePos != bundleHeader6.decompressedSize)
			{
				free(fileTableBuf);
				return false;
			}
			
			IAssetsReader *pTempReader = Create_AssetsReaderFromMemory(fileTableBuf, bundleHeader6.decompressedSize, false);
			if (!pTempReader)
			{
				free(fileTableBuf);
				return false;
			}
			AssetBundleBlockAndDirectoryList06 list = {0};
			bool res = list.Read(0, pTempReader);
			Free_AssetsReader(pTempReader);
			free(fileTableBuf);
			curFilePos = bundleHeader6.GetFileDataOffset();
			if (bundleHeader6.flags & 0x80)
				curUpFilePos = bundleHeader6.GetFileDataOffset();
			else
			{
				curUpFilePos = bundleHeader6.GetBundleInfoOffset();
				QWORD oldUpFilePos = curUpFilePos;
				list.Write(pWriter, curUpFilePos);
				bundleHeader6.decompressedSize = (uint32_t)(curUpFilePos - oldUpFilePos);
			}

			if (!res)
			{
				free(fileTableBuf);
				return false;
			}
			for (uint32_t i = 0; i < list.blockCount; i++)
			{
				//QWORD oldUpFilePos = curUpFilePos;
				switch (list.blockInf[i].GetCompressionType())
				{
				case 0: //none
					if (list.blockInf[i].compressedSize == list.blockInf[i].decompressedSize)
					{
						uint32_t copiedCount = 0;
						uint32_t copyBufLen = 1024 * 1024;
						void *copyBuf = malloc(1024 * 1024); uint8_t tmp[256];
						if (!copyBuf)
						{
							copyBuf = tmp; copyBufLen = 256;
						}
						while (copiedCount < list.blockInf[i].compressedSize)
						{
							uint32_t bytesToCopy = copyBufLen;
							if (bytesToCopy > (list.blockInf[i].compressedSize - copiedCount))
								bytesToCopy = (list.blockInf[i].compressedSize - copiedCount);
							uint32_t bytesRead = (uint32_t)pReader->Read(curFilePos, bytesToCopy, copyBuf);
							uint32_t bytesWritten = (uint32_t)pWriter->Write(curUpFilePos, bytesRead, copyBuf);
							curFilePos += bytesRead;
							curUpFilePos += bytesWritten;
							if (bytesRead != bytesToCopy || bytesWritten != bytesRead)
							{
								if (copyBuf != tmp)
									free(copyBuf);
								list.Free();
								return false;
								//break;
							}
							copiedCount += bytesRead;
						}
						if (copyBuf != tmp)
							free(copyBuf);
					}
					else
					{
						list.Free();
						return false;
					}
					break;
				case 1: //LZMA
					if (LZMADecompressBlock(&curFilePos, &curUpFilePos, list.blockInf[i].compressedSize, pReader, pWriter) != SZ_OK)
					{
						list.Free();
						return false;
					}
					break;
				case 2:
				case 3:
					if (!LZ4DecompressBlock(&curFilePos, &curUpFilePos, list.blockInf[i].compressedSize, pReader, pWriter))
					{
						list.Free();
						return false;
					}
					break;
				default:
					{
						list.Free();
						return false;
					}
					break;
				}
				
				list.blockInf[i].compressedSize = list.blockInf[i].decompressedSize;
				list.blockInf[i].flags &= ~(0x3F); //compression = 0
				//list.blockInf[i].decompressedSize = curUpFilePos - oldUpFilePos;
			}
			if (bundleHeader6.flags & 0x80)
			{
				QWORD oldUpFilePos = curUpFilePos;
				list.Write(pWriter, curUpFilePos);
				bundleHeader6.decompressedSize = (uint32_t)(curUpFilePos - oldUpFilePos);
			}
			bundleHeader6.totalFileSize = curUpFilePos;
			bundleHeader6.compressedSize = bundleHeader6.decompressedSize;
			curUpFilePos = 0;
			bundleHeader6.flags &= ~(0x3F);

			if (bundleHeader6.flags & 0x100) //originally was UnityWeb
				strcpy(bundleHeader6.signature, "UnityWeb");
			bundleHeader6.Write(pWriter, curUpFilePos);
			if (bundleHeader6.flags & 0x100) //originally was UnityWeb
				strcpy(bundleHeader6.signature, "UnityFS");
			if (bundleHeader6.flags & 0x80)
				curUpFilePos = bundleHeader6.GetBundleInfoOffset();
			list.Write(pWriter, curUpFilePos);
			list.Free();
			return true;
		}
	}
	else if (bundleHeader3.fileVersion == 3)
	{
		if (!strcmp(bundleHeader3.signature, "UnityWeb"))
		{
			if ((bundleHeader3.blockCount == 0) || (bundleHeader3.pBlockList[0].compressed < (LZMA_PROPS_SIZE+8)))
				return false;
	#define SizePerBuffer (1024*1024)
			void *buffers = malloc(2 * SizePerBuffer);
			if (!buffers)
				return false;
			uint8_t *compBuffer = (uint8_t*)buffers;
			uint8_t *decompBuffer = &compBuffer[SizePerBuffer];
			QWORD curFilePos = 0;
			QWORD curUpFilePos = 0;
			bundleHeader3.Write(pWriter, curFilePos);
			curFilePos = bundleHeader3.bundleDataOffs;
			curUpFilePos = bundleHeader3.bundleDataOffs;

			/*QWORD compressedSize = 0;
			QWORD decompressedSize = 0;
			for (uint32_t i = 0; i < bundleHeader.levelCount; i++)
			{
				if (bundleHeader.pLevelList[i].uncompressed > decompressedSize)
				{
					compressedSize = bundleHeader.pLevelList[i].compressed;
					decompressedSize = bundleHeader.pLevelList[i].uncompressed;
				}
			}*/
			QWORD decompCount = 0;
			//for (uint32_t i = 0; i < bundleHeader.bundleCount; i++)
			{
				unsigned char header[LZMA_PROPS_SIZE + 8];
				if (!pReader->Read(curFilePos, sizeof(header), header))
				{
					free(buffers);
					return false;
				}
				CLzmaDec dec;
				LzmaDec_Construct(&dec);
				SRes res = LzmaDec_Allocate(&dec, header, LZMA_PROPS_SIZE, &g_Alloc);
				if (res != SZ_OK)
				{
					free(buffers);
					return false;
				}
				LzmaDec_Init(&dec);
				QWORD compProcessCount = 0;
			
				AssetsList decompEntryList; ZeroMemory(&decompEntryList, sizeof(AssetsList));
				{
					//read the entry list
					QWORD bytesToProcess = bundleHeader3.pBlockList[0].compressed - compProcessCount;
					bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
					pReader->Read(curFilePos + sizeof(header) + compProcessCount, bytesToProcess, compBuffer);
					SizeT compLen = bytesToProcess;
					SizeT decompLen = SizePerBuffer;
					ELzmaStatus stat = LZMA_STATUS_NOT_SPECIFIED;
					res = LzmaDec_DecodeToBuf(&dec, decompBuffer, &decompLen, compBuffer, &compLen, 
						LZMA_FINISH_ANY, &stat);
					if ((compLen == 0) || (res != SZ_OK))
					{
						LzmaDec_Free(&dec, &g_Alloc);
						free(buffers);
						return false;
					}
					IAssetsReader *pMemoryReader = Create_AssetsReaderFromMemory(decompBuffer, decompLen, false);
					if (pMemoryReader == NULL)
					{
						LzmaDec_Free(&dec, &g_Alloc);
						free(buffers);
						return false;
					}
					QWORD _qwTmp = 0;
					decompEntryList.Read(pMemoryReader, _qwTmp);
					Free_AssetsReader(pMemoryReader);
				
					//sort the entries (this one is slow but the entry lists aren't that large
					bool repeat = false;
					for (uint32_t i = 0; i < decompEntryList.count; i++)
					{
						if (repeat)
							i--;
						repeat = false;
						for (uint32_t k = i+1; k < decompEntryList.count; k++)
						{
							if (decompEntryList.ppEntries[k]->offset < decompEntryList.ppEntries[i]->offset)
							{
								AssetBundleEntry *pEntry = decompEntryList.ppEntries[k];
								memcpy(&decompEntryList.ppEntries[i+1], &decompEntryList.ppEntries[i], (k - i) * sizeof(void*));
								decompEntryList.ppEntries[i] = pEntry;
								//repeat = true;
								//firstFileOffs = decompEntryList.ppEntries[i]->offset;
							}
						}
					}
					pWriter->Write(curUpFilePos, decompEntryList.ppEntries[0]->offset, decompBuffer);
				
					//get the exact amount of compressed bytes to the first file
					//reset the decoder (it possibly isn't able to revert its position)
					LzmaDec_Free(&dec, &g_Alloc);
					LzmaDec_Construct(&dec);
					LzmaDec_Allocate(&dec, header, LZMA_PROPS_SIZE, &g_Alloc);
					LzmaDec_Init(&dec);

					compLen = bytesToProcess;
					decompLen = decompEntryList.ppEntries[0]->offset;
					stat = LZMA_STATUS_NOT_SPECIFIED;
					res = LzmaDec_DecodeToBuf(&dec, decompBuffer, &decompLen, compBuffer, &compLen, 
						LZMA_FINISH_ANY, &stat);

					compProcessCount += compLen;
					decompCount += decompLen;
				}
			
				uint32_t levelIndex = 0;
				for (uint32_t i = 0; i < decompEntryList.count; i++)
				{
					QWORD decompressBytes = decompEntryList.ppEntries[i]->length;
					if ((i+1) < decompEntryList.count)
					{
						//add empty bytes to read in between (if there are any)
						decompressBytes += decompEntryList.ppEntries[i+1]->offset - 
								(decompEntryList.ppEntries[i]->offset + decompEntryList.ppEntries[i]->length);
					}
					do
					{
						ZeroMemory(buffers, 2 * SizePerBuffer);
						//QWORD bytesToProcess = compressedSize - compProcessCount;
						//bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
						QWORD bytesToDecompress = (decompEntryList.ppEntries[i]->offset + decompressBytes) - decompCount;
						bytesToDecompress = bytesToDecompress > SizePerBuffer ? SizePerBuffer : bytesToDecompress;
						pReader->Read(curFilePos + sizeof(header) + compProcessCount, SizePerBuffer, compBuffer);
						SizeT compLen = SizePerBuffer;//bytesToProcess;
						SizeT decompLen = bytesToDecompress;
						ELzmaStatus stat = LZMA_STATUS_NOT_SPECIFIED;
						res = LzmaDec_DecodeToBuf(&dec, decompBuffer, &decompLen, compBuffer, &compLen, 
							LZMA_FINISH_ANY, &stat);
						if (compLen == 0 || res != SZ_OK)
						{
							LzmaDec_Free(&dec, &g_Alloc);
							free(buffers);
							return false;
						}
						pWriter->Write(curUpFilePos + decompCount, decompLen, decompBuffer);
						compProcessCount += compLen;
						decompCount += decompLen;
					} while (decompCount < (decompEntryList.ppEntries[i]->offset + decompressBytes));
					if ((1 == decompEntryList.count || 
						!strcmp(decompEntryList.ppEntries[i]->name, "mainData") || 
						!strncmp(decompEntryList.ppEntries[i]->name, "level", 5)) && 
						(levelIndex < this->bundleHeader3.blockCount))
					{
						this->bundleHeader3.pBlockList[levelIndex].compressed = 
							this->bundleHeader3.pBlockList[levelIndex].uncompressed =
							(decompEntryList.ppEntries[i]->offset + decompEntryList.ppEntries[i]->length);
						levelIndex++;
					}
				}
				LzmaDec_Free(&dec, &g_Alloc);
				decompEntryList.Free();
				//curFilePos += bundleHeader.pBundleList[i].compressed;
				//curUpFilePos += bundleHeader.pBundleList[i].uncompressed;
			}
			free(buffers);
			AssetBundleHeader03 headerCopy;
			memcpy(&headerCopy, &this->bundleHeader3, sizeof(AssetBundleHeader03));
			strcpy(headerCopy.signature, "UnityRaw");
			headerCopy.minimumStreamedBytes = headerCopy.fileSize2 = headerCopy.bundleDataOffs + (uint32_t)decompCount;
			/*for (uint32_t i = 0; i < bundleHeader.bundleCount; i++)
			{
				headerCopy.pBundleList[i].compressed = headerCopy.pBundleList[i].uncompressed;
			}*/
			/*if (bundleHeader.bundleCount)
			{
				bundleHeader.bundleCount = 1;
				headerCopy.pBundleList[0].compressed = headerCopy.pBundleList[0].uncompressed = decompressedSize;
			}*/
			curFilePos = 0;
			headerCopy.Write(pWriter, curFilePos);
			return true;
		}
	}
	return false;
}

typedef struct
{
	ISeqInStream SeqInStream;
	std::mutex critSect;
	std::mutex critSectReading; //To sync freeing the stream.
	HANDLE hDataEvent; HANDLE hRequestDataEvent; //TODO: Replace by some std mechanism.
	bool isDone;
	size_t bufferSize;
	uint8_t *dataBuffer;
} LZMAInStream;
static SRes LZMAInStream_Read(void *p, void *buf, size_t *size)
{
	LZMAInStream *ctx = (LZMAInStream*)p;
	if (ctx->isDone)
	{
		*size = 0;
		return SZ_OK;
	}
	size_t readSize = 0;
	
	std::scoped_lock critSectReadingLock(ctx->critSectReading);
	{
		std::unique_lock critSectLock(ctx->critSect);
		ResetEvent(ctx->hDataEvent);
		size_t toCopy = std::min<size_t>(ctx->bufferSize, (*size) - readSize);
		memcpy(&((uint8_t*)buf)[readSize], ctx->dataBuffer, toCopy);
		readSize += toCopy;

		uint8_t *bufferTemp = new uint8_t[ctx->bufferSize - toCopy];
		memcpy(bufferTemp, &ctx->dataBuffer[toCopy], ctx->bufferSize - toCopy);
		if (ctx->dataBuffer) delete[] ctx->dataBuffer;
		ctx->dataBuffer = bufferTemp;
		ctx->bufferSize -= toCopy;
		critSectLock.unlock();
		if (readSize == 0)//< (*size))
		{
			SetEvent(ctx->hRequestDataEvent);
			if ((WaitForSingleObjectEx(ctx->hDataEvent, INFINITE, FALSE) != WAIT_OBJECT_0) || ctx->isDone)
			{
				*size = readSize;
				return SZ_OK;
			}
			critSectLock.lock();
			size_t toCopy = std::min<size_t>(ctx->bufferSize, (*size) - readSize);
			memcpy(&((uint8_t*)buf)[readSize], ctx->dataBuffer, toCopy);
			readSize += toCopy;

			uint8_t *bufferTemp = new uint8_t[ctx->bufferSize - toCopy];
			memcpy(bufferTemp, &ctx->dataBuffer[toCopy], ctx->bufferSize - toCopy);
			if (ctx->dataBuffer) delete[] ctx->dataBuffer;
			ctx->dataBuffer = bufferTemp;
			ctx->bufferSize -= toCopy;
		}
	}
	*size = readSize;
	return SZ_OK;
}
static void LZMAInStream_Init(LZMAInStream *pStream)
{
	pStream->SeqInStream.Read = LZMAInStream_Read;
	pStream->hDataEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pStream->hRequestDataEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pStream->isDone = false;
	pStream->bufferSize = 0;
	pStream->dataBuffer = NULL;
}
static void LZMAInStream_Free(LZMAInStream *pStream)
{
	pStream->isDone = true;
	SetEvent(pStream->hDataEvent);
	SetEvent(pStream->hRequestDataEvent);
	Sleep(0); //Prevent entering the critical section between LZMAInStream_Read's isDone check and its EnterCriticalSection.
	pStream->critSectReading.lock();
	pStream->critSectReading.unlock();
	CloseHandle(pStream->hDataEvent);
	CloseHandle(pStream->hRequestDataEvent);
	if (pStream->dataBuffer)
	{
		delete[] pStream->dataBuffer;
		pStream->dataBuffer = NULL;
	}
	pStream->bufferSize = 0;
}
static void LZMAInStream_Put(LZMAInStream *pStream, void *data, size_t size, bool waitForData)
{
	if (pStream->isDone)
		return;
	std::unique_lock critSectLock(pStream->critSect);
	uint8_t *bufferTemp = new uint8_t[pStream->bufferSize + size];
	memcpy(bufferTemp, pStream->dataBuffer, pStream->bufferSize);
	memcpy(&bufferTemp[pStream->bufferSize], data, size);
	if (pStream->dataBuffer) delete[] pStream->dataBuffer;
	pStream->dataBuffer = bufferTemp;
	pStream->bufferSize += size;
	SetEvent(pStream->hDataEvent);
	critSectLock.unlock();
	if (waitForData)
		WaitForSingleObject(pStream->hRequestDataEvent, INFINITE);
}

typedef struct
{
	ISeqOutStream SeqOutStream;
	IAssetsWriter *pWriter;
	QWORD writeOffset;
	QWORD writeCount;
} LZMAOutStream;
static size_t LZMAOutStream_Write(void *p, const void *buf, size_t size)
{
	LZMAOutStream *ctx = (LZMAOutStream*)p;
	ctx->pWriter->Write(ctx->writeOffset + ctx->writeCount, size, buf);
	ctx->writeCount += size;
	return size;
}

struct LZMACompressThreadData
{
	LZMAInStream *pIn;
	LZMAOutStream *pOut;
	CLzmaEncHandle enc;
	SRes result;
};
static void LZMACompressThread(void *pData)
{
	LZMACompressThreadData *pThreadData = (LZMACompressThreadData*)pData;
	pThreadData->result = LzmaEnc_Encode(pThreadData->enc, &pThreadData->pOut->SeqOutStream, &pThreadData->pIn->SeqInStream, NULL, &g_Alloc, &g_Alloc);
	//LZMAInStream_Free(pThreadData->pIn);
	pThreadData->pIn->isDone = true;
	SetEvent(pThreadData->pIn->hRequestDataEvent);
}


static SRes LZMACompressBlock(QWORD *fileInPos, QWORD *fileOutPos, QWORD decompressedSize, uint32_t alignmentBytes, IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	#define SizePerBuffer (1024*1024)
	CLzmaEncHandle enc = LzmaEnc_Create(&g_Alloc);
	if (!enc)
	{
		return SZ_ERROR_FAIL;
	}

	CLzmaEncProps props;
	LzmaEncProps_Init(&props);
	props.level = 0;
	props.writeEndMark = 1;
#ifdef _DEBUG
	props.dictSize = 4096;
#else
	props.dictSize = 524288;
#endif
	props.numThreads = 2;

	SRes res = LzmaEnc_SetProps(enc, &props);
	if (res != SZ_OK)
	{
		LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
		return res;
	}

	size_t headerSize = LZMA_PROPS_SIZE;
	unsigned char header[LZMA_PROPS_SIZE];
	LzmaEnc_WriteProperties(enc, header, &headerSize);
	if (res != SZ_OK || headerSize != LZMA_PROPS_SIZE)
	{
		LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
		return SZ_ERROR_FAIL;
	}
	pWriter->Write(*fileOutPos, headerSize, header);
	QWORD decompProcessCount = 0;
	void *pDecompBuf = malloc((decompressedSize > SizePerBuffer) ? SizePerBuffer : decompressedSize);
	if (!pDecompBuf)
	{
		LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
		return SZ_ERROR_MEM;
	}

	LZMAInStream seqInStream; LZMAInStream_Init(&seqInStream);
	LZMAOutStream seqOutStream = {&LZMAOutStream_Write, pWriter, *fileOutPos, headerSize};
	LZMACompressThreadData threadData;
	threadData.pIn = &seqInStream;
	threadData.pOut = &seqOutStream;
	threadData.enc = enc;
	threadData.result = SZ_OK;
	
	std::future<void> compressFuture = std::async(LZMACompressThread, &threadData);

	if (alignmentBytes > 0)
	{
		uint8_t *alignment = new uint8_t[alignmentBytes];
		memset(alignment, 0, alignmentBytes);
		LZMAInStream_Put(&seqInStream, alignment, alignmentBytes, true);
		delete[] alignment;
	}

	SRes ret = SZ_OK;
	while (decompProcessCount < decompressedSize)
	{
		QWORD bytesToProcess = decompressedSize - decompProcessCount;
		bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
		QWORD bytesAvailable = pReader->Read(*fileInPos + decompProcessCount, bytesToProcess, pDecompBuf);
		if (bytesAvailable != bytesToProcess)
		{
			ret = SZ_ERROR_INPUT_EOF;
			goto _GOTO_DO_RETURN;
		}
		
		LZMAInStream_Put(&seqInStream, pDecompBuf, bytesToProcess, true);

		decompProcessCount += bytesToProcess;
	}
_GOTO_DO_RETURN:
	free(pDecompBuf);
	LZMAInStream_Free(&seqInStream);
	assert(compressFuture.valid());
	compressFuture.wait();
	LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
	*fileOutPos += threadData.pOut->writeCount;
	*fileInPos += decompProcessCount;
	return ret;
}
/*static bool LZ4CompressBlocks(QWORD *fileInPos, QWORD *fileOutPos, QWORD decompressedSize, IAssetsReader *pReader, IAssetsWriter *pWriter,
	std::vector<AssetBundleOffsetPair> &blockSizes)
{
	#define SizePerBuffer (2*1024*1024)
	LZ4_stream_t *pStream = LZ4_createStream();
	if (!pStream)
		return false;
	memset(pStream, 0, sizeof(LZ4_stream_t));
	QWORD decompProcessCount = 0;
	QWORD compCount = 0;
	//pDecompBuf is a ring buffer with <decompBufCount> elements. LZ4 uses the previous buffers as a dictionary, except the one to be compressed at a time.
	size_t decompSizePerBuffer = (decompressedSize > SizePerBuffer) ? SizePerBuffer : decompressedSize;
#define decompBufCount 2
	uint8_t *pDecompBuf = (uint8_t*)malloc(decompBufCount * decompSizePerBuffer);
	if (!pDecompBuf)
	{
		LZ4_freeStream(pStream);
		return false;
	}
	size_t decompBufIndex = 0;
	void *pCompBuf = malloc(LZ4_COMPRESSBOUND(SizePerBuffer));
	if (!pCompBuf)
	{
		LZ4_freeStream(pStream);
		free(pDecompBuf);
		return false;
	}
	while (decompProcessCount < decompressedSize)
	{
		QWORD bytesToProcess = decompressedSize - decompProcessCount;
		bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
		QWORD bytesAvailable = pReader->Read(*fileInPos + decompProcessCount, bytesToProcess, &pDecompBuf[decompBufIndex*decompSizePerBuffer]);
		if (bytesAvailable != bytesToProcess)
		{
			LZ4_freeStream(pStream);
			free(pCompBuf);
			free(pDecompBuf);
			return false;
		}

		SizeT decompLen = bytesAvailable;
		SizeT compLen = LZ4_COMPRESSBOUND(SizePerBuffer);
		compLen = LZ4_compress_fast_continue(pStream, (char*)&pDecompBuf[decompBufIndex*decompSizePerBuffer], (char*)pCompBuf, decompLen, compLen, 1);
		if (compLen == 0)
		{
			LZ4_freeStream(pStream);
			free(pCompBuf);
			free(pDecompBuf);
			return false;
		}
		AssetBundleOffsetPair sizePair; sizePair.compressed = (uint32_t)compLen; sizePair.uncompressed = (uint32_t)decompLen;
		blockSizes.push_back(sizePair);
		decompProcessCount += decompLen;
		decompBufIndex++;
		if (decompBufIndex > decompBufCount)
			decompBufIndex = 0;
		if (pWriter->Write(*fileOutPos + compCount, compLen, pCompBuf) < compLen)
		{
			LZ4_freeStream(pStream);
			free(pCompBuf);
			free(pDecompBuf);
			return false;
		}
		compCount += compLen;
	}
	LZ4_freeStream(pStream);
	free(pCompBuf);
	free(pDecompBuf);
	*fileInPos += decompProcessCount;
	*fileOutPos += compCount;
	return true;
}*/

static bool CopyBlock(QWORD *fileInPos, QWORD *fileOutPos, QWORD totalSize, IAssetsReader *pReader, IAssetsWriter *pWriter)
{
	#define SizePerBuffer (1024*1024)
	QWORD processedSize = 0;
	void *pDecompBuf = malloc((totalSize > SizePerBuffer) ? SizePerBuffer : totalSize);
	if (!pDecompBuf)
	{
		return false;
	}

	bool ret = true;
	while (processedSize < totalSize)
	{
		QWORD bytesToProcess = totalSize - processedSize;
		bytesToProcess = bytesToProcess > SizePerBuffer ? SizePerBuffer : bytesToProcess;
		QWORD bytesAvailable = pReader->Read(*fileInPos + processedSize, bytesToProcess, pDecompBuf);
		if (bytesAvailable != bytesToProcess)
		{
			ret = false;
			goto _GOTO_DO_RETURN;
		}
		
		QWORD bytesProcessed = pWriter->Write(*fileOutPos + processedSize, bytesToProcess, pDecompBuf);
		processedSize += bytesProcessed;

		if (bytesProcessed != bytesToProcess)
		{
			ret = false;
			goto _GOTO_DO_RETURN;
		}
	}
_GOTO_DO_RETURN:
	free(pDecompBuf);
	*fileOutPos += processedSize;
	*fileInPos += processedSize;
	return ret;
}

ASSETSTOOLS_API bool AssetBundleFile::Pack(IAssetsReader *pReader, IAssetsWriter *pWriter, ECompressionTypes *settings, ECompressionTypes fileTableSettings)
{
	if (!Read(pReader, NULL, false))
		return false;
	if (bundleHeader3.fileVersion == 3)
	{
		if (!strcmp(bundleHeader3.signature, "UnityRaw"))
		{
			//sort the entries (this one is slow but the entry lists aren't that large)
			bool repeat = false;
			for (uint32_t i = 0; i < this->assetsLists3[0].count; i++)
			{
				if (repeat)
					i--;
				repeat = false;
				for (uint32_t k = i+1; k < this->assetsLists3[0].count; k++)
				{
					if (this->assetsLists3[0].ppEntries[k]->offset < this->assetsLists3[0].ppEntries[i]->offset)
					{
						AssetBundleEntry *pEntry = this->assetsLists3[0].ppEntries[k];
						memcpy(&this->assetsLists3[0].ppEntries[i+1], &this->assetsLists3[0].ppEntries[i], (k - i) * sizeof(void*));
						this->assetsLists3[0].ppEntries[i] = pEntry;
						//repeat = true;
						//firstFileOffs = decompEntryList.ppEntries[i]->offset;
					}
				}
			}
	#define SizePerBuffer (1024*1024)
			void *buffers = malloc(/*11 * */SizePerBuffer);
			if (!buffers)
				return false;
			std::vector<AssetBundleOffsetPair> pairs;

			uint8_t *decompBuffer = (uint8_t*)buffers;
			//uint8_t *compBuffer = &decompBuffer[SizePerBuffer];
			QWORD curFilePos = 0;
			QWORD curCompFilePos = 0;
			bundleHeader3.Write(pWriter, curFilePos);
			curFilePos = bundleHeader3.bundleDataOffs;
			curCompFilePos = bundleHeader3.bundleDataOffs;

			QWORD curCompressOffs = 0;
			//for (uint32_t i = 0; i < bundleHeader.bundleCount; i++)
			{
				CLzmaEncHandle enc = LzmaEnc_Create(&g_Alloc);
				if (!enc)
				{
					free(buffers);
					return false;
				}

				CLzmaEncProps props;
				LzmaEncProps_Init(&props);
				props.level = 0;
				props.writeEndMark = 1;
				props.dictSize = 524288;
				props.numThreads = 2;

				SRes res = LzmaEnc_SetProps(enc, &props);
				if (res != SZ_OK)
				{
					free(buffers);
					LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
					return false;
				}
				unsigned char header[LZMA_PROPS_SIZE+8];
				size_t headerSize = LZMA_PROPS_SIZE;
				LzmaEnc_WriteProperties(enc, header, &headerSize);
				if (res != SZ_OK || headerSize != LZMA_PROPS_SIZE)
				{
					free(buffers);
					LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
					return false;
				}
				headerSize = LZMA_PROPS_SIZE+8;
				*(QWORD*)&header[LZMA_PROPS_SIZE] = 0;
				QWORD lzmaHeaderFilePos = curFilePos;
				pWriter->Write(curFilePos, headerSize, header);
				curCompressOffs += headerSize; //?

				LZMAInStream seqInStream; LZMAInStream_Init(&seqInStream);
				LZMAOutStream seqOutStream = {&LZMAOutStream_Write, pWriter, lzmaHeaderFilePos, headerSize};
				LZMACompressThreadData threadData;
				threadData.pIn = &seqInStream;
				threadData.pOut = &seqOutStream;
				threadData.enc = enc;
				threadData.result = SZ_OK;
				std::future<void> compressFuture = std::async(LZMACompressThread, &threadData);

				QWORD curUncompressOffs = 0;
				uint32_t levelIndex = 0;
				{
					size_t destLen = SizePerBuffer;
					size_t srcLen;
					if (this->assetsLists3->count > 0)
						srcLen = this->assetsLists3->ppEntries[0]->offset;
					else
						srcLen = 4; //count dword (= 0)
					QWORD oldWriteCount = seqOutStream.writeCount;
					pReader->Read(curFilePos, srcLen, decompBuffer);
					LZMAInStream_Put(&seqInStream, decompBuffer, srcLen, true);
					destLen = seqOutStream.writeCount - oldWriteCount;
					/*SRes result = LzmaEnc_MemEncode(enc, compBuffer, &destLen, decompBuffer, srcLen, (this->assetsLists3->count > 0) ? 0 : 1, 
						NULL, &g_Alloc, &g_Alloc);
					if ((destLen == 0) || (result != SZ_OK))
					{
						free(buffers);
						LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
						return false;
					}
					writer(-1, destLen, compBuffer, writerPar);*/
					curCompressOffs += destLen;
					curUncompressOffs += srcLen;
					if (this->assetsLists3->count == 0 && this->bundleHeader3.blockCount > 0)
					{
						this->bundleHeader3.pBlockList[levelIndex].compressed = (uint32_t)curCompressOffs;
						this->bundleHeader3.pBlockList[levelIndex].uncompressed = (uint32_t)curUncompressOffs;
						levelIndex++;
					}
				}

				for (uint32_t i = 0; i < this->assetsLists3->count; i++)
				{
					QWORD compressBytes = this->assetsLists3->ppEntries[i]->length;
					if ((i+1) < this->assetsLists3->count)
					{
						//add empty bytes to compress in between (if there are any)
						compressBytes += this->assetsLists3->ppEntries[i+1]->offset - 
								(this->assetsLists3->ppEntries[i]->offset + this->assetsLists3->ppEntries[i]->length);
					}
					QWORD curBytesProcessed = 0;
					while (curBytesProcessed < compressBytes)
					{
						size_t writeLen = compressBytes - curBytesProcessed;
						size_t destLen;// = 10*SizePerBuffer;
						size_t srcLen = writeLen > SizePerBuffer ? SizePerBuffer : writeLen;
						QWORD oldWriteCount = seqOutStream.writeCount;
						pReader->Read(curFilePos + this->assetsLists3->ppEntries[i]->offset + curBytesProcessed, srcLen, decompBuffer);
						if (srcLen > 0)
							LZMAInStream_Put(&seqInStream, decompBuffer, srcLen, true);
						destLen = seqOutStream.writeCount - oldWriteCount;
						/*SRes result = LzmaEnc_MemEncode(enc, compBuffer, &destLen, decompBuffer, srcLen,
							(((i+1) >= this->assetsLists3->count) && (srcLen >= writeLen)) ? 1 : 0, 
							NULL, &g_Alloc, &g_Alloc);
						if ((destLen == 0) || (result != SZ_OK))
						{
							free(buffers);
							LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
							return false;
						}
						writer(-1, destLen, compBuffer, writerPar);*/
						curCompressOffs += destLen;
						curBytesProcessed += srcLen;
						curUncompressOffs += srcLen;
					}
					if ((1 == this->assetsLists3->count || 
							!strcmp(this->assetsLists3->ppEntries[i]->name, "mainData") || 
							!strncmp(this->assetsLists3->ppEntries[i]->name, "level", 5)) && 
							(levelIndex < this->bundleHeader3.blockCount))
					{
						this->bundleHeader3.pBlockList[levelIndex].compressed = (uint32_t)curCompressOffs;
						this->bundleHeader3.pBlockList[levelIndex].uncompressed = (uint32_t)curUncompressOffs;
						levelIndex++;
					}
				}
				LZMAInStream_Free(&seqInStream);
				assert(compressFuture.valid());
				compressFuture.wait();
				curCompressOffs = threadData.pOut->writeCount;
				LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);
				*(QWORD*)&header[LZMA_PROPS_SIZE] = curUncompressOffs;
				pWriter->Write(lzmaHeaderFilePos, headerSize, header);
			}
			free(buffers);
			AssetBundleHeader03 headerCopy;
			memcpy(&headerCopy, &this->bundleHeader3, sizeof(AssetBundleHeader03));
			strcpy(headerCopy.signature, "UnityWeb");
			headerCopy.minimumStreamedBytes = headerCopy.fileSize2 = headerCopy.bundleDataOffs + (uint32_t)curCompressOffs;
			curFilePos = 0;
			headerCopy.Write(pWriter, curFilePos);
			return true;
		}
	}
	else if (bundleHeader3.fileVersion >= 6)
	{
		//sort the entries (this one is slow but the entry lists aren't that large)
		bool repeat = false;
		for (uint32_t i = 0; i < this->bundleInf6->directoryCount; i++)
		{
			if (repeat)
				i--;
			repeat = false;
			for (uint32_t k = i+1; k < this->bundleInf6->directoryCount; k++)
			{
				if (this->bundleInf6->dirInf[k].offset < this->bundleInf6->dirInf[i].offset)
				{
					AssetBundleDirectoryInfo06 entry = this->bundleInf6->dirInf[k];
					memcpy(&this->bundleInf6->dirInf[i+1], &this->bundleInf6->dirInf[i], (k - i) * sizeof(AssetBundleDirectoryInfo06));
					this->bundleInf6->dirInf[i] = entry;
				}
			}
		}
		std::vector<AssetBundleBlockInfo06> newBlockInfo;
		std::vector<AssetBundleDirectoryInfo06> newDirectory;

		QWORD curFilePos = 0;
		QWORD curCompFilePos = 0;

		AssetBundleHeader06 headerCopy;
		memcpy(&headerCopy, &this->bundleHeader6, sizeof(AssetBundleHeader06));
		strcpy(headerCopy.signature, "UnityFS");
		//strcpy(headerCopy.fileEngineVersion, "5.3.4p10");
		headerCopy.flags = 0x80 | 0x40; //Don't know the block sizes before having written everything, so let's simply append the block info.
		headerCopy.Write(pWriter, curFilePos);
		curFilePos = bundleHeader6.GetFileDataOffset();
		curCompFilePos = headerCopy.GetFileDataOffset();

		QWORD curCompressOffs = 0;
		QWORD curAbsOffs = curCompFilePos; //"cleaned" file pointer to the new file, as if it was uncompressed.
		uint8_t nullBuf[16] = {};
		for (uint32_t i = 0; i < bundleInf6->directoryCount; i++)
		{
			AssetBundleDirectoryInfo06 *pDir = &bundleInf6->dirInf[i];
			curFilePos = bundleHeader6.GetFileDataOffset() + pDir->offset;
			
			AssetBundleBlockInfo06 newBlock;
			AssetBundleDirectoryInfo06 newDir;
			newDir.decompressedSize = pDir->decompressedSize;
			newDir.flags = pDir->flags;
			newDir.name = pDir->name;
			newDir.offset = curAbsOffs - headerCopy.GetFileDataOffset();
			newDirectory.push_back(newDir);
			size_t nameLen = strlen(pDir->name);
			uint32_t curType = COMPRESS_LZ4;
			if (!settings)
			{
				if ((nameLen > 9 && !strnicmp(&pDir->name[nameLen - 9], ".resource", 9))
					|| (nameLen > 10 && !strnicmp(&pDir->name[nameLen - 10], ".resources", 10))
					|| (nameLen > 5 && !strnicmp(&pDir->name[nameLen - 5], ".resS", 5)))
					curType = COMPRESS_NONE;
			}
			else
			{
				curType = settings[i];
				if (curType > (uint32_t)COMPRESS_LZ4) curType = COMPRESS_NONE;
			}
			QWORD entryBytesProcessed = 0;
			QWORD blockLimit = ((uint32_t)std::numeric_limits<int32_t>::max()) & ~15; //0x7FFFFFF0
			while (entryBytesProcessed < pDir->decompressedSize)
			{
				QWORD bytesToProcess = pDir->decompressedSize;
				bytesToProcess = std::min<QWORD>(bytesToProcess, blockLimit);
				QWORD preFileCompPos = curCompFilePos;
				switch (curType)
				{
				case COMPRESS_NONE:
					if (!CopyBlock(&curFilePos, &curCompFilePos, bytesToProcess, pReader, pWriter))
						return false;
					newBlock.flags = 0; //uncompressed
					break;
				case COMPRESS_LZMA:
					if (LZMACompressBlock(&curFilePos, &curCompFilePos, bytesToProcess, 0, pReader, pWriter) != SZ_OK)
						return false;
					newBlock.flags = 1; //LZMA, not streamed
					break;
				case COMPRESS_LZ4:
					if (!LZ4CompressBlock(&curFilePos, &curCompFilePos, bytesToProcess, pReader, pWriter))
						return false;
					newBlock.flags = 2; //LZ4, not streamed
					break;
				}
				newBlock.compressedSize = (uint32_t)(curCompFilePos - preFileCompPos);
				newBlock.decompressedSize = (uint32_t)bytesToProcess;
				newBlockInfo.push_back(newBlock);
				curAbsOffs += newBlock.decompressedSize;
			}
		}
		AssetBundleBlockAndDirectoryList06 listCopy;
		listCopy.checksumLow = 0;
		listCopy.checksumHigh = 0;
		listCopy.blockCount = (uint32_t)newBlockInfo.size();
		listCopy.blockInf = newBlockInfo.data();
		listCopy.directoryCount = (uint32_t)newDirectory.size();
		listCopy.dirInf = newDirectory.data();
		QWORD listBeginPos = curCompFilePos;

		IAssetsWriterToMemory *pListWriter = Create_AssetsWriterToMemory(); QWORD listWriterPos = 0;
		listCopy.Write(pListWriter, listWriterPos);
		pListWriter->SetFreeBuffer(true);
		void *pListRaw = nullptr; size_t listRawSize = 0;
		if (!pListWriter->GetBuffer(pListRaw, listRawSize))
		{
			Free_AssetsWriter(pListWriter);
			return false;
		}
		switch (fileTableSettings)
		{
			default:
			case COMPRESS_NONE:
				if (pWriter->Write(curCompFilePos, listRawSize, pListRaw) < listRawSize)
				{
					Free_AssetsWriter(pListWriter);
					return false;
				}
				curCompFilePos += listRawSize;
				headerCopy.flags = (headerCopy.flags & ~0x3F) | 0;
				break;
			case COMPRESS_LZMA:
				{
					QWORD listReaderPos = 0;
					IAssetsReader *pListReader = Create_AssetsReaderFromMemory(pListRaw, listRawSize, false);
					if (LZMACompressBlock(&listReaderPos, &curCompFilePos, listRawSize, 0, pListReader, pWriter) != SZ_OK)
					{
						Free_AssetsReader(pListReader);
						Free_AssetsWriter(pListWriter);
						return false;
					}
					headerCopy.flags = (headerCopy.flags & ~0x3F) | 1;
				}
				break;
			case COMPRESS_LZ4:
				{
					QWORD listReaderPos = 0;
					IAssetsReader *pListReader = Create_AssetsReaderFromMemory(pListRaw, listRawSize, false);
					if (!LZ4CompressBlock(&listReaderPos, &curCompFilePos, listRawSize, pListReader, pWriter))
					{
						Free_AssetsReader(pListReader);
						Free_AssetsWriter(pListWriter);
						return false;
					}
					headerCopy.flags = (headerCopy.flags & ~0x3F) | 2;
				}
				break;
		}
		Free_AssetsWriter(pListWriter);
		//listCopy.Write(pWriter, curCompFilePos);
		headerCopy.totalFileSize = curCompFilePos;
		headerCopy.decompressedSize = (uint32_t)listRawSize;
		headerCopy.compressedSize = (uint32_t)(curCompFilePos - listBeginPos);

		curCompFilePos = 0;
		headerCopy.Write(pWriter, curCompFilePos);
		return true;
	}
	else
		return false;
	return true;
}
ASSETSTOOLS_API bool AssetBundleFile::Write(IAssetsReader *pReader, 
	IAssetsWriter *pWriter, 
	BundleReplacer **pReplacers, size_t replacerCount, 
	AssetsFileVerifyLogger errorLogger, ClassDatabaseFile *typeMeta)
{
	if (!this->bundleInf6)
	{
		if (errorLogger) errorLogger("ERROR : Invalid bundle file!");
		return false;
	}
	struct BundleReplacerInfo
	{
		BundleReplacer *pReplacer;
		size_t predecessorIndex; //Currently used for fileVersion >= 6 only
		unsigned int bundleEntryIndex;
		bool skipFlag; //Currently used for fileVersion >= 6 only
		BundleReplacerInfo()
			: pReplacer(nullptr), predecessorIndex((size_t)-1),
			bundleEntryIndex((unsigned int)-1), skipFlag(false)
		{}
	};
	std::vector<std::unique_ptr<BundleReplacer, void(*)(BundleReplacer*)>> replacersToDelete;
	std::vector<BundleReplacerInfo> replacers;
	//Internal directory: Removed entries are still counted (no shifting of higher indices).
	//                    -> The first <this->bundleInf6[0].directoryCount> indices represent original directory entries.
	//Output directory: Removed entries are no longer listed.
	//Maps entry names to internal directory indices.
	std::unordered_map<std::string, unsigned int> internalDirectoryIndexByName;
	//Maps internal directory indices to output directory indices.
	std::vector<size_t> internalToOutputDirectoryMap;
	//Maps internal directory indices to the latest replacer index for that entry.
	std::vector<size_t> internalDirectoryLatestReplacerMap;
	size_t numOutputDirectories = 0;
	uint32_t numOriginalDirectories = 0;
	std::vector<std::string> outputDirectoryNames;
	if (bundleHeader3.fileVersion >= 6)
	{
		numOriginalDirectories = this->bundleInf6->directoryCount;
		internalToOutputDirectoryMap.resize(numOriginalDirectories);
		internalDirectoryLatestReplacerMap.resize(numOriginalDirectories);
		numOutputDirectories = numOriginalDirectories;
		outputDirectoryNames.resize(numOriginalDirectories);
		for (uint32_t k = 0; k < numOriginalDirectories; k++)
		{
			internalDirectoryIndexByName[this->bundleInf6->dirInf[k].name] = k;
			outputDirectoryNames[k] = this->bundleInf6->dirInf[k].name;
			internalToOutputDirectoryMap[k] = k;
			internalDirectoryLatestReplacerMap[k] = (size_t)-1;
		}
	}
	else if (bundleHeader3.fileVersion == 3)
	{
		numOriginalDirectories = this->assetsLists3->count;
		internalToOutputDirectoryMap.resize(numOriginalDirectories);
		internalDirectoryLatestReplacerMap.resize(numOriginalDirectories);
		numOutputDirectories = numOriginalDirectories;
		outputDirectoryNames.resize(numOriginalDirectories);
		for (uint32_t k = 0; k < numOriginalDirectories; k++)
		{
			internalDirectoryIndexByName[this->assetsLists3->ppEntries[k]->name] = k;
			outputDirectoryNames[k] = this->assetsLists3->ppEntries[k]->name;
			internalToOutputDirectoryMap[k] = k;
			internalDirectoryLatestReplacerMap[k] = (size_t)-1;
		}
	}
	else
		return false;
		
	//Generate the directory mappings and internal replacer list.
	replacers.resize(replacerCount);
	for (size_t i = 0; i < replacerCount; i++)
	{
		replacers[i].pReplacer = pReplacers[i];
		replacers[i].pReplacer->Uninit();
		unsigned int bundleListIndex = pReplacers[i]->GetBundleListIndex();
		const char *entryName = pReplacers[i]->GetOriginalEntryName();
		if (bundleListIndex == (unsigned int)-1 && entryName != NULL)
		{
			//Find the internal directory index by the entry name.
			auto entryNameIt = internalDirectoryIndexByName.find(entryName);
			if (entryNameIt != internalDirectoryIndexByName.end())
				bundleListIndex = entryNameIt->second;
		}
		if (bundleListIndex == (unsigned int)-1
			&& (entryName != NULL || (entryName = pReplacers[i]->GetEntryName()) != NULL)
			&& pReplacers[i]->GetType() == BundleReplacement_AddOrModify
			&& !pReplacers[i]->RequiresEntryReader())
		{
			//New entry adder.
			//-> Add it to the directory mappings.
			bundleListIndex = (unsigned int)internalToOutputDirectoryMap.size();
			internalDirectoryIndexByName[entryName] = bundleListIndex;
				
			assert(outputDirectoryNames.size() == numOutputDirectories);
			outputDirectoryNames.push_back(entryName);
			internalToOutputDirectoryMap.push_back(numOutputDirectories);
			internalDirectoryLatestReplacerMap.push_back(i);
			++numOutputDirectories;
		}
		else
		{
			//Evaluate the effect on the directory and name mappings.
			switch (pReplacers[i]->GetType())
			{
			case BundleReplacement_Remove:
				if (entryName != NULL && bundleListIndex != (unsigned int)-1)
				{
					assert(numOutputDirectories > 0);
					assert(internalToOutputDirectoryMap[bundleListIndex] != -1);
					if (internalToOutputDirectoryMap[bundleListIndex] != -1)
						outputDirectoryNames.erase(outputDirectoryNames.begin() + internalToOutputDirectoryMap[bundleListIndex]);
					internalToOutputDirectoryMap[bundleListIndex] = (size_t)-1;
					for (size_t k = bundleListIndex + 1; k < internalToOutputDirectoryMap.size(); ++k)
					{
						--internalToOutputDirectoryMap[k];
					}

					--numOutputDirectories;
					internalDirectoryIndexByName.erase(entryName);
					assert(outputDirectoryNames.size() == numOutputDirectories);
				}
				break;
			case BundleReplacement_Rename:
			case BundleReplacement_AddOrModify:
				{
					const char *newEntryName = pReplacers[i]->GetEntryName();
					if (entryName != NULL && bundleListIndex != (unsigned int)-1 && newEntryName != nullptr)
					{
						internalDirectoryIndexByName.erase(entryName);
						internalDirectoryIndexByName[newEntryName] = bundleListIndex;
						assert(internalToOutputDirectoryMap[bundleListIndex] != -1);
						if (internalToOutputDirectoryMap[bundleListIndex] != -1)
							outputDirectoryNames[internalToOutputDirectoryMap[bundleListIndex]] = newEntryName;
					}
				}
				break;
			}
		}
		if (bundleListIndex != (unsigned int)-1)
		{
			//Link the replacer in the by-directoryIndex mapping.
			if (internalDirectoryLatestReplacerMap[bundleListIndex] != (size_t)-1
				&& internalDirectoryLatestReplacerMap[bundleListIndex] != i)
			{
				replacers[i].predecessorIndex = internalDirectoryLatestReplacerMap[bundleListIndex];
			}
			internalDirectoryLatestReplacerMap[bundleListIndex] = i;
		}
		if (!replacers[i].pReplacer->RequiresEntryReader())
		{
			if (pReplacers[i]->GetType() != BundleReplacement_Rename)
			{
				//Any previous replacer result is not required.
				//-> Mark the predecessors as skippable.
				size_t k = replacers[i].predecessorIndex;
				while (k != (size_t)-1)
				{
					replacers[k].skipFlag = true;
					k = replacers[k].predecessorIndex;
				}
				replacers[i].predecessorIndex = (size_t) -1;
			}
		}
		else if (bundleListIndex >= numOriginalDirectories && replacers[i].predecessorIndex == (size_t)-1)
		{
			//The replacer cannot be applied, as it is based on previous entry data that doesn't exist.
			//Shouldn't happen, unless an invalid replacers file is read in.
			assert(false);
			replacers[i].skipFlag = true; //Invalid replacer.
			if (bundleListIndex != (unsigned int)-1)
			{
				//Unlink the replacer and remove its generated directory entry.
				assert(internalToOutputDirectoryMap[bundleListIndex] != -1);
				if (internalToOutputDirectoryMap[bundleListIndex] != -1)
					outputDirectoryNames.erase(outputDirectoryNames.begin() + internalToOutputDirectoryMap[bundleListIndex]);
				internalToOutputDirectoryMap[bundleListIndex] = (size_t)-1;
				for (size_t k = bundleListIndex + 1; k < internalToOutputDirectoryMap.size(); ++k)
				{
					--internalToOutputDirectoryMap[k];
				}
				--numOutputDirectories;
				internalDirectoryIndexByName.erase(entryName);
				assert(outputDirectoryNames.size() == numOutputDirectories);
			}
		}
		if (bundleListIndex == (unsigned int)-1)
		{
			//Shouldn't happen, unless an invalid replacers file is read in.
			assert(false);
			replacers[i].skipFlag = true; //Invalid replacer.
		}
		replacers[i].bundleEntryIndex = bundleListIndex;
	}
	//Prepare the replacers for each output directory entry.
	std::vector<std::shared_ptr<IAssetsReader>> tempReaderReferences;
	for (size_t i = 0; i < internalToOutputDirectoryMap.size(); i++)
	{
		if (internalToOutputDirectoryMap[i] == (size_t)-1)
			continue; //Entry was removed.
		std::vector<size_t> replacerReverseOrder;
		size_t k = internalDirectoryLatestReplacerMap[i];
		while (k != (size_t)-1 && !replacers[k].skipFlag)
		{
			if (replacers[k].pReplacer->GetType() != BundleReplacement_Rename) //Skip renamers here.
				replacerReverseOrder.push_back(k);
			k = replacers[k].predecessorIndex;
		}
		bool error = false;
		if (replacerReverseOrder.empty())
		{
			size_t outputIndex = internalToOutputDirectoryMap[i];
			if (i >= numOriginalDirectories)
				error = true;
			else
			{
				assert(outputDirectoryNames.size() > outputIndex);
				BundleReplacerInfo newReplacer;
				QWORD decompressedSize = 0;
				QWORD absolutePos = 0;
				if (this->bundleHeader3.fileVersion >= 6)
				{
					decompressedSize = this->bundleInf6->dirInf[i].decompressedSize;
					absolutePos = this->bundleInf6->dirInf[i].GetAbsolutePos(this);
				}
				else if (this->bundleHeader3.fileVersion == 3)
				{
					decompressedSize = this->assetsLists3->ppEntries[i]->length;
					absolutePos = this->assetsLists3->ppEntries[i]->GetAbsolutePos(this);
				}
				bool hasSerializedData = this->IsAssetsFile(pReader, i);
				newReplacer.pReplacer = MakeBundleEntryModifier(this->GetEntryName(i), outputDirectoryNames[outputIndex].c_str(), 
					hasSerializedData, pReader, NULL, decompressedSize, absolutePos, 16384, (unsigned int)outputIndex);
				replacersToDelete.push_back(std::unique_ptr<BundleReplacer, void(*)(BundleReplacer*)>(newReplacer.pReplacer, FreeBundleReplacer));
				newReplacer.bundleEntryIndex = (unsigned int)i;
				replacers.push_back(newReplacer);
				internalDirectoryLatestReplacerMap[i] = replacers.size() - 1;
				replacerReverseOrder.push_back(replacers.size() - 1);
			}
		}
		std::shared_ptr<IAssetsReader> pInitReader_refholder;
		IAssetsReader *pInitReader = nullptr;
		QWORD initSize = 0;
		QWORD initPos = 0;
		if (i < numOriginalDirectories && replacers[replacerReverseOrder.back()].pReplacer->RequiresEntryReader())
		{
			pInitReader = pReader;
			if (this->bundleHeader3.fileVersion >= 6)
			{
				initSize = this->bundleInf6->dirInf[i].decompressedSize;
				initPos = this->bundleInf6->dirInf[i].GetAbsolutePos(this);
			}
			else if (this->bundleHeader3.fileVersion == 3)
			{
				initSize = this->assetsLists3->ppEntries[i]->length;
				initPos = this->assetsLists3->ppEntries[i]->GetAbsolutePos(this);
			}
		}
		//Iterate through the replacers (first to last) to initialize them.
		for (auto it = replacerReverseOrder.rbegin(); !error && it != replacerReverseOrder.rend(); ++it)
		{
			size_t curReplacerIdx = *it;
			BundleReplacerInfo &curReplacer = replacers[curReplacerIdx];
			if (!curReplacer.pReplacer->Init(this, pInitReader, initPos, initSize, typeMeta))
			{
				error = true;
				break;
			}
			auto next_it = it; ++next_it;
			if (next_it == replacerReverseOrder.rend())
			{
				break;
			}
			struct { void operator()(BundleReplacer*){} } dummyDeleter;
			auto pNextReader = MakeReaderFromBundleEntryModifier(std::shared_ptr<BundleReplacer>(curReplacer.pReplacer, dummyDeleter));
			if (pNextReader != nullptr)
			{
				//If this is a simple file/memory-based replacer, we can use its internal resource to generate a reader.
				curReplacer.pReplacer->Uninit();
				pInitReader_refholder = pNextReader;
				pInitReader = pNextReader.get();
				if (!pNextReader->Seek(AssetsSeek_End, 0) || !pNextReader->Tell(initSize) || !pNextReader->Seek(AssetsSeek_Begin, 0))
				{
					error = true;
					break;
				}
				initPos = 0;
				continue;
			}
			//The replacer appears to be a more complex one, i.e. FromBundle or FromAssets.
			//Write the replaced file to a memory buffer, and proceed using that buffer as input for the next replacer.
			IAssetsWriterToMemory *pTempWriter = Create_AssetsWriterToMemory();
			initSize = curReplacer.pReplacer->Write(0, pTempWriter);
			curReplacer.pReplacer->Uninit();
			if (initSize == 0)
			{
				//Assume this is an error, as the output should always have a header.
				Free_AssetsWriter(pTempWriter);
				error = true;
				break;
			}
			void *memBuffer = nullptr; size_t memBufferSize = 0;
			if (!pTempWriter->GetBuffer(memBuffer, memBufferSize) || memBufferSize < initSize || !pTempWriter->SetFreeBuffer(false))
			{
				//This really shouldn't happen.
				assert(false);
				Free_AssetsWriter(pTempWriter);
				error = true;
				break;
			}
			Free_AssetsWriter(pTempWriter);
			initPos = 0;
			pNextReader.reset(Create_AssetsReaderFromMemory(memBuffer, memBufferSize, false, Free_AssetsWriterToMemory_DynBuf), Free_AssetsReader);
			pInitReader_refholder = pNextReader;
			pInitReader = pNextReader.get();
		}
		if (error)
		{
			//Remove the current directory entry.
			size_t outputIndex = internalToOutputDirectoryMap[i];
			std::string entryName = std::move(outputDirectoryNames[outputIndex]);
			outputDirectoryNames.erase(outputDirectoryNames.begin() + outputIndex);
			internalToOutputDirectoryMap[i] = (size_t)-1;
			for (size_t k = i + 1; k < internalToOutputDirectoryMap.size(); ++k)
			{
				--internalToOutputDirectoryMap[k];
			}
			--numOutputDirectories;
			internalDirectoryIndexByName.erase(entryName);
			assert(outputDirectoryNames.size() == numOutputDirectories);

			std::string warningMessage = std::string("Error : Unable to prepare the entry modification for entry ") + entryName + ".";
			if (errorLogger) errorLogger(warningMessage.c_str());
			continue;
		}
		// Unlink any Renamers in front of the last true modifier.
		internalDirectoryLatestReplacerMap[i] = replacerReverseOrder.front();
		if (pInitReader_refholder != nullptr)
			tempReaderReferences.push_back(pInitReader_refholder);
	}
	struct _Lambda_FreeReplacers {
		std::vector<BundleReplacerInfo> &replacers;
		_Lambda_FreeReplacers(std::vector<BundleReplacerInfo> &replacers) : replacers(replacers) {}
		void operator()()
		{
			for (size_t i = 0; i < replacers.size(); ++i)
				if (replacers[i].pReplacer != nullptr) replacers[i].pReplacer->Uninit();
		}
	} lambda_uninitReplacers(replacers);
	assert(outputDirectoryNames.size() == numOutputDirectories);
	assert(numOutputDirectories <= UINT_MAX);
	if (numOutputDirectories > UINT_MAX)
	{
		lambda_uninitReplacers();
		return false;
	}
	if (bundleHeader3.fileVersion >= 6)
	{
		//std::vector<BundleReplacerInfo> orderedReplacers;
		std::vector<AssetBundleDirectoryInfo06> directories(numOutputDirectories);
		std::vector<AssetBundleBlockInfo06> blocks;
		QWORD curFilePos = 0;
		AssetBundleHeader06 header = this->bundleHeader6;
		if (header.flags & 0x100)
			strcpy(header.signature, "UnityWeb");
		header.flags &= ~0x3F; //No directory/block list compression.
		header.flags |= 0x40; //Has directory info.
		header.Write(pWriter, curFilePos);
		//Create dummy block info (we don't have exact sizes but have to assume that <= or > 4 GiB can be differentiated).
		//Also assign directory names.
		for (size_t i = 0; i < internalToOutputDirectoryMap.size(); i++)
		{
			size_t outputDirectoryIdx = internalToOutputDirectoryMap[i];
			if (outputDirectoryIdx == (size_t)-1)
				continue; //Entry was removed.
			assert(outputDirectoryIdx < numOutputDirectories);
			if (outputDirectoryIdx >= numOutputDirectories)
			{
				lambda_uninitReplacers();
				return false;  //Shouldn't happen
			}
			directories[outputDirectoryIdx].name = outputDirectoryNames[outputDirectoryIdx].c_str();
			size_t replacerIdx = internalDirectoryLatestReplacerMap[i];
			assert(replacerIdx != (size_t)-1 && !replacers[replacerIdx].skipFlag);
			if (replacerIdx == (size_t)-1 || replacers[replacerIdx].skipFlag)
			{
				lambda_uninitReplacers();
				return false;  //Shouldn't happen
			}
			BundleReplacerInfo &replacer = replacers[replacerIdx];
			if (replacer.pReplacer->GetType() == BundleReplacement_AddOrModify)
			{
				QWORD estimatedSize = replacer.pReplacer->GetSize();
				while (estimatedSize >= 0xFFFFFFFFULL)
				{
					AssetBundleBlockInfo06 dummyBlock = {};
					blocks.push_back(dummyBlock);
					estimatedSize -= 0xFFFFFFFFULL;
				}
				if (estimatedSize)
				{
					AssetBundleBlockInfo06 dummyBlock = {};
					blocks.push_back(dummyBlock);
				}
			}
		}
		QWORD blockAndDirListPos = curFilePos;
		AssetBundleBlockAndDirectoryList06 blockAndDirList = {};
		blockAndDirList.blockCount = (uint32_t)blocks.size();
		blockAndDirList.blockInf = blocks.data();
		blockAndDirList.directoryCount = (uint32_t)numOutputDirectories;
		blockAndDirList.dirInf = directories.data();
		blockAndDirList.Write(pWriter, curFilePos);
		QWORD bundleDataPos = curFilePos;
		//Fix the sizes
		header.compressedSize = header.decompressedSize = (uint32_t)(curFilePos - blockAndDirListPos);
		header.flags &= ~(0x80);
		size_t blockIndex = 0;
		for (size_t i = 0; i < internalToOutputDirectoryMap.size(); i++)
		{
			size_t outputDirectoryIdx = internalToOutputDirectoryMap[i];
			if (outputDirectoryIdx == (size_t)-1)
				continue; //Entry was removed.
			size_t replacerIdx = internalDirectoryLatestReplacerMap[i];
			BundleReplacerInfo &replacer = replacers[replacerIdx];
			if (replacer.pReplacer->GetType() == BundleReplacement_AddOrModify)
			{
				//fix the sizes in the directory info
				QWORD beginFilePos = curFilePos;
				curFilePos = replacer.pReplacer->Write(curFilePos, pWriter);
				if (replacer.pReplacer->HasSerializedData())
					directories[outputDirectoryIdx].flags |= 4;
				replacer.pReplacer->Uninit();
				directories[outputDirectoryIdx].decompressedSize = curFilePos - beginFilePos;
				directories[outputDirectoryIdx].offset = beginFilePos - header.GetFileDataOffset();
				//fix the sizes in the block info
				QWORD remainingSize = curFilePos - beginFilePos;
				while (remainingSize >= 0xFFFFFFFFULL)
				{
					if (blocks.size() <= blockIndex)
						break;
					blocks[blockIndex].compressedSize = blocks[blockIndex].decompressedSize = 0xFFFFFFFFULL;
					blocks[blockIndex].flags = 0x40; //TODO: Check if the 'streamed' flag was set before, because it isn't always.
					blockIndex++;
					remainingSize -= 0xFFFFFFFFULL;
				}
				if (remainingSize && (blocks.size() > blockIndex))
				{
					blocks[blockIndex].compressedSize = blocks[blockIndex].decompressedSize = (uint32_t)remainingSize;
					blocks[blockIndex].flags = 0x40;
					blockIndex++;
				}
			}
		}
		header.totalFileSize = curFilePos;
		curFilePos = 0;
		header.Write(pWriter, curFilePos);
		blockAndDirList.blockInf = blocks.data();
		blockAndDirList.dirInf = directories.data();
		curFilePos = blockAndDirListPos;
		blockAndDirList.Write(pWriter, curFilePos);
		pWriter->SetPosition(header.totalFileSize);
		return true;
	}
	else if (bundleHeader3.fileVersion == 3)
	{
		std::vector<AssetBundleEntry*> directories;
		std::vector<AssetBundleOffsetPair> blocks;
		//Create dummy block info
		//-> The file table gets its own block
		{
			AssetBundleOffsetPair dummyBlock = {};
			blocks.push_back(dummyBlock);
		}
		bool error = false;
		//Create dummy block info for replacers and also assign directory names.
		for (size_t i = 0; i < internalToOutputDirectoryMap.size(); i++)
		{
			size_t outputDirectoryIdx = internalToOutputDirectoryMap[i];
			if (outputDirectoryIdx == (size_t)-1)
				continue; //Entry was removed.
			assert(outputDirectoryIdx < numOutputDirectories);
			if (outputDirectoryIdx >= numOutputDirectories)
			{
				error = true;  //Shouldn't happen
				break;
			}
			size_t replacerIdx = internalDirectoryLatestReplacerMap[i];
			assert(replacerIdx != (size_t)-1 && !replacers[replacerIdx].skipFlag);
			if (replacerIdx == (size_t)-1 || replacers[replacerIdx].skipFlag)
			{
				error = true;  //Shouldn't happen
				break;
			}
			assert(directories.size() == outputDirectoryIdx);

			AssetBundleEntry *newDirectory = (AssetBundleEntry*)new uint8_t[8 + sizeof(char) * (outputDirectoryNames[outputDirectoryIdx].size() + 1)];
			newDirectory->length = newDirectory->offset = 0;
			memcpy(newDirectory->name, outputDirectoryNames[outputDirectoryIdx].data(), sizeof(char) * (outputDirectoryNames[outputDirectoryIdx].size() + 1));
			directories.push_back(newDirectory);
			BundleReplacerInfo &replacer = replacers[replacerIdx];
			if (replacer.pReplacer->GetType() == BundleReplacement_AddOrModify)
			{
				QWORD estimatedSize = replacer.pReplacer->GetSize();
				while (estimatedSize >= 0xFFFFFFFFULL)
				{
					AssetBundleOffsetPair dummyBlock = {};
					blocks.push_back(dummyBlock);
					estimatedSize -= 0xFFFFFFFFULL;
				}
				if (estimatedSize)
				{
					AssetBundleOffsetPair dummyBlock = {};
					blocks.push_back(dummyBlock);
				}
			}
		}
		if (error)
		{
			for (size_t i = 0; i < directories.size(); i++)
			{
				delete[] ((uint8_t*)directories[i]);
			}
			lambda_uninitReplacers();
			return false;
		}
		AssetBundleHeader03 header = this->bundleHeader3;
		QWORD curFilePos = 0;
		header.blockCount = (uint32_t)blocks.size();
		header.pBlockList = blocks.data();
		header.Write(pWriter, curFilePos);

		uint32_t alignBytes = (uint32_t)(((curFilePos + 3) & (~3)) - curFilePos);
		uint32_t dwNull = 0;
		pWriter->Write(curFilePos, alignBytes, &dwNull);
		curFilePos += alignBytes;

		QWORD assetsListPos = curFilePos;
		AssetsList assetsList = {};
		assetsList.allocatedCount = assetsList.count = (uint32_t)directories.size();
		assetsList.pos = (uint32_t)curFilePos;
		assetsList.ppEntries = directories.data();
		assetsList.Write(pWriter, curFilePos);
		//fix the sizes
		header.bundleDataOffs = (uint32_t)assetsListPos;
		header.unknown2 = (uint32_t)(curFilePos - assetsListPos); //not sure if this is right
		blocks[0].compressed = blocks[0].uncompressed = header.unknown2;
		size_t blockIndex = 1;
		for (size_t i = 0; i < internalToOutputDirectoryMap.size(); i++)
		{
			size_t outputDirectoryIdx = internalToOutputDirectoryMap[i];
			if (outputDirectoryIdx == (size_t)-1)
				continue; //Entry was removed.
			size_t replacerIdx = internalDirectoryLatestReplacerMap[i];
			BundleReplacerInfo &replacer = replacers[replacerIdx];
			if (replacer.pReplacer->GetType() == BundleReplacement_AddOrModify)
			{
				//align to 4 bytes
				uint32_t alignBytes = (uint32_t)(((curFilePos + 3) & (~3)) - curFilePos);
				uint32_t dwNull = 0;
				pWriter->Write(curFilePos, alignBytes, &dwNull);
				curFilePos += alignBytes;

				QWORD beginFilePos = curFilePos;
				curFilePos = replacer.pReplacer->Write(curFilePos, pWriter);
				replacer.pReplacer->Uninit();
				//fix the sizes in the directory info
				directories[replacer.bundleEntryIndex]->length = (uint32_t)(curFilePos - beginFilePos);
				directories[replacer.bundleEntryIndex]->offset = (uint32_t)(beginFilePos - header.bundleDataOffs);
				//fix the sizes in the block info
				QWORD remainingSize = curFilePos - beginFilePos;
				while (remainingSize >= 0xFFFFFFFFULL)
				{
					if (blocks.size() <= blockIndex)
						break;
					blocks[blockIndex].compressed = blocks[blockIndex].uncompressed = 0xFFFFFFFFULL;
					blockIndex++;
					remainingSize -= 0xFFFFFFFFULL;
				}
				if (remainingSize && (blocks.size() > blockIndex))
				{
					blocks[blockIndex].compressed = blocks[blockIndex].uncompressed = (uint32_t)((remainingSize + 3) & (~3));
					blockIndex++;
				}
			}
		}
		header.minimumStreamedBytes = (uint32_t)curFilePos;
		header.fileSize2 = (uint32_t)curFilePos;
		curFilePos = 0;
		header.pBlockList = blocks.data();
		header.Write(pWriter, curFilePos);
		curFilePos = assetsListPos;
		assetsList.ppEntries = directories.data();
		assetsList.Write(pWriter, curFilePos);
		pWriter->SetPosition(header.fileSize2);
		for (size_t i = 0; i < directories.size(); i++)
		{
			delete[] ((uint8_t*)directories[i]);
		}
		return true;
	}
	else //unknown bundle file format
		return false;
}
ASSETSTOOLS_API bool AssetBundleFile::Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger, bool allowCompressed, uint32_t maxFileTableLen)
{
	Close();
	//this->reader = reader;
	if (!bundleHeader6.ReadInitial(pReader, errorLogger))
		return false;
	IAssetsReader *pTempReader = nullptr;
	if (bundleHeader6.fileVersion >= 6)
	{
		memset(&bundleHeader6, 0, sizeof(AssetBundleHeader06));
		if (!bundleHeader6.Read(pReader, errorLogger))
			return false;
		if (!strcmp(bundleHeader6.signature, "UnityArchive"))
		{
			//bundleHeader6.flags &= 0xFFFFFFC0;
			bundleHeader6.flags |= 0x40;
		}
		else if (!strcmp(bundleHeader6.signature, "UnityWeb"))
		{
			strcpy(bundleHeader6.signature, "UnityFS");
			//bundleHeader6.flags &= 0xFFFFFF80;
			bundleHeader6.flags |= 0x100;
			//bundleHeader6.flags &= ~0x100;
		}
		else if (!strcmp(bundleHeader6.signature, "UnityRaw"))
		{
			//bundleHeader6.flags &= 0xFFFFFFC0;
			bundleHeader6.flags |= 0x40;
		}

		void *fileTableBuf = nullptr;
		QWORD fileTablePos = bundleHeader6.GetBundleInfoOffset();
		IAssetsReader *pFileTableReader = pReader;
		if ((bundleHeader6.flags & 0x3F) != 0)
		{
			uint8_t compressionType = (bundleHeader6.flags & 0x3F);
			if (compressionType < 4)
			{
				fileTableBuf = malloc(bundleHeader6.decompressedSize);
				if (!fileTableBuf)
					goto __goto_outofmemory;
				IAssetsWriter *pTempWriter = Create_AssetsWriterToMemory(fileTableBuf, bundleHeader6.decompressedSize);
				if (!pTempWriter)
				{
					free(fileTableBuf);
					goto __goto_outofmemory;
				}
				pTempReader = Create_AssetsReaderFromMemory(fileTableBuf, bundleHeader6.decompressedSize, false);
				if (!pTempReader)
				{
					Free_AssetsWriter(pTempWriter);
					free(fileTableBuf);
					goto __goto_outofmemory;
				}
				bool decompressSuccess = false;
				QWORD curInPos = fileTablePos;
				QWORD curOutPos = 0;
				switch (compressionType)
				{
				case 1: //LZMA
					if (LZMADecompressBlock(&curInPos, &curOutPos, bundleHeader6.compressedSize, pReader, pTempWriter) == SZ_OK)
						decompressSuccess = true;
					break;
				case 2: case 3: //LZ4
					if (LZ4DecompressBlock(&curInPos, &curOutPos, bundleHeader6.compressedSize, pReader, pTempWriter))
						decompressSuccess = true;
					break;
				}
				Free_AssetsWriter(pTempWriter);
				if (!decompressSuccess || curOutPos != bundleHeader6.decompressedSize)
				{
					Free_AssetsReader(pTempReader);
					free(fileTableBuf);
					if (errorLogger) errorLogger("AssetBundleFile.Read : Failed to decompress the directory!");
					return false;
				}
				pFileTableReader = pTempReader;
				fileTablePos = 0;
			}
		}
		if (fileTableBuf || (bundleHeader6.flags & 0x3F) == 0)
		{
			bundleInf6 = (AssetBundleBlockAndDirectoryList06*)malloc(sizeof(AssetBundleBlockAndDirectoryList06));
			if (!bundleInf6)
				goto __goto_outofmemory;
			memset(bundleInf6, 0, sizeof(AssetBundleBlockAndDirectoryList06));
			if (!bundleInf6->Read(fileTablePos, pFileTableReader))
				goto __goto_readerror;
			if (pTempReader)
			{
				Free_AssetsReader(pTempReader);
				pTempReader = nullptr;
			}
			if (fileTableBuf)
				free(fileTableBuf);
		}
		else if (!allowCompressed)
		{
			Close();
			return false;
		}
	}
	else if (bundleHeader6.fileVersion == 3)
	{
		memset(&bundleHeader3, 0, sizeof(AssetBundleHeader03));
		if (!bundleHeader3.Read(pReader, errorLogger))
			return false;
		if (!strcmp(bundleHeader3.signature, "UnityRaw")) //compressed bundles only have an uncompressed header
		{
			QWORD curFilePos = bundleHeader3.bundleDataOffs;
			assetsLists3 = (AssetsList*)malloc(sizeof(AssetsList));
			if (!assetsLists3)
				goto __goto_outofmemory;
			ZeroMemory(assetsLists3, sizeof(AssetsList));

			QWORD tmpFilePos = curFilePos;
			if (!assetsLists3->Read(pReader, tmpFilePos, errorLogger))
				goto __goto_readerror;
			for (uint32_t i = 0; i < bundleHeader3.blockCount; i++)
				curFilePos += (bundleHeader3.pBlockList[i].uncompressed + 3) & (~3);
		}
		else if (!allowCompressed)
		{
			Close();
			return false;
		}
	}
	else
	{
		if (errorLogger) errorLogger("AssetBundleFile.Read : Unknown file version!");
		return false;
	}
	return true;

	__goto_readerror:
	if (errorLogger) errorLogger("AssetBundleFile.Read : A file read error occured!");
	goto __goto_errfreebuffers;
	__goto_outofmemory:
	if (errorLogger) errorLogger("AssetBundleFile.Read : Out of memory!");
	goto __goto_errfreebuffers;
	__goto_errfreebuffers:
	if (bundleHeader6.fileVersion == 3)
	{
		if (assetsLists3 != NULL)
		{
			assetsLists3->Free();
			free(assetsLists3);
		}
		assetsLists3 = NULL;
	}
	else if (bundleHeader6.fileVersion >= 6)
	{
		if (bundleInf6 != NULL)
		{
			bundleInf6->Free();
			free(bundleInf6);
		}
		bundleInf6 = NULL;
	}
	if (pTempReader != nullptr)
	{
		Free_AssetsReader(pTempReader);
	}
	return false;
}
ASSETSTOOLS_API bool AssetBundleFile::IsCompressed()
{
	if (assetsLists3 == NULL || bundleInf6 == NULL) return true;
	if (bundleHeader6.fileVersion >= 6)
	{
		for (uint32_t i = 0; i < bundleInf6->blockCount; i++)
		{
			if ((bundleInf6->blockInf[i].flags & 0x3F) != 0)
				return true;
		}
	}
	//Version 3 bundles are compressed in a single block.
	return false;
}
ASSETSTOOLS_API bool AssetBundleFile::IsAssetsFile(IAssetsReader *pReader, AssetBundleDirectoryInfo06 *pEntry)
{
	if (pEntry->name)
	{
		size_t entryNameLen = strlen(pEntry->name);
		if (entryNameLen >= 5 && !stricmp(&pEntry->name[entryNameLen - 5], ".resS"))
			return false;
		if (entryNameLen >= 9 && !stricmp(&pEntry->name[entryNameLen - 9], ".resource"))
			return false;
	}
	QWORD pos = pEntry->GetAbsolutePos(this);
	char buf[8];
	pReader->Read(pos, 8, buf);
	//mdb, sound bank, dll
	if (((*(QWORD*)&buf[0]) == 0x45E82623FD7FA614ULL) || !strncmp(buf, "FSB5", 4) || !strncmp(buf, "MZ", 2))
		return false;
	IAssetsReaderFromReaderRange *pReaderRange = Create_AssetsReaderFromReaderRange(pReader, pos, pEntry->decompressedSize);
	if (pReaderRange)
	{
		AssetsFile testAssetsFile(pReaderRange);
		bool ret = testAssetsFile.VerifyAssetsFile();
		Free_AssetsReader(pReaderRange);
		return ret;
	}
	return true;
}
ASSETSTOOLS_API bool AssetBundleFile::IsAssetsFile(IAssetsReader *pReader, AssetBundleEntry *pEntry)
{
	unsigned int pos = pEntry->GetAbsolutePos(this);
	char buf[8];
	pReader->Read(pos, 8, buf);
	if (((*(QWORD*)&buf[0]) == 0x45E82623FD7FA614ULL) || !strncmp(buf, "FSB5", 4) || !strncmp(buf, "MZ", 2))
		return false;
	return true;
}
ASSETSTOOLS_API bool AssetBundleFile::IsAssetsFile(IAssetsReader *pReader, size_t entryIdx)
{
	if (bundleHeader6.fileVersion >= 6)
		return this->bundleInf6 && this->bundleInf6->directoryCount > entryIdx && IsAssetsFile(pReader, &this->bundleInf6->dirInf[entryIdx]);
	else if (bundleHeader6.fileVersion == 3)
		return this->assetsLists3 && this->assetsLists3->count > entryIdx && IsAssetsFile(pReader, this->assetsLists3->ppEntries[entryIdx]);
	return false;
}
ASSETSTOOLS_API IAssetsReader *AssetBundleFile::MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleDirectoryInfo06 *pEntry)
{
	QWORD absoluteEntryPos = pEntry->GetAbsolutePos(this);
	QWORD entryLength = pEntry->decompressedSize;
	return Create_AssetsReaderFromReaderRange(pReader, absoluteEntryPos, entryLength);
}
ASSETSTOOLS_API IAssetsReader *AssetBundleFile::MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleEntry *pEntry)
{
	for (uint32_t i = 0; i < this->assetsLists3->count; i++)
	{
		if (this->assetsLists3->ppEntries[i] == pEntry)
		{
			goto __goto_break;
		}
	}
	return NULL;
__goto_break:
	QWORD absoluteEntryPos = pEntry->GetAbsolutePos(this);
	QWORD entryLength = pEntry->length;
	return Create_AssetsReaderFromReaderRange(pReader, absoluteEntryPos, entryLength);
}
ASSETSTOOLS_API void FreeAssetBundle_FileReader(IAssetsReader *pReader)
{
	Free_AssetsReader(pReader);
}