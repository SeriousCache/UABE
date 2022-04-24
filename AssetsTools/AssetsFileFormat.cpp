#include "stdafx.h"
#include "../AssetsTools/AssetsFileFormat.h"
#include "../AssetsTools/AssetsFileReader.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "InternalAssetsReplacer.h"
#include <assert.h>
#include <list>
#include <unordered_map>
#include <algorithm>

ASSETSTOOLS_API uint32_t AssetFileInfo::GetSize(uint32_t version)
{
	if (version >= 0x16)
		return 24;
	else if (version >= 0x11)
		return 20;
	else if (version >= 0x10)
		return 23;
	else if (version >= 0x0F)
		return 25;
	else if (version == 0x0E)
		return 24;
	else if (version >= 0x0B)
		return 20;
	else //if (version >= 0x07)
		return 20;
}

ASSETSTOOLS_API QWORD AssetFileInfo::Read(uint32_t version, QWORD pos, IAssetsReader *pReader, bool bigEndian)
{
	uint8_t fileInfoData[AssetFileInfo_MaxSize];
	if (version >= 0x0E)
		pos = (pos + 3) & ~3;
	pReader->Read(pos, GetSize(version), fileInfoData);  pos += GetSize(version);
	this->index = 0;
	int fileInfoDataOffs = 0;
	memcpy(&this->index, &fileInfoData[fileInfoDataOffs], (version >= 0x0E) ? 8 : 4); fileInfoDataOffs += ((version >= 0x0E) ? 8 : 4);
	if (bigEndian)
	{
		if (version >= 0x0E)
			SwapEndians_(this->index);
		else
			SwapEndians_(*(uint32_t*)&this->index);
	}
	this->offs_curFile = 0;
	memcpy(&this->offs_curFile, &fileInfoData[fileInfoDataOffs], (version >= 0x16) ? 8 : 4); fileInfoDataOffs += (version >= 0x16) ? 8 : 4; //always 4-byte aligned
	if (bigEndian)
	{
		if (version >= 0x16)
			SwapEndians_(this->offs_curFile);
		else
			SwapEndians_(*(uint32_t*)&this->offs_curFile);
	}
	memcpy(&this->curFileSize, &fileInfoData[fileInfoDataOffs], 4); fileInfoDataOffs += 4;
	if (bigEndian)
		SwapEndians_(this->curFileSize);
	memcpy(&this->curFileTypeOrIndex, &fileInfoData[fileInfoDataOffs], 4); fileInfoDataOffs += 4;
	if (bigEndian)
		SwapEndians_(this->curFileTypeOrIndex);
	if (version < 0x10)
	{
		memcpy(&this->inheritedUnityClass, &fileInfoData[fileInfoDataOffs], 2); fileInfoDataOffs += 2;
		if (bigEndian)
			SwapEndians_(this->inheritedUnityClass);
	}
	else
		this->inheritedUnityClass = 0;
	if (version < 0x0B)
		fileInfoDataOffs += 2;
	if ((version >= 0x0B) && (version <= 0x10))
	{
		memcpy(&this->scriptIndex, &fileInfoData[fileInfoDataOffs], 2); fileInfoDataOffs += 2;
		if (bigEndian)
			SwapEndians_(this->scriptIndex);
	}
	else
	{
		this->scriptIndex = 0xFFFF;
	}
	if ((version >= 0x0F) && (version <= 0x10))
	{
		memcpy(&this->unknown1, &fileInfoData[fileInfoDataOffs], 1); fileInfoDataOffs += 1;
	}
	else
		this->unknown1 = 0;
	return pos;
}
ASSETSTOOLS_API QWORD AssetFileInfo::Write(uint32_t version, QWORD pos, IAssetsWriter *pWriter, bool bigEndian)
{
	uint8_t fileInfoData[AssetFileInfo_MaxSize];
	if (version >= 0x0E)
	{
		uint32_t dwTmp = 0;
		pWriter->Write(pos, 3-((pos-1)&3), &dwTmp);
		pos = (pos + 3) & ~3;
	}
	int fileInfoDataOffs = 0;
	QWORD tempQW; uint32_t tempDW; uint16_t tempW;

	tempQW = this->index;
	if (bigEndian)
	{
		if (version < 0x0E) tempQW <<= 32;
		SwapEndians_(tempQW);
	}
	memcpy(&fileInfoData[fileInfoDataOffs], &tempQW, (version >= 0x0E) ? 8 : 4); fileInfoDataOffs += ((version >= 0x0E) ? 8 : 4);

	tempQW = this->offs_curFile;
	if (bigEndian)
	{
		if (version < 0x16) tempQW <<= 32;
		SwapEndians_(tempQW);
	}
	memcpy(&fileInfoData[fileInfoDataOffs], &tempQW, (version >= 0x16) ? 8 : 4); fileInfoDataOffs += ((version >= 0x16) ? 8 : 4);
	
	tempDW = this->curFileSize;
	if (bigEndian)
		SwapEndians_(tempDW);
	memcpy(&fileInfoData[fileInfoDataOffs], &tempDW, 4); fileInfoDataOffs += 4;
	
	tempDW = this->curFileTypeOrIndex;
	if (bigEndian)
		SwapEndians_(tempDW);
	memcpy(&fileInfoData[fileInfoDataOffs], &tempDW, 4); fileInfoDataOffs += 4;
	if (version < 0x10)
	{
		tempW = this->inheritedUnityClass;
		if (bigEndian)
			SwapEndians_(tempW);
		memcpy(&fileInfoData[fileInfoDataOffs], &tempW, 2); fileInfoDataOffs += 2;
	}
	if (version < 0x11)
	{
		if (version >= 0x0B)
		{
			tempW = this->scriptIndex;
			if (bigEndian)
				SwapEndians_(tempW);
			memcpy(&fileInfoData[fileInfoDataOffs], &tempW, 2);
		}
		else
		{
			uint16_t wordTmp = 0; memcpy(&fileInfoData[fileInfoDataOffs], &wordTmp, 2);
		}
		fileInfoDataOffs += 2;
		if (version >= 0x0F)
		{
			memcpy(&fileInfoData[fileInfoDataOffs], &this->unknown1, 1); fileInfoDataOffs += 1;
		}
	}
	pWriter->Write(pos, fileInfoDataOffs, fileInfoData);  pos += fileInfoDataOffs;
	return pos;
}
ASSETSTOOLS_API unsigned int AssetFileList::GetSizeBytes(uint32_t version)
{
	if (version < 0x0F || version > 0x10)
		return this->sizeFiles * AssetFileInfo::GetSize(version);
	else
	{
		if (this->sizeFiles == 0)
			return 0;
		unsigned int ret = 0;
		unsigned int sizePerFile = AssetFileInfo::GetSize(version);
		ret = ((sizePerFile+3)&(~3)) * (this->sizeFiles-1) + sizePerFile;
		return ret;
		/*for (unsigned int i = 0; i < this->sizeFiles; i++)
		{
			ret = (ret+3)&(~3);
			ret += sizePerFile;
		}*/
	}
}
ASSETSTOOLS_API QWORD AssetFileList::Read(uint32_t version, QWORD pos, IAssetsReader *pReader, bool bigEndian)
{
	for (unsigned int i = 0; i < this->sizeFiles; i++)
	{
		pos = fileInfs[i].Read(version, pos, pReader, bigEndian);
	}
	return pos;
}
ASSETSTOOLS_API QWORD AssetFileList::Write(uint32_t version, QWORD pos, IAssetsWriter *pWriter, bool bigEndian)
{
	for (unsigned int i = 0; i < this->sizeFiles; i++)
	{
		pos = fileInfs[i].Write(version, pos, pWriter, bigEndian);
	}
	return pos;
}

ASSETSTOOLS_API QWORD AssetsFileHeader::Read(QWORD absFilePos, IAssetsReader *pReader)
{
	QWORD curFilePos = absFilePos;
	uint32_t dw00, dw04, dw0C;
	pReader->Read(curFilePos, 4, &dw00); SwapEndians_(dw00); curFilePos += 4;
	pReader->Read(curFilePos, 4, &dw04); SwapEndians_(dw04); curFilePos += 4;
	pReader->Read(curFilePos, 4, &this->format); SwapEndians_(this->format); curFilePos += 4;
	pReader->Read(curFilePos, 4, &dw0C); SwapEndians_(dw0C); curFilePos += 4;
	if (this->format >= 0x16)
	{
		this->unknown00 = (static_cast<QWORD>(dw00) << 32) | dw04;
		//dw0C is padding for format >= 0x16
		pReader->Read(curFilePos, 8, &this->metadataSize); SwapEndians_(this->metadataSize); curFilePos += 8;
		pReader->Read(curFilePos, 8, &this->fileSize); SwapEndians_(this->fileSize); curFilePos += 8;
		pReader->Read(curFilePos, 8, &this->offs_firstFile); SwapEndians_(this->offs_firstFile); curFilePos += 8;
		pReader->Read(curFilePos, 1, &this->endianness); curFilePos += 1; 
		pReader->Read(curFilePos, 3, this->unknown); curFilePos += 3;
		curFilePos += 4; //Padding
	}
	else
	{
		this->unknown00 = 0;
		this->metadataSize = dw00;
		this->fileSize = dw04;
		this->offs_firstFile = dw0C;
		if (this->format < 9 && (this->fileSize > this->metadataSize))
		{
			this->unknown[0] = this->unknown[1] = this->unknown[2] = 0;
			pReader->Read(absFilePos + this->fileSize - this->metadataSize, 1, &this->endianness);
		}
		else
		{
			pReader->Read(curFilePos, 1, &this->endianness); curFilePos += 1; 
			pReader->Read(curFilePos, 3, this->unknown); curFilePos += 3;
		}
	}

	return curFilePos;
}
ASSETSTOOLS_API QWORD AssetsFileHeader::Write(QWORD pos, IAssetsWriter *pWriter)
{
	QWORD qwTmp;
	uint32_t dwTmp;
	if (format >= 0x16)
	{
		qwTmp = SwapEndians(this->unknown00);
		pWriter->Write(pos, 8, &qwTmp); pos += 8;

		dwTmp = SwapEndians(this->format);
		pWriter->Write(pos, 4, &dwTmp); pos += 4;
		dwTmp = 0;
		pWriter->Write(pos, 4, &dwTmp); pos += 4; //Padding

		qwTmp = SwapEndians(this->metadataSize);
		pWriter->Write(pos, 8, &qwTmp); pos += 8;

		qwTmp = SwapEndians(this->fileSize);
		pWriter->Write(pos, 8, &qwTmp); pos += 8;

		qwTmp = SwapEndians(this->offs_firstFile);
		pWriter->Write(pos, 8, &qwTmp); pos += 8;

		pWriter->Write(pos, 1, &this->endianness); pos += 1;
		pWriter->Write(pos, 3, this->unknown); pos += 3;
		dwTmp = 0;
		pWriter->Write(pos, 4, &dwTmp); pos += 4; //Padding
	}
	else
	{
		dwTmp = SwapEndians(static_cast<uint32_t>(this->metadataSize));
		pWriter->Write(pos, 4, &dwTmp); pos += 4;
		dwTmp = SwapEndians(static_cast<uint32_t>(this->fileSize));
		pWriter->Write(pos, 4, &dwTmp); pos += 4;
		dwTmp = SwapEndians(this->format);
		pWriter->Write(pos, 4, &dwTmp); pos += 4;
		dwTmp = SwapEndians(static_cast<uint32_t>(this->offs_firstFile));
		pWriter->Write(pos, 4, &dwTmp); pos += 4;
		/*if (this->format < 9 && (this->fileSize < this->metadataSize))
		{
			writer(pos - 16 + this->fileSize - this->metadataSize, 1, &this->endianness, writerPar); pos += 1;
		}
		else*/
		if (format >= 9)
		{
			pWriter->Write(pos, 1, &this->endianness); pos += 1;
			pWriter->Write(pos, 3, this->unknown); pos += 3;
		}
	}
	

	return pos;
}
ASSETSTOOLS_API unsigned int AssetsFileHeader::GetSizeBytes()
{
	return (format >= 0x16) ? 0x30 : 0x14;// + ((unsigned int)strlen(this->unityVersion) + 1);
}

