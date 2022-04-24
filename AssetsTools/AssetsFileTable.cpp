#include "stdafx.h"
#include "AssetsFileTable.h"
#include <stdint.h>
#include <memory>
#include <assert.h>

ASSETSTOOLS_API AssetsFileTable::AssetsFileTable(AssetsFile *pFile/*, bool readNames*/)
{
	this->pFile = pFile;
	this->pReader = pFile->pReader; 
	//this->pLookupBase = NULL;

	//AssetsFileHeader header;
	//reader(0, sizeof(AssetsFileHeader), &header, readerPar);

	QWORD fileListPos = pFile->AssetTablePos;
	unsigned int fileCount = 0;
	pReader->Read(fileListPos, 4, &fileCount);
	if (pFile->header.endianness)
		SwapEndians_(fileCount);
	fileListPos += 4;
	if (pFile->header.format >= 0x0E)
		fileListPos = ((fileListPos + 3) & (~0x3)); //align to 4-byte boundary

	this->assetFileInfoCount = fileCount;

	//AssetFileInfo *pFileInfo = new AssetFileInfo[fileCount];
	AssetFileInfoEx *pFileInfoEx = new AssetFileInfoEx[fileCount];

	for (unsigned int i = 0; i < fileCount; i++)
	{
		fileListPos = pFileInfoEx[i].Read(pFile->header.format, fileListPos, pReader, pFile->header.endianness);
		if (pFile->header.format >= 0x10)
		{
			if (pFileInfoEx[i].curFileTypeOrIndex >= pFile->typeTree.fieldCount)
			{
				pFileInfoEx[i].curFileType = 0x80000000;
				pFileInfoEx[i].inheritedUnityClass = 0xFFFF;
				pFileInfoEx[i].scriptIndex = 0xFFFF;
			}
			else
			{
				uint32_t classId = pFile->typeTree.pTypes_Unity5[pFileInfoEx[i].curFileTypeOrIndex].classId;
				if (pFile->typeTree.pTypes_Unity5[pFileInfoEx[i].curFileTypeOrIndex].scriptIndex != 0xFFFF)
				{
					pFileInfoEx[i].curFileType = (int)(-1 - (int)pFile->typeTree.pTypes_Unity5[pFileInfoEx[i].curFileTypeOrIndex].scriptIndex);
					pFileInfoEx[i].inheritedUnityClass = (uint16_t)classId;
					pFileInfoEx[i].scriptIndex = pFile->typeTree.pTypes_Unity5[pFileInfoEx[i].curFileTypeOrIndex].scriptIndex;
				}
				else
				{
					pFileInfoEx[i].curFileType = classId;
					pFileInfoEx[i].inheritedUnityClass = (uint16_t)classId;
					pFileInfoEx[i].scriptIndex = 0xFFFF;
				}
			}
		}
		else
			pFileInfoEx[i].curFileType = pFileInfoEx[i].curFileTypeOrIndex;
		pFileInfoEx[i].absolutePos = pFile->header.offs_firstFile + pFileInfoEx[i].offs_curFile;
	}
	this->pAssetFileInfo = pFileInfoEx;
}
ASSETSTOOLS_API bool AssetFileInfoEx::ReadName(AssetsFile *pFile, std::string &out, IAssetsReader *pReaderView)
{
	out.clear();
	if (HasName(this->curFileType))
	{
		if (!pReaderView)
			pReaderView = pFile->pReader;
		unsigned int nameSize = 0;
		if (pReaderView->Read(this->absolutePos, 4, &nameSize) != 4)
			return false;

		if (pFile->header.endianness != 0) SwapEndians_(nameSize);
		if (nameSize + 4 >= this->curFileSize || nameSize >= 4092)
			return false;

		out.resize(nameSize, 0);
		if (pReaderView->Read(this->absolutePos + 4, nameSize, out.data()) != nameSize)
		{
			out.clear();
			return false;
		}
		for (size_t i = 0; i < nameSize; i++)
		{
			if (out[i] < 0x20)
			{
				out.clear();
				return false;
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

ASSETSTOOLS_API AssetFileInfoEx *AssetsFileTable::getAssetInfo(QWORD pathId)
{
	if (!lookup.empty())
	{
		AssetFileInfoEx tmp;
		tmp.index = pathId;
		auto it = lookup.find(&tmp);
		if (it != lookup.end())
		{
			return it->pInfo;
		}
	}
	for (unsigned int i = 0; i < assetFileInfoCount; i++)
	{
		if (pAssetFileInfo[i].index == pathId)
			return &pAssetFileInfo[i];
	}
	return NULL;
}

ASSETSTOOLS_API AssetsFile *AssetsFileTable::getAssetsFile()
{
	return pFile;
}
ASSETSTOOLS_API IAssetsReader *AssetsFileTable::getReader()
{
	return pReader;
}

ASSETSTOOLS_API bool AssetsFileTable::GenerateQuickLookupTree()
{
	{
		std::set<AssetFileInfoEx_KeyRef> tmp;
		lookup.swap(tmp);
	}
	struct LookupIterator
	{
		AssetFileInfoEx *pFileTable;
		size_t fileTableSize;
		size_t i;
		inline LookupIterator()
			: pFileTable(nullptr), fileTableSize(0), i(0)
		{}
		inline LookupIterator(AssetFileInfoEx *pFileTable, size_t fileTableSize, size_t i)
			: pFileTable(pFileTable), fileTableSize(fileTableSize), i(i)
		{}
		inline LookupIterator(LookupIterator &&other)
		{
			(*this) = std::move(other);
		}
		inline LookupIterator(const LookupIterator &other)
		{
			(*this) = other;
		}
		inline LookupIterator &operator=(const LookupIterator &other)
		{
			this->pFileTable = other.pFileTable;
			this->fileTableSize = other.fileTableSize;
			this->i = other.i;
			return (*this);
		}
		inline LookupIterator &operator=(LookupIterator &&other)
		{
			this->pFileTable = other.pFileTable;
			this->fileTableSize = other.fileTableSize;
			this->i = other.i;
			other.i = other.fileTableSize;
			return (*this);
		}
		inline bool operator==(const LookupIterator &other)
		{
			return (pFileTable == other.pFileTable && fileTableSize == other.fileTableSize && i == other.i)
				|| (i >= fileTableSize && other.i >= other.fileTableSize);
		}
		inline bool operator!=(const LookupIterator &other)
		{
			return !((*this) == other);
		}
		inline LookupIterator &operator++() //pre-increment
		{
			if (i < fileTableSize) i++;
			return (*this);
		}
		inline LookupIterator operator++(int) //post-increment
		{
			LookupIterator ret = (*this);
			++(*this);
			return ret;
		}
		AssetFileInfoEx_KeyRef resultTmp;
		inline AssetFileInfoEx_KeyRef &operator*()
		{
			assert(i < fileTableSize);
			resultTmp = AssetFileInfoEx_KeyRef(&pFileTable[i]);
			return resultTmp;
		}
		inline AssetFileInfoEx_KeyRef *operator->()
		{
			return &(*(*this)); //&this->operator*()
		}
	};
	lookup.insert(LookupIterator(pAssetFileInfo, assetFileInfoCount, 0), LookupIterator());
	return true;
	//FreeQuickLookupTree<unsigned long long, AssetsFileTable>(pLookupBase);
	//pLookupBase = NULL;
	//return ::GenerateQuickLookupTree<unsigned long long, AssetsFileTable>(this, pLookupBase);
}

ASSETSTOOLS_API AssetsFileTable::~AssetsFileTable()
{
	if (pAssetFileInfo != NULL)
		delete[] pAssetFileInfo;
	//FreeQuickLookupTree<unsigned long long, AssetsFileTable>(pLookupBase);
}
