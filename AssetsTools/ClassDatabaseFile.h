#pragma once
#include "AssetsFileFormat.h"
//#include "AssetTypeClass.h"
#include "defines.h"
#include <vector>

//custom file type to store Unity type information
#define ClassDatabaseFileVersion 4
#define ClassDatabaseCompressionType 1 //LZ4 compress by default
#define ClassDatabasePackageVersion 1

class ClassDatabaseFile;
struct ClassDatabaseFileString
{
	union {
		//Don't trust this offset! GetString makes sure no out-of-bounds offset is used.
		uint32_t stringTableOffset;
		const char *string;
	} str;
	bool fromStringTable = false;
	inline ClassDatabaseFileString() { str.string = nullptr; }
	ASSETSTOOLS_API const char *GetString(ClassDatabaseFile *pFile);
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
};
struct ClassDatabaseTypeField
{
	ClassDatabaseFileString typeName;
	ClassDatabaseFileString fieldName;
	uint8_t depth = 0;
	uint8_t isArray = 0;
	uint32_t size = 0;
	//uint32_t index;
	uint16_t version = 0;
	uint32_t flags2 = 0; //Flag 0x4000 : align to 4 bytes after this field.
	
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos, int version); //reads version 0,1,2,3
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int version); //writes version 1,2,3
};
class AssetTypeTemplateField;
class ClassDatabaseType
{
public:
	int classId = 0;
	int baseClass = 0;
	ClassDatabaseFileString name;
	ClassDatabaseFileString assemblyFileName; //set if (header.flags & 1)
	std::vector<ClassDatabaseTypeField> fields;
	//uint32_t fieldCount;
	//ClassDatabaseTypeField *fields;
	ASSETSTOOLS_API ClassDatabaseType();
	ASSETSTOOLS_API ClassDatabaseType(const ClassDatabaseType& other);
	ASSETSTOOLS_API ~ClassDatabaseType();
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos, int version, uint8_t flags);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int version, uint8_t flags);
	//Does NOT copy strings, so the ClassDatabaseType must not be used after the template field's strings are freed!
	ASSETSTOOLS_API bool FromTemplateField(int classId, int baseClass, AssetTypeTemplateField *pTemplateBase);

	ASSETSTOOLS_API Hash128 MakeTypeHash(ClassDatabaseFile *pDatabaseFile); 
};
ASSETSTOOLS_API Hash128 MakeScriptID(const char *scriptName, const char *scriptNamespace, const char *scriptAssembly);
struct ClassDatabaseFileHeader
{
	char header[4] = {};
	uint8_t fileVersion = 0;
	uint8_t flags = 0; //1 : Describes MonoBehaviour classes (contains assembly and full class names, base field is to be ignored)

	uint8_t compressionType = 0; //version 2; 0 = none, 1 = LZ4
	uint32_t compressedSize = 0, uncompressedSize = 0;  //version 2
	//uint8_t assetsVersionCount; //version 0 only
	//uint8_t *assetsVersions; //version 0 only

	uint8_t unityVersionCount = 0;
	char **pUnityVersions = nullptr;


	uint32_t stringTableLen = 0;
	uint32_t stringTablePos = 0;
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
	//uint32_t _tmp; //used only if assetsVersions == NULL; version 0 only
};
//all classes that override Component : prepend PPtr<GameObject> m_GameObject
//Transform : add vector m_Children {Array Array {int size; PPtr<Transform> data}}
class ClassDatabaseFile
{
	bool valid;
public:
	//Only for internal use, otherwise this could create a memory leak!
	bool dontFreeStringTable;
	ClassDatabaseFileHeader header;

	std::vector<ClassDatabaseType> classes;
	//uint32_t classCount;
	//ClassDatabaseType *classes;

	char *stringTable;

public:
	
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable=1, uint32_t compress=1, bool writeStringTable=true);
	ASSETSTOOLS_API bool IsValid();

	ASSETSTOOLS_API bool InsertFrom(ClassDatabaseFile *pOther, ClassDatabaseType *pType);
	ASSETSTOOLS_API void Clear();
	
	ASSETSTOOLS_API ClassDatabaseFile();
	ASSETSTOOLS_API ClassDatabaseFile &operator=(const ClassDatabaseFile& other);
	ASSETSTOOLS_API ClassDatabaseFile(const ClassDatabaseFile& other);
	ASSETSTOOLS_API ClassDatabaseFile(ClassDatabaseFile&& other);
	ASSETSTOOLS_API ~ClassDatabaseFile();
};
typedef ClassDatabaseFile* PClassDatabaseFile;

ASSETSTOOLS_API void FreeClassDatabase_Dummy(ClassDatabaseFile *pFile);

struct ClassDatabaseFileRef
{
	uint32_t offset;
	uint32_t length;
	char name[16];
};
struct ClassDatabasePackageHeader
{
	char magic[4] = {}; //"CLPK"
	uint8_t fileVersion = 0; //0 or 1
	uint8_t compressionType = 0; //Version 1 flags : 0x80 compressed all files in one block; 0x40 string table uncompressed; 0x20 file block uncompressed;
	uint32_t stringTableOffset = 0, stringTableLenUncompressed = 0, stringTableLenCompressed = 0;
	uint32_t fileBlockSize = 0;
	uint32_t fileCount = 0;
	std::vector<ClassDatabaseFileRef> files;
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
};

//contains multiple ClassDatabaseFiles and stores the string table of all in one block
class ClassDatabasePackage
{
	bool valid = false;
public:
	ClassDatabasePackageHeader header;
	ClassDatabaseFile **files = nullptr;
	char *stringTable = nullptr;

public:
	
	ASSETSTOOLS_API void Clear();
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable=1, uint32_t compress=1);
	ASSETSTOOLS_API bool RemoveFile(uint32_t index);
	ASSETSTOOLS_API bool ImportFile(IAssetsReader *pReader);
	ASSETSTOOLS_API bool IsValid();
	
	ASSETSTOOLS_API ~ClassDatabasePackage();
};