const uint8_t GlobalTypeTreeStringTable[] = {
	0x41, 0x41, 0x42, 0x42, 0x00, 0x41, 0x6E, 0x69, 0x6D, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x43, 0x6C, 0x69, 0x70, 0x00, 0x41, 0x6E, 0x69, 0x6D, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x43, 0x75, 0x72, 0x76, 0x65, 0x00, 0x41, 0x6E, 0x69, 0x6D, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x53, 0x74, 0x61, 0x74, 0x65, 0x00, 0x41, 0x72, 0x72, 0x61, 0x79, 0x00, 0x42, 0x61, 0x73, 0x65, 0x00, 0x42, 0x69, 0x74, 0x46, 0x69, 0x65, 0x6C, 0x64, 0x00, 0x62, 0x69, 0x74, 0x73, 0x65, 0x74, 0x00, 0x62, 0x6F, 0x6F, 0x6C, 0x00, 0x63, 0x68, 0x61, 0x72, 0x00, 0x43, 0x6F, 0x6C, 0x6F, 0x72, 0x52, 0x47, 0x42, 0x41, 0x00, 0x43, 0x6F, 0x6D, 0x70, 0x6F, 0x6E, 0x65, 0x6E, 0x74, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x64, 0x65, 0x71, 0x75, 0x65, 0x00, 0x64, 0x6F, 0x75, 0x62, 0x6C, 0x65, 0x00, 0x64, 0x79, 0x6E, 0x61, 0x6D, 0x69, 0x63, 0x5F, 0x61, 0x72, 0x72, 0x61, 0x79, 0x00, 0x46, 0x61, 0x73, 0x74, 0x50, 0x72, 0x6F, 0x70, 0x65, 0x72, 0x74, 0x79, 0x4E, 0x61, 0x6D, 0x65, 0x00, 0x66, 0x69, 0x72, 0x73, 0x74, 0x00, 0x66, 0x6C, 0x6F, 0x61, 0x74, 0x00, 0x46, 0x6F, 0x6E, 0x74, 0x00, 0x47, 0x61, 0x6D, 0x65, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x00, 0x47, 0x65, 0x6E, 0x65, 0x72, 0x69, 0x63, 0x20, 0x4D, 0x6F, 0x6E, 0x6F, 0x00, 0x47, 0x72, 0x61, 0x64, 0x69, 0x65, 0x6E, 0x74, 0x4E, 0x45, 0x57, 0x00, 0x47, 0x55, 0x49, 0x44, 0x00, 0x47, 0x55, 0x49, 0x53, 0x74, 0x79, 0x6C, 0x65, 0x00, 0x69, 0x6E, 0x74, 0x00, 0x6C, 0x69, 0x73, 0x74, 0x00, 0x6C, 0x6F, 0x6E, 0x67, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x6D, 0x61, 0x70, 0x00, 0x4D, 0x61, 0x74, 0x72, 0x69, 0x78, 0x34, 0x78, 0x34, 0x66, 0x00, 0x4D, 0x64, 0x46, 0x6F, 0x75, 0x72, 0x00, 0x4D, 0x6F, 0x6E, 0x6F, 0x42, 0x65, 0x68, 0x61, 0x76, 0x69, 0x6F, 0x75, 0x72, 0x00, 0x4D, 0x6F, 0x6E, 0x6F, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x00, 0x6D, 0x5F, 0x42, 0x79, 0x74, 0x65, 0x53, 0x69, 0x7A, 0x65, 0x00, 0x6D, 0x5F, 0x43, 0x75, 0x72, 0x76, 0x65, 0x00, 0x6D, 0x5F, 0x45, 0x64, 0x69, 0x74, 0x6F, 0x72, 0x43, 0x6C, 0x61, 0x73, 0x73, 0x49, 0x64, 0x65, 0x6E, 0x74, 0x69, 0x66, 0x69, 0x65, 0x72, 0x00, 0x6D, 0x5F, 0x45, 0x64, 0x69, 0x74, 0x6F, 0x72, 0x48, 0x69, 0x64, 0x65, 0x46, 0x6C, 0x61, 0x67, 0x73, 0x00, 0x6D, 0x5F, 0x45, 0x6E, 0x61, 0x62, 0x6C, 0x65, 0x64, 0x00, 0x6D, 0x5F, 0x45, 0x78, 0x74, 0x65, 0x6E, 0x73, 0x69, 0x6F, 0x6E, 0x50, 0x74, 0x72, 0x00, 0x6D, 0x5F, 0x47, 0x61, 0x6D, 0x65, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x00, 0x6D, 0x5F, 0x49, 0x6E, 0x64, 0x65, 0x78, 0x00, 0x6D, 0x5F, 0x49, 0x73, 0x41, 0x72, 0x72, 0x61, 0x79, 0x00, 0x6D, 0x5F, 0x49, 0x73, 0x53, 0x74, 0x61, 0x74, 0x69, 0x63, 0x00, 0x6D, 0x5F, 0x4D, 0x65, 0x74, 0x61, 0x46, 0x6C, 0x61, 0x67, 0x00, 0x6D, 0x5F, 0x4E, 0x61, 0x6D, 0x65, 0x00, 0x6D, 0x5F, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x48, 0x69, 0x64, 0x65, 0x46, 0x6C, 0x61, 0x67, 0x73, 0x00, 0x6D, 0x5F, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x49, 0x6E, 0x74, 0x65, 0x72, 0x6E, 0x61, 0x6C, 0x00, 0x6D, 0x5F, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x50, 0x61, 0x72, 0x65, 0x6E, 0x74, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x00, 0x6D, 0x5F, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x00, 0x6D, 0x5F, 0x53, 0x74, 0x61, 0x74, 0x69, 0x63, 0x45, 0x64, 0x69, 0x74, 0x6F, 0x72, 0x46, 0x6C, 0x61, 0x67, 0x73, 0x00, 0x6D, 0x5F, 0x54, 0x79, 0x70, 0x65, 0x00, 0x6D, 0x5F, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x00, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x00, 0x70, 0x61, 0x69, 0x72, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x43, 0x6F, 0x6D, 0x70, 0x6F, 0x6E, 0x65, 0x6E, 0x74, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x47, 0x61, 0x6D, 0x65, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x4D, 0x61, 0x74, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x4D, 0x6F, 0x6E, 0x6F, 0x42, 0x65, 0x68, 0x61, 0x76, 0x69, 0x6F, 0x75, 0x72, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x4D, 0x6F, 0x6E, 0x6F, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x53, 0x70, 0x72, 0x69, 0x74, 0x65, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x54, 0x65, 0x78, 0x74, 0x41, 0x73, 0x73, 0x65, 0x74, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x54, 0x65, 0x78, 0x74, 0x75, 0x72, 0x65, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x54, 0x65, 0x78, 0x74, 0x75, 0x72, 0x65, 0x32, 0x44, 0x3E, 0x00, 0x50, 0x50, 0x74, 0x72, 0x3C, 0x54, 0x72, 0x61, 0x6E, 0x73, 0x66, 0x6F, 0x72, 0x6D, 0x3E, 0x00, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x00, 0x51, 0x75, 0x61, 0x74, 0x65, 0x72, 0x6E, 0x69, 0x6F, 0x6E, 0x66, 0x00, 0x52, 0x65, 0x63, 0x74, 0x66, 0x00, 0x52, 0x65, 0x63, 0x74, 0x49, 0x6E, 0x74, 0x00, 0x52, 0x65, 0x63, 0x74, 0x4F, 0x66, 0x66, 0x73, 0x65, 0x74, 0x00, 0x73, 0x65, 0x63, 0x6F, 0x6E, 0x64, 0x00, 0x73, 0x65, 0x74, 0x00, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x00, 0x73, 0x69, 0x7A, 0x65, 0x00, 0x53, 0x49, 0x6E, 0x74, 0x31, 0x36, 0x00, 0x53, 0x49, 0x6E, 0x74, 0x33, 0x32, 0x00, 0x53, 0x49, 0x6E, 0x74, 0x36, 0x34, 0x00, 0x53, 0x49, 0x6E, 0x74, 0x38, 0x00, 0x73, 0x74, 0x61, 0x74, 0x69, 0x63, 0x76, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x00, 0x73, 0x74, 0x72, 0x69, 0x6E, 0x67, 0x00, 0x54, 0x65, 0x78, 0x74, 0x41, 0x73, 0x73, 0x65, 0x74, 0x00, 0x54, 0x65, 0x78, 0x74, 0x4D, 0x65, 0x73, 0x68, 0x00, 0x54, 0x65, 0x78, 0x74, 0x75, 0x72, 0x65, 0x00, 0x54, 0x65, 0x78, 0x74, 0x75, 0x72, 0x65, 0x32, 0x44, 0x00, 0x54, 0x72, 0x61, 0x6E, 0x73, 0x66, 0x6F, 0x72, 0x6D, 0x00, 0x54, 0x79, 0x70, 0x65, 0x6C, 0x65, 0x73, 0x73, 0x44, 0x61, 0x74, 0x61, 0x00, 0x55, 0x49, 0x6E, 0x74, 0x31, 0x36, 0x00, 0x55, 0x49, 0x6E, 0x74, 0x33, 0x32, 0x00, 0x55, 0x49, 0x6E, 0x74, 0x36, 0x34, 0x00, 0x55, 0x49, 0x6E, 0x74, 0x38, 0x00, 0x75, 0x6E, 0x73, 0x69, 0x67, 0x6E, 0x65, 0x64, 0x20, 0x69, 0x6E, 0x74, 0x00, 0x75, 0x6E, 0x73, 0x69, 0x67, 0x6E, 0x65, 0x64, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x75, 0x6E, 0x73, 0x69, 0x67, 0x6E, 0x65, 0x64, 0x20, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x00, 0x76, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x00, 0x56, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x32, 0x66, 0x00, 0x56, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x33, 0x66, 0x00, 0x56, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x34, 0x66, 0x00, 0x6D, 0x5F, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x69, 0x6E, 0x67, 0x43, 0x6C, 0x61, 0x73, 0x73, 0x49, 0x64, 0x65, 0x6E, 0x74, 0x69, 0x66, 0x69, 0x65, 0x72, 0x00, 0x47, 0x72, 0x61, 0x64, 0x69, 0x65, 0x6E, 0x74, 0x00, 0x54, 0x79, 0x70, 0x65, 0x2A, 0x00, 0x69, 0x6E, 0x74, 0x32, 0x5F, 0x73, 0x74, 0x6F, 0x72, 0x61, 0x67, 0x65, 0x00, 0x69, 0x6E, 0x74, 0x33, 0x5F, 0x73, 0x74, 0x6F, 0x72, 0x61, 0x67, 0x65, 0x00, 0x42, 0x6F, 0x75, 0x6E, 0x64, 0x73, 0x49, 0x6E, 0x74, 0x00, 0x6D, 0x5F, 0x43, 0x6F, 0x72, 0x72, 0x65, 0x73, 0x70, 0x6F, 0x6E, 0x64, 0x69, 0x6E, 0x67, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x00, 0x6D, 0x5F, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x49, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x63, 0x65, 0x00, 0x6D, 0x5F, 0x50, 0x72, 0x65, 0x66, 0x61, 0x62, 0x41, 0x73, 0x73, 0x65, 0x74, 0x00, 0x46, 0x69, 0x6C, 0x65, 0x53, 0x69, 0x7A, 0x65, 0x00, 0x48, 0x61, 0x73, 0x68, 0x31, 0x32, 0x38, 0x00, 0x00
};

