#pragma once
#include <vector>
#include <cstdint>
#include "defines.h"
#include "AssetsFileReader.h"

template<class T> inline T SwapEndians(T old)
{
	T ret; size_t sizeof_T = sizeof(T);
	for (size_t i = 0; i < sizeof_T; i++)
		((uint8_t*)&ret)[sizeof_T - i - 1] = ((uint8_t*)&old)[i];
	return ret;
}
template<class T> inline void SwapEndians_(T& _old)
{
	T old = _old;
	T ret; size_t sizeof_T = sizeof(T);
	for (size_t i = 0; i < sizeof_T; i++)
		((uint8_t*)&ret)[sizeof_T - i - 1] = ((uint8_t*)&old)[i];
	_old = ret;
}
ASSETSTOOLS_API uint32_t SwapEndians(uint32_t old);
ASSETSTOOLS_API void SwapEndians_(uint32_t& old);

#define AssetFileInfo_MaxSize 25
class AssetFileInfo //little-endian or big-endian (=> header.endianness)
{
public:
	uint64_t index;					//0x00 //version < 0x0E : only uint32_t
	uint64_t offs_curFile;			//0x08 //version < 0x16 : only uint32_t
	uint32_t curFileSize;				//0x0C
	uint32_t curFileTypeOrIndex;		//0x10 //starting with version 0x10, this is an index into the type tree
	//inheritedUnityClass : for Unity classes, this is curFileType; for MonoBehaviours, this is 114
	//version < 0x0B : inheritedUnityClass is uint32_t, no scriptIndex exists
	uint16_t inheritedUnityClass;		//0x14 //(MonoScript) //only version < 0x10
	//scriptIndex : for Unity classes, this is 0xFFFF; 
	//for MonoBehaviours, this is an index of the mono class, counted separately for each .assets file
	uint16_t scriptIndex;				//0x16 //only version <= 0x10
	uint8_t unknown1;					//0x18 //only 0x0F <= version <= 0x10 //with alignment always a uint32_t
	ASSETSTOOLS_API static uint32_t GetSize(uint32_t version);
	ASSETSTOOLS_API QWORD Read(uint32_t version, QWORD pos, IAssetsReader* pReader, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(uint32_t version, QWORD pos, IAssetsWriter* pWriter, bool bigEndian);
};

struct AssetFileList //little-endian or big-endian (=> header.endianness)
{
	unsigned int sizeFiles;			//0x00
	AssetFileInfo fileInfs[1];		//0x04

	ASSETSTOOLS_API unsigned int GetSizeBytes(uint32_t version);
	ASSETSTOOLS_API QWORD Read(uint32_t version, QWORD pos, IAssetsReader* pReader, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(uint32_t version, QWORD pos, IAssetsWriter* pWriter, bool bigEndian);
};
struct AssetsFileHeader //Always big-endian
{
	uint64_t unknown00;				//0x00 //format >= 0x16 only. Always 0?
	uint32_t format;					//0x08
	uint64_t metadataSize;			//0x10 //format < 0x16: uint32_t @ 0x00;
	uint64_t fileSize;				//0x18 //format < 0x16: uint32_t @ 0x04;
	uint64_t offs_firstFile;		//0x20 //format < 0x16: uint32_t @ 0x0C;
	//0 == little-endian; 1 == big-endian
	uint8_t endianness;				//0x20, for format < 0x16 @ 0x10, for format < 9 at (fileSize - metadataSize) right before TypeTree
	uint8_t unknown[3];				//0x21, for format < 0x16 @ 0x11, exists for format >= 9

	ASSETSTOOLS_API unsigned int GetSizeBytes();
	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader);
	//does NOT write the endianness byte for format < 9!
	ASSETSTOOLS_API QWORD Write(QWORD pos, IAssetsWriter* pWriter);
};
struct AssetsFileDependency
{
	//version < 6 : no bufferedPath
	//version < 5 : no bufferedPath, guid, type
	char bufferedPath[256]; //for buffered (type=1)
	struct GUID128
	{
		__int64 mostSignificant; //64-127 //big-endian
		__int64 leastSignificant; //0-63  //big-endian
		ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader);
		ASSETSTOOLS_API QWORD Write(QWORD absFilePos, IAssetsWriter* pWriter);
	} guid;
	int type;
	char assetPath[256]; //path to the .assets file
	inline AssetsFileDependency()
		: type(0), guid{}
	{
		bufferedPath[0] = 0;
		assetPath[0] = 0;
	}
	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader, uint32_t format, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(QWORD absFilePos, IAssetsWriter* pWriter, uint32_t format, bool bigEndian);
};
struct AssetsFileDependencyList
{
	uint32_t dependencyCount;
	//uint8_t unknown; //seemingly always 0
	struct AssetsFileDependency* pDependencies;
	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader, uint32_t format, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(QWORD absFilePos, IAssetsWriter* pWriter, uint32_t format, bool bigEndian);
};

