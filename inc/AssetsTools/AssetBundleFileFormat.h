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
	DWORD flags; //(flags & 4) : has serialized data
	char *name;
	ASSETSTOOLS_API QWORD GetAbsolutePos(AssetBundleHeader06 *pHeader);
	ASSETSTOOLS_API QWORD GetAbsolutePos(class AssetBundleFile *pFile);
};
struct AssetBundleBlockInfo06
{
	DWORD decompressedSize;
	DWORD compressedSize;
	WORD flags; //(flags & 0x40) : is streamed (read in 64KiB blocks); (flags & 0x3F) :  compression info;
	inline BYTE GetCompressionType() { return (BYTE)(flags & 0x3F); }
	//example flags (LZMA, streamed) : 0x41
	//example flags (LZ4, not streamed) : 0x03
};
struct AssetBundleBlockAndDirectoryList06
{
	QWORD checksumLow;
	QWORD checksumHigh;
	DWORD blockCount;
	AssetBundleBlockInfo06 *blockInf;
	DWORD directoryCount;
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
	DWORD fileVersion; //big-endian, = 6
	char minPlayerVersion[24]; //0-terminated; 5.x.x
	char fileEngineVersion[64]; //0-terminated; exact unity engine version
	QWORD totalFileSize;
	//sizes for the blocks info :
	DWORD compressedSize;
	DWORD decompressedSize;
	//(flags & 0x3F) is the compression mode (0 = none; 1 = LZMA; 2-3 = LZ4)
	//(flags & 0x40) says whether the bundle has directory info
	//(flags & 0x80) says whether the block and directory list is at the end
	DWORD flags;
	
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
				return (ret + 0x0A);
			else
				return (ret + strlen(signature) + 1);
		}
	}
	inline DWORD GetFileDataOffset()
	{
		DWORD ret = 0;
		if (!strcmp(this->signature, "UnityArchive"))
			return this->compressedSize;
		else if (!strcmp(this->signature, "UnityFS") || !strcmp(this->signature, "UnityWeb"))
		{
			ret = (DWORD)strlen(minPlayerVersion) + (DWORD)strlen(fileEngineVersion) + 0x1A;
			if (this->flags & 0x100)
				ret += 0x0A;
			else
				ret += (DWORD)strlen(signature) + 1;
		}
		if (!(this->flags & 0x80))
			ret += this->compressedSize;
		return ret;
	}
};

struct AssetBundleHeader03
{
	char signature[13]; //0-terminated; UnityWeb or UnityRaw
	DWORD fileVersion; //big-endian; 3 : Unity 3.5 and 4;
	char minPlayerVersion[24]; //0-terminated; 3.x.x -> Unity 3.x.x/4.x.x; 5.x.x
	char fileEngineVersion[64]; //0-terminated; exact unity engine version
	DWORD minimumStreamedBytes; //big-endian; not always the file's size
	DWORD bundleDataOffs; //big-endian;
	DWORD numberOfAssetsToDownload; //big-endian;
	DWORD blockCount; //big-endian;
	struct AssetBundleOffsetPair *pBlockList;
	DWORD fileSize2; //big-endian; for fileVersion >= 2
	DWORD unknown2; //big-endian; for fileVersion >= 3
	BYTE unknown3;

	ASSETSTOOLS_API bool Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL);
	ASSETSTOOLS_API bool Write(IAssetsWriter *pWriter, QWORD &curFilePos, AssetsFileVerifyLogger errorLogger = NULL);
};

struct AssetBundleEntry
{
	DWORD offset;
	DWORD length;
	char name[1];
	ASSETSTOOLS_API unsigned int GetAbsolutePos(AssetBundleHeader03 *pHeader);//, DWORD listIndex);
	ASSETSTOOLS_API unsigned int GetAbsolutePos(class AssetBundleFile *pFile);//, DWORD listIndex);
};
struct AssetsList
{
	DWORD pos;
	DWORD count;
	AssetBundleEntry **ppEntries;
	DWORD allocatedCount;
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
	DWORD compressed;
	DWORD uncompressed;
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
		ASSETSTOOLS_API bool Read(IAssetsReader *pReader, AssetsFileVerifyLogger errorLogger = NULL, bool allowCompressed = false);
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
		ASSETSTOOLS_API IAssetsReader *MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleDirectoryInfo06 *pEntry);
		ASSETSTOOLS_API IAssetsReader *MakeAssetsFileReader(IAssetsReader *pReader, AssetBundleEntry *pEntry);
};
ASSETSTOOLS_API void FreeAssetBundle_FileReader(IAssetsReader *pReader);

//Deprecated names.
#define AssetsBundleDirectoryInfo06 AssetBundleDirectoryInfo06
#define AssetsBundleBlockInfo06 AssetBundleBlockInfo06
#define AssetsBundleBlockAndDirectoryList06 AssetBundleBlockAndDirectoryList06
#define AssetsBundleHeader06 AssetBundleHeader06
#define AssetsBundleHeader03 AssetBundleHeader03
#define AssetsBundleEntry AssetBundleEntry
#define AssetsBundleOffsetPair AssetBundleOffsetPair
#define AssetsBundleFilePar AssetBundleFilePar
#define AssetsBundle_AssetsFileReader AssetBundle_AssetsFileReader
#define AssetsBundleFile AssetBundleFile
#define FreeAssetsBundle_FileReader FreeAssetBundle_FileReader

#endif