ASSETSTOOLS_API QWORD TypeField_0D::Read(QWORD curFilePos, IAssetsReader *pReader, uint32_t format, bool bigEndian)
{
	uint8_t _isArrayTemp;

	pReader->Read(curFilePos, 2, &this->version); curFilePos += 2;
	if (bigEndian)
		SwapEndians_(this->version);
	pReader->Read(curFilePos, 1, &this->depth); curFilePos ++;
	pReader->Read(curFilePos, 1, &_isArrayTemp); curFilePos ++;
	this->isArray = (format >= 0x13) ? _isArrayTemp : (_isArrayTemp != 0 ? 1 : 0);
	pReader->Read(curFilePos, 4, &this->typeStringOffset); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->typeStringOffset);
	pReader->Read(curFilePos, 4, &this->nameStringOffset); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->nameStringOffset);
	pReader->Read(curFilePos, 4, &this->size); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->size);
	pReader->Read(curFilePos, 4, &this->index); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->index);
	pReader->Read(curFilePos, 4, &this->flags); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->flags);

	if (format >= 0x12)
	{
		pReader->Read(curFilePos, 8, &this->unknown1); curFilePos += 8;
	}

	return curFilePos;
}
ASSETSTOOLS_API QWORD TypeField_0D::Write(QWORD curFilePos, IAssetsWriter *pWriter, uint32_t format, bool bigEndian)
{
	uint16_t wTmp; uint32_t dwTmp;

	wTmp = this->version;
	if (bigEndian)
		SwapEndians_(wTmp);
	pWriter->Write(curFilePos, 2, &wTmp); curFilePos += 2;

	pWriter->Write(curFilePos, 1, &this->depth); curFilePos ++;
	pWriter->Write(curFilePos, 1, &this->isArray); curFilePos ++;

	dwTmp = this->typeStringOffset;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	dwTmp = this->nameStringOffset;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	dwTmp = this->size;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	dwTmp = this->index;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	dwTmp = this->flags;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	if (format >= 0x12)
	{
		pWriter->Write(curFilePos, 8, &this->unknown1); curFilePos += 8;
	}

	return curFilePos;
}
ASSETSTOOLS_API const char *TypeField_0D::GetTypeString(const char *stringTable, size_t stringTableLen)
{
	const char *type = NULL;
	if (typeStringOffset & 0x80000000)
	{
		if ((typeStringOffset & 0x7FFFFFFF) < (sizeof(GlobalTypeTreeStringTable)-1))
			type = (const char*)&GlobalTypeTreeStringTable[typeStringOffset & 0x7FFFFFFF];
	}
	else if (typeStringOffset < (stringTableLen-1))
	{
		type = &stringTable[typeStringOffset];
	}
	return type;
}
ASSETSTOOLS_API const char *TypeField_0D::GetNameString(const char *stringTable, size_t stringTableLen)
{
	const char *name = NULL;
	if (nameStringOffset & 0x80000000)
	{
		if ((nameStringOffset & 0x7FFFFFFF) < (sizeof(GlobalTypeTreeStringTable)-1))
			name = (const char*)&GlobalTypeTreeStringTable[nameStringOffset & 0x7FFFFFFF];
	}
	else if (nameStringOffset < (stringTableLen-1))
	{
		name = &stringTable[nameStringOffset];
	}
	return name;
}

ASSETSTOOLS_API QWORD Type_0D::Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter *pWriter, uint32_t version, bool bigEndian, bool secondaryTypeTree)
{
	uint32_t dwTmp; uint16_t wTmp;
	pWriter->Write(absFilePos, 4, &classId); absFilePos += 4;
	if (version >= 16)
	{
		pWriter->Write(absFilePos, 1, &this->unknown16_1); absFilePos += 1;
		if (version >= 17)
		{
			wTmp = this->scriptIndex;
			if (bigEndian)
				SwapEndians_(wTmp);
			pWriter->Write(absFilePos, 2, &wTmp); absFilePos += 2;
		}
	}
	if ((classId < 0) || (this->classId == 0x72) || (this->classId == 0x7C90B5B3) || ((short)this->scriptIndex) >= 0)
	{
		pWriter->Write(absFilePos, 16, this->scriptIDHash.qValue); absFilePos += 16;
	}
	pWriter->Write(absFilePos, 16, this->typeHash.qValue); absFilePos += 16;

	if (hasTypeTree)
	{
		dwTmp = this->typeFieldsExCount;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
		
		dwTmp = this->stringTableLen;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
		for (uint32_t i = 0; i < this->typeFieldsExCount; i++)
		{
			absFilePos = this->pTypeFieldsEx[i].Write(absFilePos, pWriter, version, bigEndian);
		}
		pWriter->Write(absFilePos, this->stringTableLen, this->pStringTable); absFilePos += this->stringTableLen;

		if (version >= 0x15)
		{
			if (!secondaryTypeTree)
			{
				dwTmp = this->depListLen;
				if (bigEndian)
					SwapEndians_(dwTmp);
				pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
				if (bigEndian)
				{
					for (uint32_t i = 0; i < this->depListLen; i++)
					{
						dwTmp = SwapEndians(this->pDepList[i]);
						pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
					}
				}
				else
				{
					pWriter->Write(absFilePos, this->depListLen * 4, this->pDepList); absFilePos += this->depListLen * 4;
				}
			}
			else
			{
				const char *header1 = this->header1 ? this->header1 : "";
				const char *header2 = this->header2 ? this->header2 : "";
				const char *header3 = this->header3 ? this->header3 : "";
				size_t header1Len = strlen(header1) * sizeof(char) + 1;
				size_t header2Len = strlen(header2) * sizeof(char) + 1;
				size_t header3Len = strlen(header3) * sizeof(char) + 1;
				pWriter->Write(absFilePos, header1Len, this->header1); absFilePos += header1Len;
				pWriter->Write(absFilePos, header2Len, this->header2); absFilePos += header2Len;
				pWriter->Write(absFilePos, header3Len, this->header3); absFilePos += header3Len;
			}
		}
	}
	return absFilePos;
}
ASSETSTOOLS_API QWORD Type_0D::Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader *pReader, uint32_t version, bool bigEndian, bool secondaryTypeTree)
{
	QWORD curFilePos = absFilePos;
	if (version >= 0x0D) //Unity 5
	{
		pReader->Read(curFilePos, 4, &this->classId); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->classId);
		if (version >= 16)
		{
			pReader->Read(curFilePos, 1, &this->unknown16_1); curFilePos += 1;
		}
		else
		{
			this->unknown16_1 = 0;
		}
		if (version >= 17)
		{
			pReader->Read(curFilePos, 2, &this->scriptIndex); curFilePos += 2;
			if (bigEndian)
				SwapEndians_(this->scriptIndex);
		}
		else
		{
			this->scriptIndex = 0xFFFF;
		}
		if ((classId < 0) || (this->classId == 0x72) || (this->classId == 0x7C90B5B3) || ((short)this->scriptIndex) >= 0) //MonoBehaviour
		{
			pReader->Read(curFilePos, 16, this->scriptIDHash.qValue); curFilePos += 16;
		}
		pReader->Read(curFilePos, 16, this->typeHash.qValue); curFilePos += 16;
		this->typeFieldsExCount = 0;
		this->pTypeFieldsEx = NULL;
		this->stringTableLen = 0;
		this->pStringTable = NULL;
		this->depListLen = 0;
		this->pDepList = nullptr;
		this->header1 = this->header2 = this->header3 = nullptr;
		if (hasTypeTree)
		{
			uint32_t dwVariableCount;
			uint32_t dwStringTableLen;
			pReader->Read(curFilePos, 4, &dwVariableCount); curFilePos += 4;
			if (bigEndian)
				SwapEndians_(dwVariableCount);
			pReader->Read(curFilePos, 4, &dwStringTableLen); curFilePos += 4;
			if (bigEndian)
				SwapEndians_(dwStringTableLen);
			uint32_t variableFieldsLen = (dwVariableCount * (version >= 0x12 ? 32 : 24)); 
			uint32_t typeTreeLen = variableFieldsLen + dwStringTableLen;
			void *pTreeBuffer = malloc(typeTreeLen + 1);
			if (pTreeBuffer == NULL)
			{
				curFilePos += typeTreeLen;
			}
			else
			{
				pReader->Read(curFilePos, typeTreeLen, pTreeBuffer); curFilePos += typeTreeLen;
				//((uint8_t*)pTreeBuffer)[typeTreeLen] = 0; //make sure the string table is null-terminated
				IAssetsReader *pNewReader = Create_AssetsReaderFromMemory(pTreeBuffer, typeTreeLen, false);
				if (pNewReader != NULL)
				{
					TypeField_0D *pTypeFields = new TypeField_0D[dwVariableCount];
					QWORD newFilePos = 0;
					for (uint32_t i = 0; i < dwVariableCount; i++)
					{
						newFilePos = pTypeFields[i].Read(newFilePos, pNewReader, version, bigEndian);
					}
					this->typeFieldsExCount = dwVariableCount;
					this->pTypeFieldsEx = pTypeFields;

					bool appendNullTerminator = (typeTreeLen == 0) || (((uint8_t*)pTreeBuffer)[typeTreeLen-1]) != 0;
					void *pStringTable = malloc(dwStringTableLen + (appendNullTerminator?1:0));
					if (pStringTable != NULL)
					{
						memcpy(pStringTable, &((uint8_t*)pTreeBuffer)[variableFieldsLen], dwStringTableLen);
						if (appendNullTerminator)
							((uint8_t*)pStringTable)[dwStringTableLen] = 0;
						this->stringTableLen = dwStringTableLen;
						this->pStringTable = (char*)pStringTable;
					}
					else
					{
						this->stringTableLen = 0;
						this->pStringTable = NULL;
					}
					Free_AssetsReader(pNewReader);
				}
				free(pTreeBuffer);
			}
			if (version >= 0x15)
			{
				if (!secondaryTypeTree)
				{
					pReader->Read(curFilePos, 4, &this->depListLen); curFilePos += 4;
					if (bigEndian)
						SwapEndians_(this->depListLen);
					if (static_cast<int>(this->depListLen) >= 0)
					{
						this->pDepList = new unsigned int[this->depListLen];
						pReader->Read(curFilePos, this->depListLen * 4, this->pDepList); curFilePos += this->depListLen * 4;
						if (bigEndian)
						{
							for (uint32_t i = 0; i < this->depListLen; i++)
								SwapEndians_(this->pDepList[i]);
						}
					}
					else
						this->depListLen = 0;
				}
				else //if (secondaryTypeTree)
				{
					char *header[3] = {nullptr, nullptr, nullptr};
					std::vector<char> headerBuf;
					for (int i = 0; i < 3; i++)
					{
						headerBuf.clear();
						size_t start = 0;
						bool foundNullCharacter = false;
						do {
							headerBuf.resize(headerBuf.size() + 32);
							pReader->Read(curFilePos + start, 32, headerBuf.data() + start);
							for (size_t i = start; i < start + 32; i++)
							{
								if (headerBuf[i] == 0)
								{
									foundNullCharacter = true;
									headerBuf.resize(i + 1);
									break;
								}
							}
							start = headerBuf.size();
						} while (!foundNullCharacter);
						curFilePos += headerBuf.size();
						header[i] = new char[headerBuf.size()];
						memcpy(header[i], headerBuf.data(), headerBuf.size());
					}
					this->header1 = header[0];
					this->header2 = header[1];
					this->header3 = header[2];
				}
			}
		}
		return curFilePos;
	}
	else
	{
		memset(this, 0, sizeof(Type_0D));
		return Type_07().Read(hasTypeTree, absFilePos, pReader, version, bigEndian);
	}
}