struct TypeField_0D
{
	uint16_t version;			//0x00
	uint8_t depth;				//0x02 //specifies the amount of parents
	//0x01 : IsArray
	//0x02 : IsRef
	//0x04 : IsRegistry
	//0x08 : IsArrayOfRefs
	uint8_t isArray;			//0x03 //actually a bool for format <= 0x12, uint8_t since format 0x13
	uint32_t typeStringOffset;	//0x04
	uint32_t nameStringOffset;	//0x08
	uint32_t size;				//0x0C //size in bytes; if not static (if it contains an array), set to -1
	uint32_t index;			//0x10
	//0x0001 : is invisible(?), set for m_FileID and m_PathID; ignored if no parent field exists or the type is neither ColorRGBA, PPtr nor string
	//0x0100 : ? is bool
	//0x1000 : ?
	//0x4000 : align bytes
	//0x8000 : any child has the align bytes flag
	//=> if flags & 0xC000 and size != 0xFFFFFFFF, the size field matches the total length of this field plus its children.
	//0x400000 : ?
	//0x800000 : ? is non-primitive type
	//0x02000000 : ? is UInt16 (called char)
	//0x08000000 : has fixed buffer size? related to Array (i.e. this field or its only child or its father is an array), should be set for vector, Array and the size and data fields.
	uint32_t flags;			//0x14 
	uint8_t unknown1[8];		//0x18 //since format 0x12

	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader, uint32_t format, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(QWORD curFilePos, IAssetsWriter* pWriter, uint32_t format, bool bigEndian);
	ASSETSTOOLS_API const char* GetTypeString(const char* stringTable, size_t stringTableLen);
	ASSETSTOOLS_API const char* GetNameString(const char* stringTable, size_t stringTableLen);
};//0x18

struct Type_0D //everything big endian
{
	//Starting with U5.5, all MonoBehaviour types have MonoBehaviour's classId (114)
	//Before, the different MonoBehaviours had different negative classIds, starting with -1
	int classId; //0x00

	uint8_t unknown16_1; //format >= 0x10
	uint16_t scriptIndex; //format >= 0x11 U5.5+, index to the MonoManager (usually 0xFFFF)

	//Script ID (md4 hash)
	Hash128 scriptIDHash;  //if classId < 0 //0x04..0x13

	//Type hash / properties hash (md4)
	Hash128 typeHash;  //0x04..0x13 or 0x14..0x23

	uint32_t typeFieldsExCount; //if (TypeTree.enabled) //0x14 or 0x24
	TypeField_0D* pTypeFieldsEx;

	uint32_t stringTableLen; //if (TypeTree.enabled) //0x18 or 0x28
	char* pStringTable;

	//For types from assetsFile.pSecondaryTypeList :
	uint32_t depListLen; //format >= 0x15
	unsigned int* pDepList; //format >= 0x15

	//For types in assetsFile.typeTree :
	char* header1; //format >= 0x15
	char* header2; //format >= 0x15
	char* header3; //format >= 0x15

	ASSETSTOOLS_API QWORD Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader* pReader, uint32_t version, bool bigEndian, bool secondaryTypeTree = false);
	ASSETSTOOLS_API QWORD Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter* pWriter, uint32_t version, bool bigEndian, bool secondaryTypeTree = false);
};

struct TypeField_07 //everything big endian
{
	char type[256] = {0}; //null-terminated
	char name[256] = {0}; //null-terminated
	uint32_t size = 0;
	uint32_t index = 0;
	uint32_t arrayFlag = 0;
	uint32_t flags1 = 0;
	uint32_t flags2 = 0; //Flag 0x4000 : align to 4 bytes after this field.
	uint32_t childrenCount = 0;
	TypeField_07* children = nullptr;

	ASSETSTOOLS_API QWORD Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader* pReader, uint32_t version, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter* pWriter, bool bigEndian);
};
struct Type_07
{
	int classId; //big endian
	TypeField_07 base;

	ASSETSTOOLS_API QWORD Read(bool hasTypeTree, QWORD absFilePos, IAssetsReader* pReader, uint32_t version, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(bool hasTypeTree, QWORD absFilePos, IAssetsWriter* pWriter, bool bigEndian);
};
struct TypeTree
{
	//The actual 4-byte-alignment base starts here. Using the header as the base still works since its length is 20.
	char unityVersion[64] = {0}; //null-terminated; stored for .assets format > 6
	uint32_t platform = 0; //big endian; stored for .assets format > 6
	bool hasTypeTree = false; //stored for .assets format >= 13; Unity 5 only stores some metadata if it's set to false
	uint32_t fieldCount = 0; //big endian;

	union
	{
		Type_0D* pTypes_Unity5;
		Type_07* pTypes_Unity4;
	};

	uint32_t dwUnknown; //actually belongs to the asset list; stored for .assets format < 14
	uint32_t _fmt; //not stored here in the .assets file, the variable is just to remember the .assets file version

	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader, uint32_t version, bool bigEndian); //Minimum AssetsFile format : 6
	ASSETSTOOLS_API QWORD Write(QWORD absFilePos, IAssetsWriter* pWriter, uint32_t version, bool bigEndian);

	ASSETSTOOLS_API void Clear();
};

struct AssetPPtr
{
	uint32_t fileID;
	QWORD pathID;
};
struct PreloadList
{
	uint32_t len;
	AssetPPtr* items;

	ASSETSTOOLS_API QWORD Read(QWORD absFilePos, IAssetsReader* pReader, uint32_t format, bool bigEndian);
	ASSETSTOOLS_API QWORD Write(QWORD absFilePos, IAssetsWriter* pWriter, uint32_t format, bool bigEndian);
};
