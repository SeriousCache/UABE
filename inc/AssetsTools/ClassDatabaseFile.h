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
		DWORD stringTableOffset;
		const char *string;
	} str;
	bool fromStringTable;
	ASSETSTOOLS_API const char *GetString(ClassDatabaseFile *pFile);
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
};
struct ClassDatabaseTypeField
{
	ClassDatabaseFileString typeName;
	ClassDatabaseFileString fieldName;
	BYTE depth;
	BYTE isArray;
	DWORD size;
	//DWORD index;
	WORD version;
	DWORD flags2; //Flag 0x4000 : align to 4 bytes after this field.
	
	ASSETSTOOLS_API ClassDatabaseTypeField();
	ASSETSTOOLS_API ClassDatabaseTypeField(const ClassDatabaseTypeField& other);
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos, int version); //reads version 0,1,2,3
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int version); //writes version 1,2,3
};
class AssetTypeTemplateField;
class ClassDatabaseType
{
public:
	int classId;
	int baseClass;
	ClassDatabaseFileString name;
	ClassDatabaseFileString assemblyFileName; //set if (header.flags & 1)
	std::vector<ClassDatabaseTypeField> fields;
	//DWORD fieldCount;
	//ClassDatabaseTypeField *fields;
	ASSETSTOOLS_API ClassDatabaseType();
	ASSETSTOOLS_API ClassDatabaseType(const ClassDatabaseType& other);
	ASSETSTOOLS_API ~ClassDatabaseType();
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos, int version, BYTE flags);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int version, BYTE flags);
	//Does NOT copy strings, so the ClassDatabaseType must not be used after the template field's strings are freed!
	ASSETSTOOLS_API bool FromTemplateField(int classId, int baseClass, AssetTypeTemplateField *pTemplateBase);

	ASSETSTOOLS_API Hash128 MakeTypeHash(ClassDatabaseFile *pDatabaseFile); 
};
ASSETSTOOLS_API Hash128 MakeScriptID(const char *scriptName, const char *scriptNamespace, const char *scriptAssembly);
struct ClassDatabaseFileHeader
{
	char header[4];
	BYTE fileVersion;
	BYTE flags; //1 : Describes MonoBehaviour classes (contains assembly and full class names, base field is to be ignored)

	BYTE compressionType; //version 2; 0 = none, 1 = LZ4
	DWORD compressedSize, uncompressedSize;  //version 2
	//BYTE assetsVersionCount; //version 0 only
	//BYTE *assetsVersions; //version 0 only

	BYTE unityVersionCount;
	char **pUnityVersions;


	DWORD stringTableLen;
	DWORD stringTablePos;
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
	//DWORD _tmp; //used only if assetsVersions == NULL; version 0 only
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
	//DWORD classCount;
	//ClassDatabaseType *classes;

	char *stringTable;

public:
	
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable=1, DWORD compress=1, bool writeStringTable=true);
	ASSETSTOOLS_API bool IsValid();

	ASSETSTOOLS_API bool InsertFrom(ClassDatabaseFile *pOther, ClassDatabaseType *pType);
	ASSETSTOOLS_API void Clear();
	
	ASSETSTOOLS_API ClassDatabaseFile();
	ASSETSTOOLS_API ClassDatabaseFile(const ClassDatabaseFile& other);
	ASSETSTOOLS_API ~ClassDatabaseFile();
};
typedef ClassDatabaseFile* PClassDatabaseFile;



struct ClassDatabaseFileRef
{
	DWORD offset;
	DWORD length;
	char name[16];
};
struct ClassDatabasePackageHeader
{
	char magic[4]; //"CLPK"
	BYTE fileVersion; //0 or 1
	BYTE compressionType; //Version 1 flags : 0x80 compressed all files in one block; 0x40 string table uncompressed; 0x20 file block uncompressed;
	DWORD stringTableOffset, stringTableLenUncompressed, stringTableLenCompressed;
	DWORD fileBlockSize;
	DWORD fileCount;
	std::vector<ClassDatabaseFileRef> files;
	ASSETSTOOLS_API QWORD Read(IAssetsReader *pReader, QWORD filePos);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos);
};

//contains multiple ClassDatabaseFiles and stores the string table of all in one block
class ClassDatabasePackage
{
	bool valid;
public:
	ClassDatabasePackageHeader header;
	ClassDatabaseFile **files;
	char *stringTable;

public:
	
	ASSETSTOOLS_API void Clear();
	ASSETSTOOLS_API bool Read(IAssetsReader *pReader);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable=1, DWORD compress=1);
	ASSETSTOOLS_API bool RemoveFile(DWORD index);
	ASSETSTOOLS_API bool ImportFile(IAssetsReader *pReader);
	ASSETSTOOLS_API bool IsValid();
	
	ASSETSTOOLS_API ClassDatabasePackage();
	ASSETSTOOLS_API ~ClassDatabasePackage();
};