ASSETSTOOLS_API QWORD TypeField_07::Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader *pReader, uint32_t version, bool bigEndian)
{
	QWORD curFilePos = absFilePos;
	pReader->Read(curFilePos, 256, this->type);
	this->type[255] = 0;
	curFilePos += (strlen(this->type)+1);

	pReader->Read(curFilePos, 256, this->name);
	this->name[255] = 0;
	curFilePos += (strlen(this->name)+1);

	pReader->Read(curFilePos, 4, &this->size); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->size);
	if (version == 2) curFilePos += 4;
	if (version == 3) { this->index = (uint32_t)-1; }
	else
	{
		pReader->Read(curFilePos, 4, &this->index); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->index);
	}
	pReader->Read(curFilePos, 4, &this->arrayFlag); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->arrayFlag);
	pReader->Read(curFilePos, 4, &this->flags1); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->flags1);
	if (version == 3) { this->flags2 = (uint32_t)-1; }
	else
	{
		pReader->Read(curFilePos, 4, &this->flags2); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->flags2);
	}

	if (hasTypeTree)
	{
		pReader->Read(curFilePos, 4, &this->childrenCount); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->childrenCount);
		this->children = new TypeField_07[childrenCount]();
		for (uint32_t i = 0; i < childrenCount; i++)
			curFilePos = this->children[i].Read(hasTypeTree, curFilePos, pReader, version, bigEndian);
	}
	return curFilePos;
}
ASSETSTOOLS_API QWORD TypeField_07::Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter *pWriter, bool bigEndian)
{
	uint32_t dwTmp;
	QWORD curFilePos = absFilePos;
	pWriter->Write(curFilePos, strlen(this->type)+1, this->type); curFilePos += (strlen(this->type)+1);
	
	pWriter->Write(curFilePos, strlen(this->name)+1, this->name); curFilePos += (strlen(this->name)+1);
	
	dwTmp = this->size;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	
	dwTmp = this->index;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	
	dwTmp = this->arrayFlag;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	
	dwTmp = this->flags1;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	
	dwTmp = this->flags2;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;

	if (hasTypeTree)
	{
		dwTmp = this->childrenCount;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
		for (uint32_t i = 0; i < childrenCount; i++)
			curFilePos = this->children[i].Write(hasTypeTree, curFilePos, pWriter, bigEndian);
	}
	return curFilePos;
}
ASSETSTOOLS_API QWORD Type_07::Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter *pWriter, bool bigEndian)
{
	QWORD curFilePos = absFilePos;
	
	uint32_t dwTmp = classId;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	curFilePos = base.Write(hasTypeTree, curFilePos, pWriter, bigEndian);

	return curFilePos;
}
ASSETSTOOLS_API QWORD Type_07::Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader *pReader, uint32_t version, bool bigEndian)
{
	QWORD curFilePos = absFilePos;

	if (version >= 0x0D)
	{
		memset(this, 0, sizeof(Type_07));
		return Type_0D().Read(hasTypeTree, absFilePos, pReader, version, bigEndian);
	}

	pReader->Read(curFilePos, 4, &classId); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(classId);
	curFilePos = base.Read(hasTypeTree, curFilePos, pReader, version, bigEndian);

	return curFilePos;
}

ASSETSTOOLS_API QWORD TypeTree::Write(QWORD absFilePos, IAssetsWriter *pWriter, uint32_t version, bool bigEndian) //Minimum AssetsFile format : 7
{
	uint32_t dwTmp;
	QWORD curFilePos = absFilePos;
	pWriter->Write(curFilePos, strlen(this->unityVersion)+1, this->unityVersion); curFilePos += strlen(this->unityVersion)+1;

	dwTmp = this->platform;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	if (version >= 0x0D) //Unity 5
	{
		pWriter->Write(curFilePos, 1, &hasTypeTree); curFilePos++;
	}

	dwTmp = this->fieldCount;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	if (this->fieldCount > 0)
	{
		if (version < 0x0D)
		{
			if (this->pTypes_Unity4 != NULL)
			{
				for (uint32_t i = 0; i < fieldCount; i++)
					curFilePos = this->pTypes_Unity4[i].Write(hasTypeTree, curFilePos, pWriter, bigEndian);
			}
		}
		else
		{
			if (this->pTypes_Unity5 != NULL)
			{
				for (uint32_t i = 0; i < fieldCount; i++)
					curFilePos = this->pTypes_Unity5[i].Write(hasTypeTree, curFilePos, pWriter, version, bigEndian);
			}
		}
	}
	if (version < 0x0E)
	{
		dwTmp = this->dwUnknown;
		if (bigEndian)
			SwapEndians_(dwTmp);
		//actually belongs to the asset file info tree
		pWriter->Write(curFilePos, 4, &dwTmp); curFilePos += 4;
	}
	return curFilePos;
}
ASSETSTOOLS_API QWORD TypeTree::Read(QWORD absFilePos, IAssetsReader *pReader, uint32_t version, bool bigEndian) //Minimum AssetsFile format : 7
{
	_fmt = version;
	hasTypeTree = true;

	QWORD curFilePos = absFilePos;
	if (version > 6)
	{
		pReader->Read(curFilePos, sizeof(this->unityVersion), this->unityVersion);
		this->unityVersion[sizeof(this->unityVersion) - 1] = 0;
		curFilePos += (strlen(this->unityVersion) + 1);
		if (this->unityVersion[0] < '0' || this->unityVersion[0] > '9')
		{
			this->fieldCount = 0;
			return curFilePos;
		}
		pReader->Read(curFilePos, 4, &this->platform); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->platform);
	}
	else
	{
		this->platform = 0;
		if (version == 6)
		{
			strcpy(this->unityVersion, "Unsupported 2.6+");
		}
		else if (version == 5)
		{
			strcpy(this->unityVersion, "Unsupported 2.0+");
		}
		else
		{
			strcpy(this->unityVersion, "Unsupported Unknown");
		}
		this->fieldCount = 0; //not supported
		return curFilePos;
	}

	if (version >= 0x0D) //Unity 5
	{
		pReader->Read(curFilePos, 1, &hasTypeTree); curFilePos++;
	}
	pReader->Read(curFilePos, 4, &this->fieldCount); curFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->fieldCount);
	if (this->fieldCount > 0)
	{
		if (version < 0x0D)
		{
			this->pTypes_Unity4 = new Type_07[fieldCount]();
			for (uint32_t i = 0; i < fieldCount; i++)
			{
				curFilePos = this->pTypes_Unity4[i].Read(hasTypeTree, curFilePos, pReader, version, bigEndian);
			}
		}
		else
		{
			this->pTypes_Unity5 = new Type_0D[fieldCount]();
			for (uint32_t i = 0; i < fieldCount; i++)
				curFilePos = this->pTypes_Unity5[i].Read(hasTypeTree, curFilePos, pReader, version, bigEndian);
		}
	}
	else
	{
		this->pTypes_Unity4 = NULL;
		this->pTypes_Unity5 = NULL;
	}
	if (version < 0x0E)
	{
		//actually belongs to the asset file info tree
		pReader->Read(curFilePos, 4, &this->dwUnknown); curFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->dwUnknown);
	}
	else
		this->dwUnknown = 0;
	return curFilePos;
}
void __RecursiveDeleteChildren(TypeField_07 *pCurTarget)
{
	for (uint32_t i = 0; i < pCurTarget->childrenCount; i++)
	{
		if (pCurTarget->children[i].childrenCount > 0)
			__RecursiveDeleteChildren(&pCurTarget->children[i]);
	}
	if (pCurTarget->childrenCount > 0)
	{
		delete[] pCurTarget->children;
		pCurTarget->children = NULL;
		pCurTarget->childrenCount = 0;
	}
}
ASSETSTOOLS_API void TypeTree::Clear()
{
	if (fieldCount > 0)
	{
		if (_fmt >= 0x0D)
		{
			for (uint32_t i = 0; i < fieldCount; i++)
			{
				if (pTypes_Unity5[i].stringTableLen > 0)
					free(pTypes_Unity5[i].pStringTable);
				if (pTypes_Unity5[i].typeFieldsExCount > 0)
					delete[] pTypes_Unity5[i].pTypeFieldsEx;
				if (pTypes_Unity5[i].pDepList)
					delete[] pTypes_Unity5[i].pDepList;
				if (pTypes_Unity5[i].header1)
					delete[] pTypes_Unity5[i].header1;
				if (pTypes_Unity5[i].header2)
					delete[] pTypes_Unity5[i].header2;
				if (pTypes_Unity5[i].header3)
					delete[] pTypes_Unity5[i].header3;
			}
			delete[] pTypes_Unity5;
			pTypes_Unity5 = NULL;
			fieldCount = 0;
		}
		else
		{
			for (uint32_t i = 0; i < fieldCount; i++)
			{
				TypeField_07 *pBaseField = &pTypes_Unity4[i].base;
				while (pBaseField->childrenCount > 0)
				{
					__RecursiveDeleteChildren(pBaseField);
				}
			}
			delete[] pTypes_Unity4;
			pTypes_Unity4 = NULL;
			fieldCount = 0;
		}
	}
}

ASSETSTOOLS_API QWORD AssetsFileDependency::GUID128::Read(QWORD absFilePos, IAssetsReader *pReader)
{
	pReader->Read(absFilePos, 8, &this->mostSignificant); absFilePos += 8;
	SwapEndians_<__int64>(this->mostSignificant);
	pReader->Read(absFilePos, 8, &this->leastSignificant); absFilePos += 8;
	SwapEndians_<__int64>(this->leastSignificant);
	return absFilePos;
}
ASSETSTOOLS_API QWORD AssetsFileDependency::GUID128::Write(QWORD absFilePos, IAssetsWriter *pWriter)
{
	__int64 qwTmp = SwapEndians<__int64>(this->mostSignificant);
	pWriter->Write(absFilePos, 8, &qwTmp); absFilePos += 8;
	qwTmp = SwapEndians<__int64>(this->leastSignificant);
	pWriter->Write(absFilePos, 8, &qwTmp); absFilePos += 8;
	return absFilePos;
}

ASSETSTOOLS_API QWORD AssetsFileDependency::Read(QWORD absFilePos, IAssetsReader *pReader, uint32_t format, bool bigEndian)
{
	if (format >= 6)
	{
		pReader->Read(absFilePos, 255, this->bufferedPath);
		this->bufferedPath[255] = 0;
		if (strlen(this->bufferedPath) == 255)
		{
			absFilePos += 255;
			char buf[17];
			buf[16] = 0;
			size_t bufLen;
			do
			{
				buf[0] = 0;
				pReader->Read(absFilePos, 16, buf);
				bufLen = strlen(buf);
				absFilePos += bufLen;
			} while (bufLen == 16);
			absFilePos++;
		}
		else
			absFilePos += (strlen(this->bufferedPath) + 1);
	}
	else
		this->bufferedPath[0] = 0;

	if (format >= 5)
	{
		absFilePos = this->guid.Read(absFilePos, pReader);

		pReader->Read(absFilePos, 4, &this->type); absFilePos += 4;
		if (bigEndian)
			SwapEndians_(this->type);
	}
	else
	{
		this->guid.leastSignificant = this->guid.mostSignificant = 0;
		this->type = 0;
	}

	pReader->Read(absFilePos, 255, this->assetPath);
	this->assetPath[255] = 0;
	if (strlen(this->assetPath) == 255)
	{
		absFilePos += 255;
		char buf[17];
		buf[16] = 0;
		size_t bufLen;
		do
		{
			buf[0] = 0;
			pReader->Read(absFilePos, 16, buf);
			bufLen = strlen(buf);
			absFilePos += bufLen;
		} while (bufLen == 16);
		absFilePos++;
	}
	else
		absFilePos += (strlen(this->assetPath) + 1);

	return absFilePos;
}
ASSETSTOOLS_API QWORD AssetsFileDependency::Write(QWORD absFilePos, IAssetsWriter *pWriter, uint32_t format, bool bigEndian)
{
	if (format >= 6)
	{
		pWriter->Write(absFilePos, strlen(this->bufferedPath)+1, this->bufferedPath); absFilePos += strlen(this->bufferedPath)+1;
	}

	if (format >= 5)
	{
		absFilePos = this->guid.Write(absFilePos, pWriter);

		uint32_t dwTmp = this->type;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
	}
	
	pWriter->Write(absFilePos, strlen(this->assetPath)+1, this->assetPath); absFilePos += strlen(this->assetPath)+1;

	return absFilePos;
}

