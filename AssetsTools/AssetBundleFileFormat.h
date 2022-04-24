#pragma once
#ifndef __AssetsTools__AssetBundleFormat_Header
#define __AssetsTools__AssetBundleFormat_Header
#include "defines.h"
#include "BundleReplacer.h"
#include "ClassDatabaseFile.h"

class AssetBundleFile;
struct AssetBundleHeader06;
struct AssetBundleHeader03;
struct AssetBundleEntry;
struct AssetBundleList;

struct AssetBundleDirectoryInfo06
{
	QWORD offset;
	QWORD decompressedSize;
	uint32_t flags; //(flags & 4) : has serialized data
	const char *name;
	ASSETSTOOLS_API QWORD GetAbsolutePos(AssetBundleHeader06 *pHeader);
	ASSETSTOOLS_API QWORD GetAbsolutePos(class AssetBundleFile *pFile);
};
struct AssetBundleBlockInfo06
{
	uint32_t decompressedSize;
	uint32_t compressedSize;
	uint16_t flags; //(flags & 0x40) : is streamed (read in 64KiB blocks); (flags & 0x3F) :  compression info;
	inline uint8_t GetCompressionType() { return (uint8_t)(flags & 0x3F); }
	//example flags (LZMA, streamed) : 0x41
	//example flags (LZ4, not streamed) : 0x03
	//https://docs.unity3d.com/530/Documentation/Manual/AssetBundleCompression.html
	//LZMA is used as stream-based compression (the whole file is decompressed before usage),
	// LZ4 as chunk-based compression (many equally large, independent LZ4 chunks for random access, decompressed in memory).
};
struct AssetBundleBlockAndDirectoryList06
{
	QWORD checksumLow;
	QWORD checksumHigh;
	uint32_t blockCount;
	AssetBundleBlockInfo06 *blockInf;
	uint32_t directoryCount;
	AssetBundleDirectoryInfo06 *dirInf;
	
	ASSETSTOOLS_API void Free();
	ASSETSTOOLS_API bool Read(QWORD filePos, IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL);
	//Write doesn't compress
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
};

#define LargestBundleHeader AssetBundleHeader03
//Unity 5.3+
struct AssetBundleHeader06
{
	//no alignment in this struct!
	char signature[13]; //0-terminated; UnityFS, UnityRaw, UnityWeb or UnityArchive
	uint32_t fileVersion; //big-endian, = 6
	char minPlayerVersion[24]; //0-terminated; 5.x.x
	char fileEngineVersion[64]; //0-terminated; exact unity engine version
	QWORD totalFileSize;
	//sizes for the blocks info :
	uint32_t compressedSize;
	uint32_t decompressedSize;
	//(flags & 0x3F) is the compression mode (0 = none; 1 = LZMA; 2-3 = LZ4)
	//(flags & 0x40) says whether the bundle has directory info
	//(flags & 0x80) says whether the block and directory list is at the end
	uint32_t flags;
	
	ASSETSTOOLS_API bool ReadInitial(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
	inline QWORD GetBundleInfoOffset()
	{
		if (this->flags & 0x80)
		{
			if (this->totalFileSize == 0)
				return -1;
			return this->totalFileSize - this->compressedSize;
		}
		else
		{
			//if (!strcmp(this->signature, "UnityWeb") || !strcmp(this->signature, "UnityRaw"))
			//	return 9;
			QWORD ret = strlen(minPlayerVersion) + strlen(fileEngineVersion) + 0x1A;
			if (this->flags & 0x100)
				ret = (ret + 0x0A);
			else
				ret = (ret + strlen(signature) + 1);
			if (this->fileVersion >= 7)
				ret = (ret + 15) & ~15; //16 byte alignment
			return ret;
		}
	}
	inline uint32_t GetFileDataOffset()
	{
		uint32_t ret = 0;
		if (!strcmp(this->signature, "UnityArchive"))
			return this->compressedSize;
		else if (!strcmp(this->signature, "UnityFS") || !strcmp(this->signature, "UnityWeb"))
		{
			ret = (uint32_t)strlen(minPlayerVersion) + (uint32_t)strlen(fileEngineVersion) + 0x1A;
			if (this->flags & 0x100)
				ret += 0x0A;
			else
				ret += (uint32_t)strlen(signature) + 1;
			if (this->fileVersion >= 7)
				ret = (ret + 15) & ~15; //16 byte alignment
		}
		if (!(this->flags & 0x80))
			ret += this->compressedSize;
		return ret;
	}
};

struct AssetBundleHeader03
{
	char signature[13]; //0-terminated; UnityWeb or UnityRaw
	uint32_t fileVersion; //big-endian; 3 : Unity 3.5 and 4;
	char minPlayerVersion[24]; //0-terminated; 3.x.x -> Unity 3.x.x/4.x.x; 5.x.x
	char fileEngineVersion[64]; //0-terminated; exact unity engine version
	uint32_t minimumStreamedBytes; //big-endian; not always the file's size
	uint32_t bundleDataOffs; //big-endian;
	uint32_t numberOfAssetsToDownload; //big-endian;
	uint32_t blockCount; //big-endian;
	struct AssetBundleOffsetPair *pBlockList;
	uint32_t fileSize2; //big-endian; for fileVersion >= 2
	uint32_t unknown2; //big-endian; for fileVersion >= 3
	uint8_t unknown3;