ASSETSTOOLS_API QWORD AssetsFileDependencyList::Write(QWORD absFilePos, IAssetsWriter *pWriter, uint32_t format, bool bigEndian)
{
	uint32_t dwTmp = this->dependencyCount;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
	//writer(absFilePos, 1, &this->unknown, writerPar); absFilePos += 1;
	for (uint32_t i = 0; i < this->dependencyCount; i++)
		absFilePos = this->pDependencies[i].Write(absFilePos, pWriter, format, bigEndian);

	return absFilePos;
}
ASSETSTOOLS_API QWORD AssetsFileDependencyList::Read(QWORD absFilePos, IAssetsReader *pReader, uint32_t format, bool bigEndian)
{
	//if (format >= 0x0E) //actually another field
	//	absFilePos += 4;
	pReader->Read(absFilePos, 4, &this->dependencyCount); absFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->dependencyCount);
	//reader(absFilePos, 1, &this->unknown, readerPar); absFilePos += 1;
	if (this->dependencyCount > 0)
	{
		this->pDependencies = (AssetsFileDependency*)malloc(this->dependencyCount * sizeof(AssetsFileDependency));
		if (this->pDependencies != NULL)
		{
			for (uint32_t i = 0; i < this->dependencyCount; i++)
				absFilePos = this->pDependencies[i].Read(absFilePos, pReader, format, bigEndian);
		}
		else
		{
			for (uint32_t i = 0; i < this->dependencyCount; i++)
				absFilePos = AssetsFileDependency().Read(absFilePos, pReader, format, bigEndian);
			this->dependencyCount = 0;
		}
	}
	else
		this->pDependencies = NULL;

	return absFilePos;
}

static QWORD SecondaryTypeList_Write(AssetsFile *pAssetsFile, QWORD absFilePos, IAssetsWriter *pWriter, uint32_t format, bool bigEndian)
{
	if (format >= 0x14)
	{
		uint32_t dwTmp = pAssetsFile->secondaryTypeCount;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
		if (pAssetsFile->pSecondaryTypeList != NULL)
		{
			for (uint32_t i = 0; i < pAssetsFile->secondaryTypeCount; i++)
				absFilePos = pAssetsFile->pSecondaryTypeList[i].Write(pAssetsFile->typeTree.hasTypeTree, absFilePos, pWriter, format, bigEndian, true);
		}
	}
	return absFilePos;
}
static QWORD SecondaryTypeList_Read(AssetsFile *pAssetsFile, QWORD absFilePos, IAssetsReader *pReader, uint32_t format, bool bigEndian)
{
	if (format >= 0x14)
	{
		pReader->Read(absFilePos, 4, &pAssetsFile->secondaryTypeCount); absFilePos += 4;
		if (bigEndian)
			SwapEndians_(pAssetsFile->secondaryTypeCount);
		if (pAssetsFile->secondaryTypeCount > 0)
		{
			pAssetsFile->pSecondaryTypeList = new Type_0D[pAssetsFile->secondaryTypeCount]();
			for (uint32_t i = 0; i < pAssetsFile->secondaryTypeCount; i++)
				absFilePos = pAssetsFile->pSecondaryTypeList[i].Read(pAssetsFile->typeTree.hasTypeTree, absFilePos, pReader, format, bigEndian, true);
		}
		else
			pAssetsFile->pSecondaryTypeList = NULL;
	}
	else
	{
		pAssetsFile->secondaryTypeCount = 0;
		pAssetsFile->pSecondaryTypeList = NULL;
	}
	return absFilePos;
}

static QWORD UnknownString_Write(AssetsFile *pAssetsFile, QWORD absFilePos, IAssetsWriter *pWriter, uint32_t format)
{
	if (format >= 5)
	{
		pWriter->Write(absFilePos, strlen(pAssetsFile->unknownString)+1, pAssetsFile->unknownString);
		absFilePos += strlen(pAssetsFile->unknownString)+1;
	}
	return absFilePos;
}
static QWORD UnknownString_Read(AssetsFile *pAssetsFile, QWORD absFilePos, IAssetsReader *pReader, uint32_t format)
{
	if (format >= 5)
	{
		pReader->Read(absFilePos, 63, pAssetsFile->unknownString);
		pAssetsFile->unknownString[63] = 0;
		if (strlen(pAssetsFile->unknownString) == 63)
		{
			absFilePos += 63;
			char buf[17];
			buf[16] = 0;
			size_t bufLen;
			do
			{
				buf[0] = 0;
				pReader->Read(absFilePos, 16, buf);
				bufLen = strlen(buf);
				absFilePos += bufLen;
			} while (bufLen == 16);
			absFilePos++;
		}
		else
			absFilePos += (strlen(pAssetsFile->unknownString) + 1);
	}
	else
		pAssetsFile->unknownString[0] = 0;
	return absFilePos;
}


ASSETSTOOLS_API QWORD PreloadList::Write(QWORD absFilePos, IAssetsWriter *pWriter, uint32_t format, bool bigEndian)
{
	uint32_t dwTmp;

	dwTmp = len;
	if (bigEndian)
		SwapEndians_(dwTmp);
	pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
	for (uint32_t i = 0; i < len; i++)
	{
		dwTmp = items[i].fileID;
		if (bigEndian)
			SwapEndians_(dwTmp);
		pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
		if (format >= 0x0E)
		{
			absFilePos = ((absFilePos + 3) & (~0x3));

			QWORD qwTmp = items[i].pathID;
			if (bigEndian)
				SwapEndians_(qwTmp);
			pWriter->Write(absFilePos, 8, &qwTmp); absFilePos += 8;
		}
		else
		{
			dwTmp = (uint32_t)items[i].pathID;
			if (bigEndian)
				SwapEndians_(dwTmp);
			pWriter->Write(absFilePos, 4, &dwTmp); absFilePos += 4;
		}
	}
	return absFilePos;
}
ASSETSTOOLS_API QWORD PreloadList::Read(QWORD absFilePos, IAssetsReader *pReader, uint32_t format, bool bigEndian)
{
	pReader->Read(absFilePos, 4, &len); absFilePos += 4;
	if (bigEndian)
		SwapEndians_(this->len);
	if (len > 0)
		this->items = new AssetPPtr[len];
	for (uint32_t i = 0; i < len; i++)
	{
		pReader->Read(absFilePos, 4, &items[i].fileID); absFilePos += 4;
		if (bigEndian)
			SwapEndians_(items[i].fileID);
		if (format >= 0x0E)
		{
			absFilePos = ((absFilePos + 3) & (~0x3));
			pReader->Read(absFilePos, 8, &items[i].pathID); absFilePos += 8;
			if (bigEndian)
				SwapEndians_(items[i].pathID);
		}
		else
		{
			uint32_t pathID;
			pReader->Read(absFilePos, 4, &pathID); absFilePos += 4;
			if (bigEndian)
				SwapEndians_(pathID);
			items[i].pathID = (QWORD)pathID;
		}
	}
	return absFilePos;
}
ASSETSTOOLS_API QWORD AssetsFile::Write(IAssetsWriter *pWriter, QWORD filePos, AssetsReplacer **replacers, size_t replacerCount, uint32_t fileID,
			ClassDatabaseFile *typeMeta)
{
	bool isOffsetWriter = filePos != 0;
	QWORD filePosOffset = filePos;
	if (isOffsetWriter)
	{
		pWriter = Create_AssetsWriterToWriterOffset(pWriter, filePos);
		if (pWriter == NULL)
			return 0;
		filePos = 0;
	}
	QWORD headerPos = filePos;
	AssetsFileHeader tempHeader = this->header;
	//tempHeader.endianness = 0;
	filePos = tempHeader.Write(filePos, pWriter);
	unsigned int fileListLen = 0;

	
	this->pReader->Read(this->AssetTablePos, 4, &fileListLen);
	struct exAssetFileInfo
	{
		AssetFileInfo finf;
		uint32_t actualFileType;
		bool isUntouched;
		bool isPreload;
		AssetsEntryReplacer *pReplacer;
	};

	TypeTree newTypeTree = this->typeTree;
	std::vector<Type_0D> types0D;
	std::vector<exAssetFileInfo> assetFiles;
	std::vector<size_t> assetWriteOrder;
	std::list<std::pair<size_t,AssetsReplacer*>> pendingReplacers;
	std::vector<AssetPPtr> preloadDependencies;
	if (this->header.format >= 0x0D)
	{
		types0D.assign(&this->typeTree.pTypes_Unity5[0], &this->typeTree.pTypes_Unity5[this->typeTree.fieldCount]);
		preloadDependencies.assign(&this->preloadTable.items[0], &this->preloadTable.items[this->preloadTable.len]);
	}
	const AssetsFileDependency *pDependencies = this->dependencies.pDependencies;
	size_t dependencyCount = this->dependencies.dependencyCount;

	for (size_t i = 0; i < replacerCount; ++i)
	{
		if (replacers[i] != nullptr
			&& (replacers[i]->GetFileID() == fileID
				|| replacers[i]->GetFileID() == 0
				|| fileID == (uint32_t)-1))
			pendingReplacers.push_back({ i, replacers[i] });
	}
	for (auto pendingReplacersIt = pendingReplacers.begin(); pendingReplacersIt != pendingReplacers.end();)
	{
		AssetsReplacer *pendingReplacer = pendingReplacersIt->second;
		if (this->header.format >= 0x0D
			&& (pendingReplacer->GetType() == AssetsReplacement_Remove
				|| pendingReplacer->GetType() == AssetsReplacement_AddOrModify))
		{
			const AssetPPtr *replacerPreloads = nullptr;
			size_t replacerPreloadCount = 0;
			reinterpret_cast<AssetsEntryReplacer*>(pendingReplacer)
				->GetPreloadDependencies(replacerPreloads, replacerPreloadCount);

			if (pendingReplacer->GetType() == AssetsReplacement_Remove)
			{
				for (size_t j = 0; j < replacerPreloadCount; j++)
				{
					const AssetPPtr &curReplacerPreload = replacerPreloads[j];
					for (size_t k = 0; k < preloadDependencies.size(); k++)
					{
						if (preloadDependencies[k].fileID == curReplacerPreload.fileID && 
							preloadDependencies[k].pathID == curReplacerPreload.pathID)
						{
							preloadDependencies.erase(preloadDependencies.begin() + k);
							break;
						}
					}
				}
			}
			else //AddOrModify
			{
				for (size_t j = 0; j < replacerPreloadCount; j++)
				{
					const AssetPPtr &curReplacerPreload = replacerPreloads[j];
					bool alreadyHasPreload = false;
					for (size_t k = 0; k < preloadDependencies.size(); k++)
					{
						if (preloadDependencies[k].fileID == curReplacerPreload.fileID && 
							preloadDependencies[k].pathID == curReplacerPreload.pathID)
						{
							alreadyHasPreload = true;
							break;
						}
					}
					if (!alreadyHasPreload)
						preloadDependencies.push_back(curReplacerPreload);
				}
			}
		}
		else if (pendingReplacer->GetType() == AssetsReplacement_Dependencies)
		{
			auto* pDepModifier = reinterpret_cast<AssetsDependenciesReplacer*>(pendingReplacer);
			const std::vector<AssetsFileDependency> &dependencies = pDepModifier->GetDependencies();
			pDependencies = dependencies.data();
			dependencyCount = dependencies.size();
		}
		++pendingReplacersIt;
	}

	size_t untouchedAssetCount = 0;
	assetFiles.reserve(fileListLen);
	//For each existing asset in AssetsFile:
	// - Process the replacers for the current path ID in the order of pReplacers.
	// -> The replacer will be removed from pendingReplacers.
	// -> If a remover is encountered, leave all further replacers for that path ID intact.
	//    (Further replacers may add a new asset with that ID, but are not processed in this stage).
	// - If the asset is removed by one of the replacers, skip it.
	// - Otherwise (if the asset is kept unchanged or is modified without having been removed),
	//   add an entry to assetFiles.
	{
		std::unordered_map<uint64_t, std::vector<decltype(pendingReplacers)::iterator>> pendingReplacersLookup(pendingReplacers.size());
		for (auto pendingReplacersIt = pendingReplacers.begin(); pendingReplacersIt != pendingReplacers.end(); ++pendingReplacersIt)
		{
			AssetsReplacer* pendingReplacer = pendingReplacersIt->second;
			if ((pendingReplacer->GetType() == AssetsReplacement_Remove
				|| pendingReplacer->GetType() == AssetsReplacement_AddOrModify))
			{
				uint64_t pathID = reinterpret_cast<AssetsEntryReplacer*>(pendingReplacer)->GetPathID();
				pendingReplacersLookup[pathID].push_back(pendingReplacersIt);
			}
		}
		QWORD readerFilePos = this->AssetTablePos + 4;
		for (unsigned int i = 0; i < fileListLen; i++)
		{
			exAssetFileInfo fileInfo;
			fileInfo.isUntouched = true;
			readerFilePos = fileInfo.finf.Read(this->header.format, readerFilePos, this->pReader, this->header.endianness == 1);
			
			if (this->header.format < 0x10)
			{
				fileInfo.actualFileType = fileInfo.finf.curFileTypeOrIndex;
				if (((int)fileInfo.actualFileType) < 0)
					fileInfo.finf.scriptIndex = (uint16_t)(-((int)fileInfo.actualFileType + 1));
			}
			else
			{
				if (fileInfo.finf.curFileTypeOrIndex >= types0D.size())
					fileInfo.actualFileType = fileInfo.finf.inheritedUnityClass = 0;
				else
				{
					fileInfo.actualFileType = types0D[fileInfo.finf.curFileTypeOrIndex].classId;
					fileInfo.finf.inheritedUnityClass = (uint16_t)types0D[fileInfo.finf.curFileTypeOrIndex].classId;
					if (this->header.format >= 0x11)
						fileInfo.finf.scriptIndex = types0D[fileInfo.finf.curFileTypeOrIndex].scriptIndex;
				}
			}

			fileInfo.pReplacer = NULL;
			bool doIgnore = false;
			//Find the replacers for this asset.
			auto replacersLookupIt = pendingReplacersLookup.find(fileInfo.finf.index);
			if (replacersLookupIt != pendingReplacersLookup.end())
			{
				auto& replacersItVec = replacersLookupIt->second;
				size_t iProcessedReplacer = 0;
				for (iProcessedReplacer = 0; iProcessedReplacer < replacersItVec.size() && !doIgnore; ++iProcessedReplacer)
				{
					AssetsReplacer* pReplacerUnchecked = replacersItVec[iProcessedReplacer]->second;
					unsigned int replacerFileID = pReplacerUnchecked->GetFileID();
					if (pReplacerUnchecked->GetType() == AssetsReplacement_Remove
						|| pReplacerUnchecked->GetType() == AssetsReplacement_AddOrModify)
					{
						AssetsEntryReplacer* pCurReplacer = reinterpret_cast<AssetsEntryReplacer*>(pReplacerUnchecked);
						assert(pCurReplacer->GetPathID() == fileInfo.finf.index);
						if (pCurReplacer->GetPathID() != fileInfo.finf.index)
						{
							assert(false); //Lookup is not valid (even though it should be).
							continue;
						}
						if (pCurReplacer->GetType() == AssetsReplacement_Remove)
							doIgnore = true;
						else if (pCurReplacer->GetType() == AssetsReplacement_AddOrModify)
						{
							bool typeChanged = false;
							if (   (pCurReplacer->GetClassID() < 0
									&& pCurReplacer->GetMonoScriptID() != fileInfo.finf.scriptIndex)
								|| (pCurReplacer->GetClassID() >= 0
									&& pCurReplacer->GetClassID() != fileInfo.actualFileType))
							{
								//If the replacer changes the asset type,
								// behave as if a remover was applied first.
								doIgnore = true;
								//break so the replacer is not removed from pendingReplacers.
								break;
							}
							fileInfo.finf.curFileSize = (uint32_t)pCurReplacer->GetSize();
							fileInfo.pReplacer = pCurReplacer;
						}
						else
							break;
					}
					else
						assert(false);
					pendingReplacers.erase(replacersItVec[iProcessedReplacer]);
				}
				replacersItVec.erase(replacersItVec.begin(), replacersItVec.begin() + iProcessedReplacer);
			}
			if (!doIgnore)
			{
				if (fileInfo.pReplacer == NULL)
					untouchedAssetCount++;
				else
					fileInfo.isUntouched = false;
				fileInfo.isPreload = false;
				for (uint32_t i = 0; i < preloadDependencies.size(); i++)
				{
					if (preloadDependencies[i].fileID == 0 && preloadDependencies[i].pathID == fileInfo.finf.index)
					{
						fileInfo.isPreload = true;
						break;
					}
				}
				if (fileInfo.isPreload)
					assetWriteOrder.push_back(assetFiles.size());
				assetFiles.push_back(fileInfo);
			}
		}
		//Add the remaining assetFiles to assetWriteOrder.
		assetWriteOrder.reserve(assetFiles.size());
		for (size_t i = 0; i < assetFiles.size(); i++)
		{
			if (!assetFiles[i].isPreload)
				assetWriteOrder.push_back(i);
		}
		if (assetWriteOrder.size() != assetFiles.size())
			return NULL; //TODO : Add an error message
	}

	//Process all entry replacers that are not based on assets in the original AssetsFile.
	//TODO: Add a faster assetFiles entry lookup and maintain it inside the loop (as entries are added or removed).
	for (auto pendingReplacersIt = pendingReplacers.begin(); pendingReplacersIt != pendingReplacers.end(); ++pendingReplacersIt)
	{
		AssetsReplacer *pReplacer = pendingReplacersIt->second;
		if (pReplacer->GetType() != AssetsReplacement_Remove
			&& pReplacer->GetType() != AssetsReplacement_AddOrModify)
			continue;
		AssetsEntryReplacer *pCurReplacer = reinterpret_cast<AssetsEntryReplacer*>(pReplacer);
		if ((pCurReplacer->GetFileID() == fileID || pCurReplacer->GetFileID() == 0 || fileID == (uint32_t)-1)
			&& pCurReplacer->GetPathID() != 0)
		{
			__int64 curPathID = (__int64)pCurReplacer->GetPathID();
			bool alreadyExists = false;
			exAssetFileInfo fileInfo;
			exAssetFileInfo *pFileInfo = &fileInfo; size_t existingFileIndex = 0;
			for (size_t k = 0; k < assetFiles.size(); k++)
			{
				if (assetFiles[k].finf.index == curPathID)
				{
					alreadyExists = true;
					pFileInfo = &assetFiles[k];
					existingFileIndex = k;
				}
			}
			if (pCurReplacer->GetType() == AssetsReplacement_AddOrModify)
			{
				pFileInfo->pReplacer = pCurReplacer;
				pFileInfo->finf.curFileSize = (uint32_t)pCurReplacer->GetSize();
				if (this->header.format < 0x10)
				{
					pFileInfo->finf.curFileTypeOrIndex = pCurReplacer->GetClassID();
					pFileInfo->actualFileType = pCurReplacer->GetClassID();
				}
				else
				{
					bool hasIndex = false;
					for (size_t i = 0; i < types0D.size(); i++)
					{
						if ((pCurReplacer->GetClassID() >= 0)
							? (pCurReplacer->GetClassID() == types0D[i].classId)
							: ((types0D[i].classId == 114) && (types0D[i].scriptIndex == pCurReplacer->GetMonoScriptID())))
						{
							pFileInfo->finf.curFileTypeOrIndex = (uint32_t)i;
							pFileInfo->actualFileType = (uint32_t)pCurReplacer->GetClassID();
							pFileInfo->finf.inheritedUnityClass = (uint16_t)types0D[i].classId;
							hasIndex = true;
							break;
						}
					}
					if (!hasIndex)
					{
						Type_0D newType = {};
						newType.classId = (pCurReplacer->GetClassID() >= 0) ? pCurReplacer->GetClassID() : 114; //MonoBehaviour
						newType.pStringTable = NULL;
						newType.pTypeFieldsEx = NULL;
						newType.stringTableLen = 0;
						newType.typeFieldsExCount = 0;
						newType.depListLen = 0;
						newType.pDepList = nullptr;
						newType.header1 = newType.header2 = newType.header3 = nullptr;
						newType.scriptIndex = (pCurReplacer->GetClassID() >= 0) ? 0xFFFF : pCurReplacer->GetMonoScriptID();
						newType.unknown16_1 = 0;
						ClassDatabaseType *pTypeMetaType = nullptr;
						if (typeMeta)
						{
							for (size_t i = 0; i < typeMeta->classes.size(); i++)
							{
								ClassDatabaseType &curType = typeMeta->classes[i];
								if (curType.classId == newType.classId)
								{
									pTypeMetaType = &curType;
									break;
								}
							}
						}
						//Properties hash
						Hash128 hash; bool hasHash = false;
						if (pCurReplacer->GetPropertiesHash(hash)) hasHash = true;
						else if (pTypeMetaType)
						{
							hash = pTypeMetaType->MakeTypeHash(typeMeta);
							hasHash = true;
						}
						if (hasHash)
						{
							newType.typeHash = hash;
						}
						//ScriptID hash
						if (pCurReplacer->GetScriptIDHash(hash))
						{
							newType.scriptIDHash = hash;
						}
						if (typeTree.hasTypeTree)
						{
							std::shared_ptr<ClassDatabaseFile> pCurFile = nullptr;
							ClassDatabaseType *pCurType = nullptr;
							if (!pCurReplacer->GetTypeInfo(pCurFile, pCurType))
							{
								pCurFile.reset(typeMeta, FreeClassDatabase_Dummy);
								pCurType = pTypeMetaType;
							}
							if (pCurType)
							{
								//Generate TypeField_0Ds and the string table from a class database type 
								//TODO: Refactor (move to a new function, e.g. a static one).
								uint32_t newTypeFieldsCount = (uint32_t)pCurType->fields.size();
								TypeField_0D *pNewTypeFields = new TypeField_0D[newTypeFieldsCount];
								char *pNewStringTable = NULL;
								uint32_t newStringTableLen = 0;
								for (uint32_t k = 0; k < newTypeFieldsCount; k++)
								{
									bool foundNameStringOffset = false;
									bool foundTypeStringOffset = false;
									ClassDatabaseTypeField &curField = pCurType->fields[k];
									pNewTypeFields[k].version = curField.version;
									pNewTypeFields[k].depth = curField.depth;
									pNewTypeFields[k].isArray = curField.isArray;
									pNewTypeFields[k].size = curField.size;
									pNewTypeFields[k].index = (uint32_t)k;
									pNewTypeFields[k].flags = curField.flags2;
									memset(&pNewTypeFields[k].unknown1, 0, sizeof(pNewTypeFields[k].unknown1));
									const char *nameString = curField.fieldName.GetString(pCurFile.get());
									const char *typeString = curField.typeName.GetString(pCurFile.get());
									size_t nameStringLen = strlen(nameString);
									size_t typeStringLen = strlen(typeString);
									//GlobalTypeTreeStringTable has 1042 bytes in Unity 5.5.0f3 (format 0x11)
									if (typeStringLen < (sizeof(GlobalTypeTreeStringTable)-1))
									{
										for (uint32_t l = 0; l < (sizeof(GlobalTypeTreeStringTable)-typeStringLen-1); l++)
										{
											if (!memcmp(&GlobalTypeTreeStringTable[l], typeString, typeStringLen+1))
											{
												pNewTypeFields[k].typeStringOffset = l | 0x80000000;
												foundTypeStringOffset = true;
												break;
											}
										}
									}
									if (nameStringLen < (sizeof(GlobalTypeTreeStringTable)-1))
									{
										for (uint32_t l = 0; l < (sizeof(GlobalTypeTreeStringTable)-nameStringLen-1); l++)
										{
											if (!memcmp(&GlobalTypeTreeStringTable[l], nameString, nameStringLen+1))
											{
												pNewTypeFields[k].nameStringOffset = l | 0x80000000;
												foundNameStringOffset = true;
												break;
											}
										}
									}
									if (!foundTypeStringOffset && newStringTableLen && typeStringLen < (newStringTableLen-1))
									{
										for (uint32_t l = 0; l < (newStringTableLen-typeStringLen); l++)
										{
											if (!memcmp(&pNewStringTable[l], typeString, typeStringLen+1))
											{
												pNewTypeFields[k].typeStringOffset = l;
												foundTypeStringOffset = true;
												break;
											}
										}
									}
									if (!foundNameStringOffset && newStringTableLen && nameStringLen < (newStringTableLen-1))
									{
										for (uint32_t l = 0; l < (newStringTableLen-nameStringLen); l++)
										{
											if (!memcmp(&pNewStringTable[l], nameString, nameStringLen+1))
											{
												pNewTypeFields[k].nameStringOffset = l;
												foundNameStringOffset = true;
												break;
											}
										}
									}
									if (!foundTypeStringOffset)
									{
										char *_tmp = (char*)realloc(pNewStringTable, newStringTableLen + typeStringLen + 1);
										if (!_tmp)
										{
											delete[] pNewTypeFields;
											newTypeFieldsCount = 0;
											if (pNewStringTable)
												free(pNewStringTable);
											newStringTableLen = 0;
											break;
										}
										pNewStringTable = _tmp;
										memcpy(&pNewStringTable[newStringTableLen], typeString, typeStringLen + 1);
										pNewTypeFields[k].typeStringOffset = newStringTableLen;
										newStringTableLen += typeStringLen + 1;
									}
									if (!foundNameStringOffset)
									{
										char *_tmp = (char*)realloc(pNewStringTable, newStringTableLen + nameStringLen + 1);
										if (!_tmp)
										{
											delete[] pNewTypeFields;
											newTypeFieldsCount = 0;
											if (pNewStringTable)
												free(pNewStringTable);
											newStringTableLen = 0;
											break;
										}
										pNewStringTable = _tmp;
										memcpy(&pNewStringTable[newStringTableLen], nameString, nameStringLen + 1);
										pNewTypeFields[k].nameStringOffset = newStringTableLen;
										newStringTableLen += nameStringLen + 1;
									}
								}
								newType.pTypeFieldsEx = pNewTypeFields;
								newType.typeFieldsExCount = newTypeFieldsCount;
								newType.pStringTable = pNewStringTable;
								newType.stringTableLen = newStringTableLen;
							}
						}
						pFileInfo->finf.curFileTypeOrIndex = (uint32_t)types0D.size();
						pFileInfo->actualFileType = (uint32_t)pCurReplacer->GetClassID();
						pFileInfo->finf.inheritedUnityClass = (uint16_t)pCurReplacer->GetMonoScriptID();
						types0D.push_back(newType);
					}
				}
				pFileInfo->finf.index = curPathID;
				pFileInfo->isUntouched = false;
				pFileInfo->finf.offs_curFile = 0;
				if (this->header.format < 0x10)
				{
					if (((int)pFileInfo->finf.curFileTypeOrIndex) < 0)
						pFileInfo->finf.inheritedUnityClass = 114; //MonoBehaviour
					else
						pFileInfo->finf.inheritedUnityClass = (uint16_t)pFileInfo->finf.curFileTypeOrIndex;
				}
				pFileInfo->finf.scriptIndex = pCurReplacer->GetMonoScriptID();
				pFileInfo->finf.unknown1 = 0;
				if (!alreadyExists)
				{
					assetWriteOrder.push_back(assetFiles.size());
					assetFiles.push_back(fileInfo);
				}
			}
			else if (pCurReplacer->GetType() == AssetsReplacement_Remove && alreadyExists)
			{
				assetWriteOrder.erase(assetWriteOrder.begin()+existingFileIndex);
				assetFiles.erase(assetFiles.begin()+existingFileIndex);
			}
		}
	}
	if (assetWriteOrder.size() != assetFiles.size())
		return NULL; //TODO : Add an error message

	uint64_t nextAssetOffset = (this->header.format < 9) ? 0x10 : 0;
	
	std::vector<AssetModifierFromReader> untouchedModifiers;
	untouchedModifiers.resize(untouchedAssetCount);
	uint64_t initializedModifierIndex = 0;
	
	//Generate the asset offsets and original copy replacers.
	for (unsigned int i = 0; i < assetWriteOrder.size(); i++)
	{
		exAssetFileInfo &fileInfo = assetFiles[assetWriteOrder[i]];
		if (fileInfo.pReplacer == NULL) //is this file untouched?
		{
			assert(initializedModifierIndex < untouchedAssetCount);
			if (initializedModifierIndex < untouchedAssetCount)
			{
				assert(fileInfo.isUntouched);
				fileInfo.isUntouched = true;
				untouchedModifiers[initializedModifierIndex] = 
					AssetModifierFromReader(fileID, fileInfo.finf.index, fileInfo.actualFileType, fileInfo.finf.scriptIndex, 
						this->pReader, fileInfo.finf.curFileSize, this->header.offs_firstFile + fileInfo.finf.offs_curFile, 16384);
				fileInfo.pReplacer = &untouchedModifiers[initializedModifierIndex];
				initializedModifierIndex++;
			}
		}
		fileInfo.finf.offs_curFile = nextAssetOffset;
		nextAssetOffset += fileInfo.finf.curFileSize;
		nextAssetOffset = (nextAssetOffset+7)&(~7); //use 8-byte alignment here because Unity wants me to
	}

	QWORD metadataSize = 0; QWORD firstFilePos = headerPos;
	if (this->header.format < 9)
	{
		//firstFilePos = 0x10; //for Step 2
		goto WriteAssetsFile_Step2;
	}

WriteAssetsFile_Step1:
	{
		if (this->header.format >= 0x0D)
		{
			newTypeTree.pTypes_Unity5 = types0D.data();
			newTypeTree.fieldCount = types0D.size();
		}
		QWORD typeTreePos = filePos;
		filePos = newTypeTree.Write(filePos, pWriter, this->header.format, this->header.endianness ? true : false);
		QWORD fileTablePos = filePos;

		uint32_t dwTmp = (uint32_t)assetFiles.size();
		if (this->header.endianness)
			SwapEndians_(dwTmp);
		pWriter->Write(filePos, 4, &dwTmp); filePos += 4;
		for (unsigned int i = 0; i < assetFiles.size(); i++)
		{
			exAssetFileInfo &fileInfo = assetFiles[i];
			//Write the asset info
			filePos = fileInfo.finf.Write(this->header.format, filePos, pWriter, this->header.endianness ? true : false);
		}
		QWORD preloadTablePos = filePos;
		if (this->header.format >= 0x0B)
		{
			//Modified preloadTable.
			PreloadList tempPreloadTable;
			tempPreloadTable.items = preloadDependencies.data();
			tempPreloadTable.len = preloadDependencies.size();
			filePos = tempPreloadTable.Write(filePos, pWriter, this->header.format, this->header.endianness ? true : false);
		}
		QWORD dependencyTablePos = filePos;
		AssetsFileDependencyList _dependencies;
		_dependencies.pDependencies = const_cast<AssetsFileDependency*>(pDependencies);
		_dependencies.dependencyCount = (uint32_t)dependencyCount;
		filePos = _dependencies.Write(filePos, pWriter, this->header.format, this->header.endianness ? true : false);
		filePos = SecondaryTypeList_Write(this, filePos, pWriter, this->header.format, this->header.endianness ? true : false);
		filePos = UnknownString_Write(this, filePos, pWriter, this->header.format);
		metadataSize = filePos;
		if (this->header.format < 9)
		{
			//firstFilePos = 0; //for writing the header
			metadataSize -= typeTreePos; //TODO: Make sure this still works
			//(previously increased metadataSize by 1 because of the endianness byte, but it was more likely because of the missing field after the dependencies list)
		}
		else
		{
			metadataSize -= (headerPos + this->header.GetSizeBytes());
			if (filePos < this->header.offs_firstFile)
				firstFilePos = this->header.offs_firstFile;
			else
				firstFilePos = (filePos+15) & (~15);//(filePos+0xFFF) & (~0xFFF);
		}
		//QWORD firstFilePos;//= (filePos+15) & (~15);
	}
	if (this->header.format < 9)
		goto WriteAssetsFile_Step3;
WriteAssetsFile_Step2:
	{
		filePos = firstFilePos;
		QWORD qwNull = 0;
		for (unsigned int i = 0; i < assetWriteOrder.size(); i++)
		{
			exAssetFileInfo *fileInfo = &assetFiles[assetWriteOrder[i]];
			QWORD targetPos = firstFilePos + fileInfo->finf.offs_curFile;
			while ((targetPos-filePos) > 8)
			{
				if (filePos >= (headerPos + 0x10)) //for header.format < 9
					pWriter->Write(filePos, 8, &qwNull);
				filePos += 8;
			}
			if ((targetPos-filePos) > 0)
			{
				pWriter->Write(filePos, (targetPos-filePos), &qwNull);
				filePos = targetPos;
			}
			filePos = fileInfo->pReplacer->Write(targetPos, pWriter);
		}
	}
	if (this->header.format < 9)
	{
		pWriter->Write(filePos, 1, &this->header.endianness); filePos++;
		goto WriteAssetsFile_Step1;
	}
WriteAssetsFile_Step3:
	for (size_t i = this->typeTree.fieldCount; i < types0D.size(); i++)
	{
		if (types0D[i].pStringTable)
			free(types0D[i].pStringTable);
		if (types0D[i].pTypeFieldsEx)
			delete[] types0D[i].pTypeFieldsEx;
		if (types0D[i].pDepList)
			delete[] types0D[i].pDepList;
		if (types0D[i].header1)
			delete[] types0D[i].header1;
		if (types0D[i].header2)
			delete[] types0D[i].header2;
		if (types0D[i].header3)
			delete[] types0D[i].header3;
	}

	AssetsFileHeader newHeader = this->header;
	newHeader.metadataSize = metadataSize;
	newHeader.fileSize = (filePos - headerPos);
	newHeader.offs_firstFile = (firstFilePos - headerPos);
	//newHeader.endianness = 0;
	newHeader.Write(headerPos, pWriter);
	if (isOffsetWriter)
		Free_AssetsWriter(pWriter);
	return newHeader.fileSize + headerPos + filePosOffset;
}
ASSETSTOOLS_API AssetsFile::AssetsFile(IAssetsReader *pReader)
{
	pReader->Seek(AssetsSeek_Begin, 0);
	this->pReader = pReader;
	QWORD filePos;
	filePos = this->header.Read(0, pReader);
	//simple validity check
	if (!this->header.format || this->header.format > 0x40)
		goto AssetsFile_Break_InvalidFile;
	//if (this->header.endianness) //-> big endian not supported
	//	goto AssetsFile_Break_InvalidFile;
	if (this->header.format < 9)
		filePos = this->header.fileSize - this->header.metadataSize + 1;
	filePos = this->typeTree.Read(filePos, pReader, this->header.format, this->header.endianness == 1);
	if (this->typeTree.unityVersion[0] < '0' || this->typeTree.unityVersion[0] > '9')
	{
		this->typeTree.Clear();
		goto AssetsFile_Break_InvalidFile;
	}
	this->AssetTablePos = (uint32_t)filePos;

	{
		AssetFileList tmpFileList; tmpFileList.sizeFiles = 0;
		pReader->Read(filePos, 4, &tmpFileList.sizeFiles);
		if (this->header.endianness)
			SwapEndians_(tmpFileList.sizeFiles);
		this->AssetCount = tmpFileList.sizeFiles;
		filePos += 4;

		if (this->header.format >= 0x0E && (this->AssetCount > 0))
			filePos = ((filePos + 3) & (~0x3));
		filePos += tmpFileList.GetSizeBytes(this->header.format);
	}
	if (this->header.format >= 0x0B)
	{
		filePos = this->preloadTable.Read(filePos, pReader, this->header.format, this->header.endianness == 1);
	}
	else
	{
		this->preloadTable.len = 0;
		this->preloadTable.items = NULL;
	}
	filePos = this->dependencies.Read(filePos, pReader, this->header.format, this->header.endianness == 1);
	filePos = SecondaryTypeList_Read(this, filePos, pReader, this->header.format, this->header.endianness == 1);
	filePos = UnknownString_Read(this, filePos, pReader, this->header.format);
	return;
	AssetsFile_Break_InvalidFile:
	{
		this->preloadTable.len = 0;
		this->preloadTable.items = NULL;
		this->AssetTablePos = 0;
		this->AssetCount = 0;
		this->typeTree.fieldCount = 0;
		this->dependencies.dependencyCount = 0;
		this->dependencies.pDependencies = NULL;
		this->secondaryTypeCount = 0;
		this->pSecondaryTypeList = NULL;
		this->unknownString[0] = 0;
		return;
	}
}
ASSETSTOOLS_API AssetsFile::~AssetsFile()
{
	this->typeTree.Clear();
	if (this->preloadTable.len > 0)
	{
		free(this->preloadTable.items);
		this->preloadTable.items = NULL;
		this->preloadTable.len = 0;
	}
	if (this->dependencies.dependencyCount > 0)
	{
		free(this->dependencies.pDependencies);
		this->dependencies.pDependencies = NULL;
		this->dependencies.dependencyCount = 0;
	}
	if (this->secondaryTypeCount > 0)
	{
		for (uint32_t i = 0; i < this->secondaryTypeCount; i++)
		{
			if (this->pSecondaryTypeList[i].stringTableLen > 0)
				free(this->pSecondaryTypeList[i].pStringTable);
			if (this->pSecondaryTypeList[i].typeFieldsExCount > 0)
				delete[] this->pSecondaryTypeList[i].pTypeFieldsEx;
			if (this->pSecondaryTypeList[i].pDepList)
				delete[] this->pSecondaryTypeList[i].pDepList;
			if (this->pSecondaryTypeList[i].header1)
				delete[] this->pSecondaryTypeList[i].header1;
			if (this->pSecondaryTypeList[i].header2)
				delete[] this->pSecondaryTypeList[i].header2;
			if (this->pSecondaryTypeList[i].header3)
				delete[] this->pSecondaryTypeList[i].header3;
		}
		delete[] this->pSecondaryTypeList;
		this->pSecondaryTypeList = NULL;
		this->secondaryTypeCount = 0;
	}
}