	ASSETSTOOLS_API bool Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
};

struct AssetBundleEntry
{
	uint32_t offset;
	uint32_t length;
	char name[1];
	ASSETSTOOLS_API unsigned int GetAbsolutePos(AssetBundleHeader03 *pHeader);//, uint32_t listIndex);
	ASSETSTOOLS_API unsigned int GetAbsolutePos(class AssetBundleFile *pFile);//, uint32_t listIndex);
};
struct AssetsList
{
	uint32_t pos;
	uint32_t count;
	AssetBundleEntry **ppEntries;
	uint32_t allocatedCount;
	//AssetBundleEntry entries[0];
	ASSETSTOOLS_API void Free();
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Write(IAssetsReader *pReader, 
		IAssetsWriter *pWriter, bool doWriteAssets, QWORD &curReadPos, QWORD *curWritePos = NULL,
		AssetsFileVerifyLogger errorLogger = NULL);
};
struct AssetBundleOffsetPair
{
	uint32_t compressed;
	uint32_t uncompressed;
};
enum ECompressionTypes
{
	COMPRESS_NONE,
	COMPRESS_LZMA,
	COMPRESS_LZ4, //experimental, compressor may have bugs
	COMPRESS_MAX
};
class AssetBundleFile
{
	public:
		union {
			AssetBundleHeader03 bundleHeader3;
			AssetBundleHeader06 bundleHeader6;
		};
		union {
			AssetsList *assetsLists3;
			AssetBundleBlockAndDirectoryList06 *bundleInf6;
		};

		ASSETSTOOLS_API AssetBundleFile();
		ASSETSTOOLS_API ~AssetBundleFile();
		ASSETSTOOLS_API void Close();
		//allowCompressed : Don't fail if there are compressed blocks, or if the directory list exceeds maxDirectoryLen.
		//maxDirectoryLen : If the file table is compressed, this specifies the maximum compressed and decompressed length for in-memory decompression.
		ASSETSTOOLS_API bool Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL, bool allowCompressed = false, uint32_t maxDirectoryLen = 16*1024*1024);
		ASSETSTOOLS_API bool IsCompressed();
		ASSETSTOOLS_API bool Write(IAssetsReader *pReader,
			IAssetsWriter *pWriter,
			class BundleReplacer **pReplacers, size_t replacerCount, 
			AssetsFileVerifyLogger errorLogger = NULL, ClassDatabaseFile *typeMeta = NULL);
		ASSETSTOOLS_API bool Unpack(IAssetsReader *pReader, IAssetsWriter *pWriter);
		//settings : array of ECompressionTypes values, or NULL for default settings; settings and fileTableCompression unused for version 3 bundles
		ASSETSTOOLS_API bool Pack(IAssetsReader *pReader, IAssetsWriter *pWriter, ECompressionTypes *settings = NULL, ECompressionTypes fileTableCompression = COMPRESS_LZ4);
		ASSETSTOOLS_API bool IsAssetsFile(IAssetsReader *pReader, AssetBundleDirectoryInfo06 *pEntry);
		ASSETSTOOLS_API bool IsAssetsFile(IAssetsReader *pReader, AssetBundleEntry *pEntry);
		ASSETSTOOLS_API bool IsAssetsFile(IAssetsReader *pReader, size_t entryIdx);
		inline const char *GetEntryName(size_t entryIdx)
		{
			if (bundleHeader6.fileVersion >= 6)
 				return (bundleInf6 == nullptr || bundleInf6->directoryCount <= entryIdx) ? nullptr : bundleInf6->dirInf[entryIdx].name;
			else if (bundleHeader6.fileVersion == 3)
 				return (assetsLists3 == nullptr || assetsLists3->count <= entryIdx) ? nullptr : assetsLists3->ppEntries[entryIdx]->name;
			return nullptr;
		}
		ASSETSTOOLS_API IAssetsReader *MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleDirectoryInfo06 *pEntry);
		ASSETSTOOLS_API IAssetsReader *MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleEntry *pEntry);
};
ASSETSTOOLS_API void FreeAssetBundle_FileReader(IAssetsReader *pReader);

#endif