uint32_t SwapEndians(uint32_t old)
{
	uint32_t ret = (((old & 0xFF000000) >> 24) + ((old & 0x00FF0000) >> 8) + ((old & 0x0000FF00) << 8) + ((old & 0x000000FF) << 24));
	return ret;
}
void SwapEndians_(uint32_t& old)
{
	old = (((old & 0xFF000000) >> 24) + ((old & 0x00FF0000) >> 8) + ((old & 0x0000FF00) << 8) + ((old & 0x000000FF) << 24));
}

bool HasName(uint32_t type)
{
	switch (type)
	{
	case 21:
	case 27:
	case 28:
	case 43:
	case 48:
	case 49:
	case 62:
	case 72:
	case 74:
	case 83:
	case 84:
	case 86:
	case 89:
	case 90:
	case 91:
	case 93:
	case 109:
	case 115:
	case 117:
	case 121:
	case 128:
	case 134:
	case 142:
	case 150:
	case 152:
	case 156:
	case 158:
	case 171:
	case 184:
	case 185:
	case 186:
	case 187:
	case 188:
	case 194:
	case 200:
	case 207:
	case 213:
	case 221:
	case 226:
	case 228:
	case 237:
	case 238:
	case 240:
	case 258:
	case 271:
	case 272:
	case 273:
	case 290:
	case 319:
	case 329:
	case 363:
	case 850595691:
	case 1480428607:
	case 687078895:
	case 825902497:
	case 2083778819:
	case 1953259897:
	case 2058629509:
		return true;
	default:
		return false;
	}
}

void _BlankVerifyLogger(const char *message){}
ASSETSTOOLS_API bool AssetsFile::VerifyAssetsFile(AssetsFileVerifyLogger logger)
{
	char sprntTmp[100]; bool ret;
	QWORD fileListOffs = 0; int fileListSize = 0;
	AssetFileList *pFileList = NULL; //AssetsFileHeader *pFileHeader = NULL;
	const void *errorData = 0; void *errorData2 = 0;
	uint8_t tmpLastFile; AssetFileInfo *pLastFileInfo;
	if (logger == NULL) logger = &_BlankVerifyLogger;
	if (pReader == NULL) {
		logger("ERROR: The AssetsFileReader is NULL!");
		return false;
	}
	size_t allocCount;

	/*pFileHeader = (AssetsFileHeader*)malloc(sizeof(AssetsFileHeader) + 11);
	if (!pFileHeader)
	{
		errorData = (void*)(sizeof(AssetsFileHeader) + 11);
		goto _mallocError;
	}
	ZeroMemory(pFileHeader, sizeof(AssetsFileHeader) + 11);
	if (!reader(0, sizeof(AssetsFileHeader) + 10, pFileHeader, readerPar))
	{
		errorData = (void*)0; errorData2 = (void*)(sizeof(AssetsFileHeader) + 10);
		goto _readerError;
	}*/
	if (!this->header.format || this->header.format > 0x40)
	{
		errorData = "Invalid file format";
		goto _fileFormatError;
	}
	if (this->typeTree.unityVersion[0] == 0 || this->typeTree.unityVersion[0] < '0' || this->typeTree.unityVersion[0] > '9')
	{
		char sprntTmp2[100];
		sprintf_s(sprntTmp2, "Invalid version string at %llX", (uint64_t)((uintptr_t)(&this->typeTree.unityVersion[0]) - (uintptr_t)(&this->header)));
		errorData = sprntTmp2;
		goto _fileFormatError;
	}
	sprintf_s(sprntTmp, "INFO: The .assets file was built for Unity %s.", this->typeTree.unityVersion);
	logger(sprntTmp);

	if (this->header.format > 0x16 || this->header.format < 0x08)
		logger("WARNING: AssetsTools (for .assets versions 8-22) wasn't tested with this .assets' version, likely parsing or writing the file won't work properly!");

	fileListOffs = this->AssetTablePos;
	fileListSize = 0;
	if (!pReader->Read(fileListOffs, 4, &fileListSize))
	{
		errorData = (void*)fileListOffs; errorData2 = (void*)4;
		goto _readerError;
	}
	if (this->header.endianness)
		SwapEndians_(fileListSize);
	fileListOffs += 4;
	if (this->header.format >= 0x0E)
		fileListOffs = ((fileListOffs + 3) & (~0x3)); //align to 4-byte boundary

	
	allocCount = (uintptr_t)&pFileList->fileInfs[fileListSize];//(fileListSize * sizeof(AssetFileInfo)) + 4 + 4;
	pFileList = (AssetFileList*)malloc(allocCount);
	if (!pFileList)
	{
		errorData = (void*)(allocCount);
		goto _mallocError;
	}
	pFileList->sizeFiles = fileListSize;
	pFileList->Read(this->header.format, fileListOffs, pReader, this->header.endianness);
	/*if (!reader(fileListOffs, fileListSize, pFileList->fileInfs, readerPar))
	{
		errorData = (void*)fileListOffs; errorData2 = (void*)(4 + fileListSize);
		goto _readerError;
	}*/

	sprintf_s(sprntTmp, "INFO: The .assets file has %u assets (info list : %u bytes).", pFileList->sizeFiles, pFileList->GetSizeBytes(this->header.format));
	logger(sprntTmp);
	
	if (pFileList->sizeFiles > 0)
	{
		if (this->header.metadataSize < 8)
		{
			errorData = "Invalid metadata size";
			goto _fileFormatError;
		}
		pLastFileInfo = &pFileList->fileInfs[pFileList->sizeFiles-1];
		if ((this->header.offs_firstFile + pLastFileInfo->offs_curFile + pLastFileInfo->curFileSize - 1) < this->header.metadataSize)
		{
			errorData = "Last asset begins before the header ends";
			goto _fileFormatError;
		}
		//sprintf_s(sprntTmp, "%u;%u;%u.", pLastFileInfo->index, pLastFileInfo->offs_curFile, pLastFileInfo->curFileSize);
		if (!pReader->Read(this->header.offs_firstFile + pLastFileInfo->offs_curFile + pLastFileInfo->curFileSize - 1, 1, &tmpLastFile))
		{
			errorData = "File data are cut off";
			goto _fileFormatError;
		}
	}

	logger("SUCCESS: The .assets file seems to be ok!");
	ret = true;
	goto _cleanup;

	_cleanup:
	//if (pFileHeader != NULL) free(pFileHeader);
	if (pFileList != NULL) free(pFileList);
	return ret;

	_readerError:
	ret = false;
	sprintf_s(sprntTmp, "ERROR: Invalid .assets file (reading %u bytes at %p in the .assets file failed)!", (unsigned int)errorData2, errorData);
	logger(sprntTmp);
	goto _cleanup;

	_mallocError:
	ret = false;
	sprintf_s(sprntTmp, "ERROR: Out of Memory : Allocating %u bytes failed!", (unsigned int)errorData);
	logger(sprntTmp);
	goto _cleanup;

	_fileFormatError:
	ret = false;
	sprintf_s(sprntTmp, "ERROR: Invalid .assets file (error message : '%s')!", (const char*)errorData);
	logger(sprntTmp);
	goto _cleanup;
}
