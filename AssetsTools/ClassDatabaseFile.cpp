#include "stdafx.h"
#include "../AssetsTools/ClassDatabaseFile.h"
#include "../AssetsTools/AssetsFileReader.h"
#include "../AssetsTools/AssetTypeClass.h"
#include "../libCompression/lz4.h"
#include "..\inc\LZMA\LzmaLib.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WinCrypt.h>
#pragma comment(lib, "Advapi32.lib")

ASSETSTOOLS_API const char *ClassDatabaseFileString::GetString(ClassDatabaseFile *pFile)
{
	if (!fromStringTable)
		return str.string;
	if (str.stringTableOffset >= pFile->header.stringTableLen)
		return "";
	return &pFile->stringTable[str.stringTableOffset];
}
ASSETSTOOLS_API QWORD ClassDatabaseFileString::Read(IAssetsReader *pReader, QWORD filePos)
{
	fromStringTable = true;
	pReader->Read(filePos, 4, &str.stringTableOffset);
	return (filePos+4);
}
ASSETSTOOLS_API QWORD ClassDatabaseFileString::Write(IAssetsWriter *pWriter, QWORD filePos)
{
	pWriter->Write(filePos, 4, &str.stringTableOffset);
	return (filePos+4);
}

ASSETSTOOLS_API QWORD ClassDatabaseTypeField::Read(IAssetsReader *pReader, QWORD filePos, int version)
{
	filePos = typeName.Read(pReader, filePos);
	filePos = fieldName.Read(pReader, filePos);
	pReader->Read(filePos, 1, &depth); filePos++;
	pReader->Read(filePos, 1, &isArray); filePos++;
	pReader->Read(filePos, 4, &size); filePos+=4;
	this->version = 1;
	if (version < 1)
	{
		uint32_t index; 
		pReader->Read(filePos, 4, &index); filePos+=4;
		if (index & 0x80000000)
		{
			pReader->Read(filePos, 2, &this->version); filePos+=2;
		}
	}
	else if (version >= 3)
	{
		pReader->Read(filePos, 2, &this->version); filePos+=2;
	}
	pReader->Read(filePos, 4, &flags2); filePos+=4;
	return filePos;
}
ASSETSTOOLS_API QWORD ClassDatabaseTypeField::Write(IAssetsWriter *pWriter, QWORD filePos, int version)
{
	filePos = typeName.Write(pWriter, filePos);
	filePos = fieldName.Write(pWriter, filePos);
	pWriter->Write(filePos, 1, &depth); filePos++;
	pWriter->Write(filePos, 1, &isArray); filePos++;
	pWriter->Write(filePos, 4, &size); filePos+=4;
	//pWriter->Write(filePos, 4, &index); filePos+=4;
	if (version >= 3)
	{
		pWriter->Write(filePos, 2, &this->version); filePos+=2;
	}
	pWriter->Write(filePos, 4, &flags2); filePos+=4;
	return filePos;
}

ASSETSTOOLS_API ClassDatabaseType::ClassDatabaseType()
{
	baseClass = -1;
	classId = -1;
	//ZeroMemory(&name, sizeof(ClassDatabaseFileString));
	this->fields = std::vector<ClassDatabaseTypeField>();
	this->fields.reserve(1);
}
ASSETSTOOLS_API ClassDatabaseType::ClassDatabaseType(const ClassDatabaseType& other)
{
	baseClass = other.baseClass;
	classId = other.classId;
	memcpy(&name, &other.name, sizeof(ClassDatabaseFileString));
	memcpy(&assemblyFileName, &other.assemblyFileName, sizeof(ClassDatabaseFileString));
	fields.reserve(other.fields.size());
	for (size_t i = 0; i < other.fields.size(); i++)
		fields.push_back(ClassDatabaseTypeField(other.fields[i]));
}
class AssetTypeTemplateField;
int _RecursiveAddTemplateFieldToClassDatabase(std::vector<ClassDatabaseTypeField> &list, uint8_t depth, AssetTypeTemplateField *pTemplate)
{
	list.resize(list.size() + 1);
	ClassDatabaseTypeField &ownField = list[list.size() - 1];
	ownField.fieldName.fromStringTable = false;
	ownField.fieldName.str.string = pTemplate->name.c_str();
	ownField.typeName.fromStringTable = false;
	ownField.typeName.str.string = pTemplate->type.c_str();
	ownField.depth = depth;
	ownField.isArray = pTemplate->isArray;
	ownField.size = 0;
	if (pTemplate->isArray & 1)
		ownField.size = -1;
	else
	{
		switch (GetValueTypeByTypeName(pTemplate->type.c_str()))
		{
			case ValueType_Bool:
			case ValueType_Int8:
			case ValueType_UInt8:
				ownField.size = 1;
				break;
			case ValueType_Int16:
			case ValueType_UInt16:
				ownField.size = 2;
				break;
			case ValueType_Int32:
			case ValueType_UInt32:
			case ValueType_Float:
				ownField.size = 4;
				break;
			case ValueType_Int64:
			case ValueType_UInt64:
			case ValueType_Double:
				ownField.size = 8;
				break;
			default:
				ownField.size = 0;
		}
	}
	ownField.version = 1;
	ownField.flags2 = pTemplate->align ? 0x4000 : 0;
	if (depth < 255)
	{
		for (uint32_t i = 0; i < pTemplate->children.size(); i++)
		{
			int curSize = _RecursiveAddTemplateFieldToClassDatabase(list, depth + 1, &pTemplate->children[i]);
			if (!(ownField.isArray & 1) && (ownField.size != -1))
			{
				if (curSize == -1)
					ownField.size = -1;
				else
					ownField.size += curSize;
			}
		}
	}
	return ownField.size;
}
ASSETSTOOLS_API bool ClassDatabaseType::FromTemplateField(int classId, int baseClass, AssetTypeTemplateField *pTemplateBase)
{
	if (!pTemplateBase) return false;
	this->classId = classId;
	this->baseClass = baseClass;
	this->name.fromStringTable = false;
	this->name.str.string = pTemplateBase->type.c_str();
	this->assemblyFileName.fromStringTable = false;
	this->assemblyFileName.str.string = "";
	this->fields.clear();
	_RecursiveAddTemplateFieldToClassDatabase(this->fields, 0, pTemplateBase);
	return true;
}
ASSETSTOOLS_API ClassDatabaseType::~ClassDatabaseType()
{
	/*if (freeStrings)
	{
		if (!name.fromStringTable && name.str.string != NULL)
		{
			free(const_cast<char*>(name.str.string));
			name.str.string = NULL;
		}
		if (!assemblyFileName.fromStringTable && assemblyFileName.str.string != NULL)
		{
			free(const_cast<char*>(assemblyFileName.str.string));
			assemblyFileName.str.string = NULL;
		}
		for (size_t i = 0; i < fields.size(); i++)
		{
			ClassDatabaseTypeField &field = fields[i];
			if (!field.fieldName.fromStringTable && field.fieldName.str.string != NULL)
			{
				free(const_cast<char*>(field.fieldName.str.string));
				field.fieldName.str.string = NULL;
			}
			if (!field.typeName.fromStringTable && field.typeName.str.string != NULL)
			{
				free(const_cast<char*>(field.typeName.str.string));
				field.typeName.str.string = NULL;
			}
		}
	}*/
	//if (this->fields.size() > 0)
	//	this->fields.clear();
}
ASSETSTOOLS_API QWORD ClassDatabaseType::Read(IAssetsReader *pReader, QWORD filePos, int version, uint8_t flags)
{
	uint32_t fieldCount;
	pReader->Read(filePos, 4, &classId); filePos+=4;
	pReader->Read(filePos, 4, &baseClass); filePos+=4;
	filePos = name.Read(pReader, filePos);
	if (flags & 1)
		filePos = assemblyFileName.Read(pReader, filePos);
	else
	{
		assemblyFileName.str.string = NULL;
		assemblyFileName.fromStringTable = false;
	}
	pReader->Read(filePos, 4, &fieldCount); filePos+=4;

	/*fields = (ClassDatabaseTypeField*)malloc(sizeof(ClassDatabaseTypeField) * fieldCount);
	if (fields == NULL)
	{
		for (uint32_t i = 0; i < fieldCount; i++)
			filePos = ClassDatabaseTypeField().Read(reader, readerPar, filePos);
		fieldCount = 0;
	}
	for (uint32_t i = 0; i < fieldCount; i++)
	{
		filePos = fields[i].Read(reader, readerPar, filePos);
	}*/
	fields.reserve(fieldCount);
	for (uint32_t i = 0; i < fieldCount; i++)
	{
		ClassDatabaseTypeField field;
		filePos = field.Read(pReader, filePos, version);
		fields.push_back(field);
	}
	return filePos;
}
ASSETSTOOLS_API QWORD ClassDatabaseType::Write(IAssetsWriter *pWriter, QWORD filePos, int version, uint8_t flags)
{
	uint32_t fieldCount = (uint32_t)fields.size();

	pWriter->Write(filePos, 4, &classId); filePos+=4;
	pWriter->Write(filePos, 4, &baseClass); filePos+=4;
	filePos = name.Write(pWriter, filePos);
	if (flags & 1)
		filePos = assemblyFileName.Write(pWriter, filePos);
	pWriter->Write(filePos, 4, &fieldCount); filePos+=4;

	for (uint32_t i = 0; i < fieldCount; i++)
	{
		filePos = fields[i].Write(pWriter, filePos, version);
	}
	return filePos;
}
ASSETSTOOLS_API Hash128 ClassDatabaseType::MakeTypeHash(ClassDatabaseFile *pDatabaseFile)
{
	Hash128 ret = {};
	HCRYPTPROV hContext;
	if (!CryptAcquireContext(&hContext, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return ret;
	HCRYPTHASH hHash;
	if (!CryptCreateHash(hContext, CALG_MD4, 0, 0, &hHash))
	{
		CryptReleaseContext(hContext, 0);
		return ret;
	}
	for (size_t k = 0; k < fields.size(); k++)
	{
		const char *typeName = fields[k].typeName.GetString(pDatabaseFile);
		const char *fieldName = fields[k].fieldName.GetString(pDatabaseFile);
		uint32_t size = fields[k].size;
		uint32_t isArray = fields[k].isArray;
		uint32_t version = fields[k].version;
		uint32_t flag = fields[k].flags2 & 0x4000;
		CryptHashData(hHash, (const uint8_t*)typeName, strlen(typeName), 0);
		CryptHashData(hHash, (const uint8_t*)fieldName, strlen(fieldName), 0);
		CryptHashData(hHash, (const uint8_t*)&size, 4, 0);
		CryptHashData(hHash, (const uint8_t*)&isArray, 4, 0);
		CryptHashData(hHash, (const uint8_t*)&version, 4, 0);
		CryptHashData(hHash, (const uint8_t*)&flag, 4, 0);
	}
	DWORD len = 16;
	CryptGetHashParam(hHash, HP_HASHVAL, ret.bValue, &len, 0); //if it returns FALSE, it still needs to be freed
	CryptDestroyHash(hHash);
	CryptReleaseContext(hContext, 0);
	return ret;
}

ASSETSTOOLS_API Hash128 MakeScriptID(const char *scriptName, const char *scriptNamespace, const char *scriptAssembly)
{
	Hash128 ret = {};
	HCRYPTPROV hContext;
	if (!CryptAcquireContext(&hContext, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return ret;
	HCRYPTHASH hHash;
	if (!CryptCreateHash(hContext, CALG_MD4, 0, 0, &hHash))
	{
		CryptReleaseContext(hContext, 0);
		return ret;
	}
	CryptHashData(hHash, (const uint8_t*)scriptName, strlen(scriptName), 0);
	CryptHashData(hHash, (const uint8_t*)scriptNamespace, strlen(scriptNamespace), 0);
	CryptHashData(hHash, (const uint8_t*)scriptAssembly, strlen(scriptAssembly), 0);
	DWORD len = 16;
	CryptGetHashParam(hHash, HP_HASHVAL, ret.bValue, &len, 0); //if it returns FALSE, it still needs to be freed
	CryptDestroyHash(hHash);
	CryptReleaseContext(hContext, 0);
	return ret;
}

ASSETSTOOLS_API QWORD ClassDatabaseFileHeader::Read(IAssetsReader *pReader, QWORD filePos)
{
	if (!pReader->Read(filePos, 4, header))
		return 0;
	if (memcmp(header, "cldb", 4))
		return 0;
	filePos += 4;
	pReader->Read(filePos, 1, &fileVersion); filePos++;
	if (fileVersion >= 4)
	{
		pReader->Read(filePos, 1, &flags); filePos++;
	}
	else
		flags = 0;
	if (fileVersion >= 2)
	{
		pReader->Read(filePos, 1, &compressionType); filePos++;
		pReader->Read(filePos, 4, &compressedSize); filePos+=4;
		pReader->Read(filePos, 4, &uncompressedSize); filePos+=4;
	}
	else
	{
		compressionType = 0;
		compressedSize = uncompressedSize = 0;
	}
	if (fileVersion == 0)
	{
		uint8_t assetsVersionCount;
		pReader->Read(filePos, 1, &assetsVersionCount); filePos++;
		filePos += assetsVersionCount;
	}
	else
	{
		pReader->Read(filePos, 1, &unityVersionCount); filePos++;
		QWORD firstVersionPos = filePos;
		size_t bufferLen = unityVersionCount * sizeof(char*);
		for (uint32_t i = 0; i < unityVersionCount; i++)
		{
			uint8_t byTmp;
			pReader->Read(filePos, 1, &byTmp); filePos++;
			filePos += byTmp;
			bufferLen += (byTmp+1); //plus null-terminator
		}
		QWORD postVersionPos = filePos;
		pUnityVersions = (char**)malloc(bufferLen);
		if (pUnityVersions != NULL)
		{
			size_t bufferPos = unityVersionCount * sizeof(char*);
			filePos = firstVersionPos;
			for (uint32_t i = 0; i < unityVersionCount; i++)
			{
				char *charBuffer = &((char*)pUnityVersions)[bufferPos];
				pUnityVersions[i] = charBuffer;
				uint8_t byTmp;
				pReader->Read(filePos, 1, &byTmp); filePos++;
				pReader->Read(filePos, byTmp, charBuffer);
				charBuffer[byTmp] = 0;
				filePos += byTmp;
				bufferPos += (byTmp+1); //plus null-terminator
			}
		}
		else
			unityVersionCount = 0;

	}
	pReader->Read(filePos, 4, &stringTableLen); filePos+=4;
	pReader->Read(filePos, 4, &stringTablePos); filePos+=4;
	return filePos;
}
ASSETSTOOLS_API QWORD ClassDatabaseFileHeader::Write(IAssetsWriter *pWriter, QWORD filePos)
{
	pWriter->Write(filePos, 4, "cldb"); filePos += 4;
	pWriter->Write(filePos, 1, &fileVersion); filePos++;
	if (fileVersion >= 4)
	{
		pWriter->Write(filePos, 1, &flags); filePos++;
	}
	if (fileVersion >= 2)
	{
		pWriter->Write(filePos, 1, &compressionType); filePos++;
		pWriter->Write(filePos, 4, &compressedSize); filePos+=4;
		pWriter->Write(filePos, 4, &uncompressedSize); filePos+=4;
	}
	pWriter->Write(filePos, 1, &unityVersionCount); filePos++;
	for (uint32_t i = 0; i < unityVersionCount; i++)
	{
		uint8_t byTmp = (uint8_t)strlen(pUnityVersions[i]);
		pWriter->Write(filePos, 1, &byTmp); filePos++;
		pWriter->Write(filePos, byTmp, pUnityVersions[i]); filePos+=byTmp;
	}
	pWriter->Write(filePos, 4, &stringTableLen); filePos+=4;
	pWriter->Write(filePos, 4, &stringTablePos); filePos+=4;
	return filePos;
}

ASSETSTOOLS_API QWORD ClassDatabaseFile::Read(IAssetsReader *pReader, QWORD filePos)
{
	valid = false;
	this->dontFreeStringTable = false;
	char *uncompressedBuf = NULL;
	QWORD _filePos = header.Read(pReader, filePos);
	if (_filePos == filePos)
		return filePos;
	filePos = _filePos;
	QWORD postHeaderPos = filePos;
	QWORD compressedFilePos = filePos;
	
	if (header.compressionType && header.compressionType < 3)
	{
		char *compressedBuf = (char*)malloc(header.compressedSize);
		if (!compressedBuf)
			return postHeaderPos + header.compressedSize;
		if (pReader->Read(filePos, header.compressedSize, compressedBuf) != header.compressedSize)
		{
			free(compressedBuf);
			return postHeaderPos + header.compressedSize;
		}
		compressedFilePos = filePos + header.compressedSize;
		//add 1 to see if it decompresses more bytes than the header says there are
		uncompressedBuf = (char*)malloc(header.uncompressedSize + 1);
		if (!uncompressedBuf)
		{
			free(compressedBuf);
			return postHeaderPos + header.compressedSize;
		}
		uint32_t uncompressedSize = 0;
		if (header.compressionType == 1)
		{
#ifdef _WITHOUT_LZ4
			free(compressedBuf);
			return postHeaderPos + header.compressedSize;
#else
			uncompressedSize = (uint32_t)LZ4_decompress_safe(compressedBuf, uncompressedBuf, header.compressedSize, header.uncompressedSize + 1);
#endif
		}
		else if (header.compressionType == 2 && header.compressedSize > LZMA_PROPS_SIZE)
		{
			size_t lz_uncompressedSize = header.uncompressedSize;
			size_t compressedWithoutProps = header.compressedSize - LZMA_PROPS_SIZE;
			int result = LzmaUncompress((Byte*)uncompressedBuf, &lz_uncompressedSize,
				(Byte*)&compressedBuf[LZMA_PROPS_SIZE], &compressedWithoutProps,
				(Byte*)compressedBuf, LZMA_PROPS_SIZE);
			if (result == SZ_OK)
				uncompressedSize = (uint32_t)lz_uncompressedSize;
		}
		free(compressedBuf);
		if (uncompressedSize != header.uncompressedSize)
		{
			free(uncompressedBuf);
			return postHeaderPos + header.compressedSize;
		}
		pReader = Create_AssetsReaderFromMemory(uncompressedBuf, header.uncompressedSize, false);
		if (pReader == NULL)
		{
			free(uncompressedBuf);
			return postHeaderPos + header.compressedSize;
		}
		filePos = 0;
	}

	uint32_t classCount;
	pReader->Read(filePos, 4, &classCount); filePos += 4;
	classes.clear();
	classes.resize(classCount);
	for (uint32_t i = 0; i < classCount; i++)
	{
		filePos = classes[i].Read(pReader, filePos, header.fileVersion, header.flags);
	}
	/*classes = (ClassDatabaseType*)malloc(sizeof(ClassDatabaseType) * classCount);
	if (classes == NULL)
	{
		for (uint32_t i = 0; i < classCount; i++)
			filePos = ClassDatabaseType().Read(reader, readerPar, filePos);
		classCount = 0;
	}
	for (uint32_t i = 0; i < classCount; i++)
	{
		filePos = classes[i].Read(reader, readerPar, filePos);
	}*/

	stringTable = (char*)malloc((header.stringTableLen + 1) * sizeof(char));
	if (stringTable == NULL)
	{
		if (uncompressedBuf)
		{
			Free_AssetsReader(pReader);
			free(uncompressedBuf);
		}
		return postHeaderPos + header.compressedSize;
	}
	if (pReader->Read(header.stringTablePos, header.stringTableLen, stringTable) != header.stringTableLen)
	{
		if (uncompressedBuf)
		{
			Free_AssetsReader(pReader);
			free(uncompressedBuf);
		}
		return postHeaderPos + header.compressedSize;
	}
	stringTable[header.stringTableLen] = 0;
	filePos += header.stringTableLen;
	
	if (uncompressedBuf)
	{
		Free_AssetsReader(pReader);
		free(uncompressedBuf);
	}

	valid = true;
	return postHeaderPos + header.compressedSize;
}
ASSETSTOOLS_API bool ClassDatabaseFile::Read(IAssetsReader *pReader)
{
	Read(pReader, 0ULL);
	return valid;
}

enum EStringTableWriter_AddString_OptModes
{
	//better result without AddStringsOptimized and less memory overhead but slower than hash table
	StringTableWriter_Opt_LinearSearch, 
	//memory overhead of 512KiB(32)/1MiB(64) + sizeof(size_t) * stringCount; only better than linear with AddStringsOptimized called before
	StringTableWriter_Opt_HashTableSearch,
};
struct StringTableOptTmp
{
	size_t strLen;
	const char *str;
	StringTableOptTmp *parent;
};
struct StringTableHashEntryTmp
{
	size_t matchCount;
	StringTableOptTmp **matches;
};
class StringTableWriter
{
	char *stringTable;
	size_t stringTableBufferLen;
	size_t stringTableLen;
	//EStringTableWriter_AddString_OptModes optMode;
	struct StringTableHashEntry
	{
		size_t matchCount;
		size_t *matches; //offsets into stringTable
	}* stringHashTable; //only for making AddString optimization faster (searching for double entries)
public:
	StringTableWriter(EStringTableWriter_AddString_OptModes optMode = StringTableWriter_Opt_LinearSearch)
	{
		stringTable = NULL;
		stringTableLen = stringTableBufferLen = 0;
		//this->optMode = optMode;
		if (optMode == StringTableWriter_Opt_HashTableSearch)
		{
			stringHashTable = (StringTableHashEntry*)malloc(sizeof(StringTableHashEntry) * (MAXWORD+1)); //1 MiB
			memset(stringHashTable, 0, sizeof(StringTableHashEntry) * (MAXWORD+1));
		}
		else
		{
			stringHashTable = NULL;
		}
	}
	//linear only
	StringTableWriter(char *stringTable, size_t stringTableLen)
	{
		this->stringTable = stringTable;
		this->stringTableLen = this->stringTableBufferLen = stringTableLen;
		this->stringHashTable = NULL;
	}
	~StringTableWriter()
	{
		if (stringHashTable)
			free(stringHashTable);
	}
	//a small bit smaller stringtables than only AddStrings (1260485 vs. 1261082 Bytes) but A LOT slower (~0:02 vs. ~1:35)
	bool AddStringsOptimized(const char **strings, size_t count)
	{
		size_t stringCount = count;
		StringTableHashEntryTmp *_TmpStringHashTable = NULL;
		//StringTableHashEntryTmp *_TmpStringHashTable = (StringTableHashEntryTmp*)malloc(sizeof(StringTableHashEntryTmp) * (MAXWORD+1)); //1 MiB
		//memset(_TmpStringHashTable, 0, sizeof(StringTableHashEntryTmp) * (MAXWORD+1));
		StringTableOptTmp *_TmpStringTable = (StringTableOptTmp*)malloc(sizeof(StringTableOptTmp) * stringCount);
		if (_TmpStringTable)
		{
			if (!_TmpStringHashTable)
			{
				size_t curStringIndex = 0;
				for (uint32_t i = 0; i < stringCount; i++)
				{
					_TmpStringTable[curStringIndex].str = strings[i];
					_TmpStringTable[curStringIndex].strLen = (_TmpStringTable[curStringIndex].str ? strlen(_TmpStringTable[curStringIndex].str) : 0);
					_TmpStringTable[curStringIndex].parent = NULL;
					curStringIndex++;
				}
			}
			else
			{
				size_t curStringIndex = 0;
				for (uint32_t i = 0; i < stringCount; i++)
				{
					_TmpStringTable[curStringIndex].str = strings[i];
					_TmpStringTable[curStringIndex].strLen = (_TmpStringTable[curStringIndex].str ? strlen(_TmpStringTable[curStringIndex].str) : 0);
					_TmpStringTable[curStringIndex].parent = NULL;
					curStringIndex++;
					if (_TmpStringTable[curStringIndex-1].str)
					{
						StringTableHashEntryTmp *hashEntry = 
							&_TmpStringHashTable[_TmpStringTable[curStringIndex-1].strLen ? (*(const uint16_t*)strings[i]) : 0];
						{
							bool isDuplicate = false;
							for (uint32_t l = 0; l < hashEntry->matchCount; l++)
							{
								if ((hashEntry->matches[l]->strLen == _TmpStringTable[curStringIndex-1].strLen) &&
									!memcmp(_TmpStringTable[curStringIndex-1].str, 
										hashEntry->matches[l]->str, 
										hashEntry->matches[l]->strLen * sizeof(char)))
								{
									_TmpStringTable[curStringIndex-1].parent = hashEntry->matches[l];
									isDuplicate = true;
									break;
								}
							}
							if (isDuplicate)
								continue;
						}
						if (!hashEntry->matchCount || !((hashEntry->matchCount + 1) & 3))
						{
							void *tmp = realloc(hashEntry->matches, ((hashEntry->matchCount + 5) & (~3)) * sizeof(void*));
							if (!tmp)
							{
								if (hashEntry->matches)
								{
									free(hashEntry->matches);
									hashEntry->matches = NULL;
								}
								hashEntry->matchCount = 0;
								continue;
							}
							hashEntry->matches = (StringTableOptTmp**)tmp;
						}
						hashEntry->matches[hashEntry->matchCount] = &_TmpStringTable[curStringIndex-1];
						hashEntry->matchCount++;
					}
				}
				for (uint32_t i = 0; i < (MAXWORD+1); i++)
					if (_TmpStringHashTable[i].matches)
						free(_TmpStringHashTable[i].matches);
				free(_TmpStringHashTable);
			}
			for (uint32_t i = 0; i < stringCount; i++)
			{
				if (_TmpStringTable[i].str == NULL)
					continue;
				for (uint32_t k = 0; k < stringCount; k++)
				{
					if ((k == i) || (_TmpStringTable[k].strLen > _TmpStringTable[i].strLen))
						continue;
					if ((_TmpStringTable[k].str == NULL) || (_TmpStringTable[k].parent != NULL))
						continue;
					//for (size_t l = 0; l <= (_TmpStringTable[i].strLen - _TmpStringTable[k].strLen); l++)
					{
						const char *cmpTarget = &_TmpStringTable[i].str[_TmpStringTable[i].strLen-_TmpStringTable[k].strLen];
						if (!memcmp( _TmpStringTable[k].str, cmpTarget, (_TmpStringTable[k].strLen+1) * sizeof(char)))
						{
							_TmpStringTable[k].str = cmpTarget;
							_TmpStringTable[k].parent = &_TmpStringTable[i];
							//break;
						}
					}
				}
			}
			for (uint32_t i = 0; i < stringCount; i++)
			{
				if (_TmpStringTable[i].str && !_TmpStringTable[i].parent)
				{
					AddString(_TmpStringTable[i].str, false);
				}
			}
			free(_TmpStringTable);
			return true;
		}
		else
			return false;
		//MessageBox(NULL, TEXT("Out of memory while optimizing the string table!"), TEXT("ERROR"), 0);
	}
	size_t AddString(const char *string, bool optimize)
	{
		if (string == NULL)
			return 0;
		size_t strLen = strlen(string);
		uint16_t hash = 0;
		for (size_t i = 0; i < strLen; i++)
			hash += string[i];
		//uint16_t hash = strLen ? (*(const uint16_t*)string) : 0;
		if (optimize)
		{
			if (stringHashTable)
			{
				//only detects 100% equal strings -> best if AddStringsOptimized was called before
				StringTableHashEntry *pHashEntry = &stringHashTable[hash];
				for (size_t i = 0; i < pHashEntry->matchCount; i++)
					if (!strncmp(&stringTable[pHashEntry->matches[i]], string, strLen+1))
						return pHashEntry->matches[i];
			}
			else
			{
				for (size_t i = 0; i < stringTableLen; i++)
					if (!strncmp(&stringTable[i], string, strLen+1))
						return i;
			}
		}
		size_t ret = stringTableLen;
		if ((stringTableLen+strLen+1) > stringTableBufferLen)
		{
			char *pNewStringTable = (char*)realloc(stringTable, stringTableBufferLen=((stringTableLen+strLen+1)+15)&(~15));
			if (!pNewStringTable)
				return -1;
			stringTable = pNewStringTable;
		}
		if (stringHashTable)
		{
			//if out of memory, it works less efficiently (depending on how many of the matches arrays are affected)
			StringTableHashEntry *pHashEntry = &stringHashTable[hash];
			bool resized = true;
			if (!pHashEntry->matchCount || !((pHashEntry->matchCount + 1) & 3))
			{
				size_t *tmp = (size_t*)realloc(pHashEntry->matches, ((pHashEntry->matchCount + 5) & (~3)) * sizeof(size_t));
				if (!tmp)
				{
					if (pHashEntry->matches)
						free(pHashEntry->matches);
					pHashEntry->matchCount = 0;
					resized = false;
				}
				pHashEntry->matches = (size_t*)tmp;
			}
			if (resized)
			{
				pHashEntry->matches[pHashEntry->matchCount] = stringTableLen;
				pHashEntry->matchCount++;
			}
		}
		strncpy(&stringTable[ret], string, strLen+1);
		stringTableLen += (strLen+1);
		return ret;
	}
	char *GetStringTable()
	{
		return stringTable;
	}
	size_t GetStringTableLen()
	{
		return stringTableLen;
	}
};
ASSETSTOOLS_API QWORD ClassDatabaseFile::Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable, uint32_t compress, bool writeStringTable)
{
	uint32_t classCount = (uint32_t)classes.size();
	if (writeStringTable)
	{
		StringTableWriter strTableWriter;
		//puts only the strings that can't be interpreted as a part of another string into the string table
		if (optimizeStringTable == 2)
		{
			size_t stringCount = classCount;
			for (uint32_t i = 0; i < classCount; i++)
			{
				ClassDatabaseType *pType = &classes[i];
				stringCount += ((uint32_t)pType->fields.size()) * 2;
			}
			const char **stringList = (const char**)malloc(sizeof(char*) * stringCount);
			bool optResult = false;
			if (stringList)
			{
				size_t curStringIndex = 0;
				for (uint32_t i = 0; i < classCount; i++)
				{
					ClassDatabaseType *pType = &classes[i];
					stringList[curStringIndex] = pType->name.GetString(this);
					curStringIndex++;
					for (uint32_t k = 0; k < (uint32_t)pType->fields.size(); k++)
					{
						stringList[curStringIndex] = pType->fields[k].fieldName.GetString(this);
						curStringIndex++;
						stringList[curStringIndex] = pType->fields[k].typeName.GetString(this);
						curStringIndex++;
					}
				}
				optResult = strTableWriter.AddStringsOptimized(stringList, curStringIndex);
				free(stringList);
			}
			if (!optResult)
				MessageBox(NULL, TEXT("Out of memory while optimizing the string table!"), TEXT("ERROR"), 0);
		}
		for (uint32_t i = 0; i < classCount; i++)
		{
			ClassDatabaseType *pType = &classes[i];
			pType->name.str.stringTableOffset = (uint32_t)strTableWriter.AddString(pType->name.GetString(this), optimizeStringTable ? true : false);
			pType->name.fromStringTable = true;
			pType->assemblyFileName.str.stringTableOffset = (uint32_t)strTableWriter.AddString(pType->assemblyFileName.GetString(this), optimizeStringTable ? true : false);
			pType->assemblyFileName.fromStringTable = true;
			for (uint32_t k = 0; k < pType->fields.size(); k++)
			{
				ClassDatabaseTypeField *pTypeField = &pType->fields[k];
				pTypeField->fieldName.str.stringTableOffset = (uint32_t)strTableWriter.AddString(pTypeField->fieldName.GetString(this), optimizeStringTable ? true : false);
				pTypeField->fieldName.fromStringTable = true;
				pTypeField->typeName.str.stringTableOffset = (uint32_t)strTableWriter.AddString(pTypeField->typeName.GetString(this), optimizeStringTable ? true : false);
				pTypeField->typeName.fromStringTable = true;
			}
		}
		if (!this->dontFreeStringTable && this->stringTable != NULL)
			free(this->stringTable);
		this->stringTable = strTableWriter.GetStringTable();
		header.stringTableLen = (uint32_t)strTableWriter.GetStringTableLen();
		this->dontFreeStringTable = false;
	}

	QWORD headerPos = filePos;
	if (compress > 2)
		compress = 0;
#ifdef _WITHOUT_LZ4
	if (compress == 1)
		compress = 0;
#endif
#if (ClassDatabaseFileVersion==2)
	if (!compress)
		header.fileVersion = 1;
	else
#endif
#if (ClassDatabaseFileVersion==3)||(ClassDatabaseFileVersion==4)
	bool needsVersion3 = false;
	for (size_t i = 0; i < this->classes.size(); i++)
	{
		ClassDatabaseType &curType = this->classes[i];
		for (size_t k = 0; k < curType.fields.size(); k++)
		{
			if (curType.fields[k].version != 1)
			{
				needsVersion3 = true;
				goto goto_post_checkNeeds3;
			}
		}
	}
goto_post_checkNeeds3:
	if (!needsVersion3)
		header.fileVersion = compress ? 2 : 1;
	else
#endif
#if (ClassDatabaseFileVersion==4)
		header.fileVersion = 3;
	if (header.flags)
#endif
		header.fileVersion = ClassDatabaseFileVersion;
	header.compressionType = compress;// ? ClassDatabaseCompressionType : 0;
	filePos = header.Write(pWriter, filePos);
	QWORD postHeaderPos = filePos;
	IAssetsWriter *pDataWriter = pWriter;
	if (compress)
	{
		IAssetsWriter *pMemWriter = Create_AssetsWriterToMemory();
		if (!pMemWriter)
		{
			//we don't want to throw an error here, so silently don't compress the file
			header.compressionType = 0;
			compress = false;
		}
		else
		{
			pDataWriter = pMemWriter;
			filePos = 0;
		}
	}

	pDataWriter->Write(filePos, 4, &classCount); filePos += 4;
	for (uint32_t i = 0; i < classCount; i++)
	{
		filePos = classes[i].Write(pDataWriter, filePos, header.fileVersion, header.flags);
	}
	header.stringTablePos = (uint32_t)filePos;
	if (writeStringTable && (header.stringTableLen > 0))
		stringTable[header.stringTableLen-1] = 0;
	pDataWriter->Write(header.stringTablePos, header.stringTableLen, stringTable);
	filePos += header.stringTableLen;
	header.uncompressedSize = filePos - (compress ? 0 : postHeaderPos); //doesn't include the header

	if (compress)
	{
		size_t memWriterPos; void *memWriterBuf = NULL;
		((IAssetsWriterToMemory*)pDataWriter)->GetBuffer(memWriterBuf, memWriterPos);
		if (!memWriterBuf || (memWriterPos != filePos))
		{
			Free_AssetsWriter(pDataWriter);
			compress = false;
			return 0; //the memory writer went out of memory, we can't change that
		}
		uint32_t maxSize;
		if (compress == 2)
			maxSize = (uint32_t)(memWriterPos + memWriterPos / 3 + 128 + LZMA_PROPS_SIZE);
#ifndef _WITHOUT_LZ4
		else
			maxSize = (uint32_t)LZ4_compressBound((int)filePos);
#endif
		void *compressBuf = malloc(maxSize);
		if (compressBuf)
		{
#ifndef _WITHOUT_LZ4
			if (compress == 1)
				header.compressedSize = (uint32_t)LZ4_compress_default((char*)memWriterBuf, (char*)compressBuf, (int)memWriterPos, maxSize);
			else 
#endif
			if (compress == 2)
			{
				size_t destLen = maxSize - LZMA_PROPS_SIZE; size_t propsLen = LZMA_PROPS_SIZE;
				int result = LzmaCompress(
					&((uint8_t*)compressBuf)[LZMA_PROPS_SIZE], &destLen, 
					(uint8_t*)memWriterBuf, memWriterPos, 
					(uint8_t*)compressBuf, &propsLen, 
					-1, 0, -1, -1, -1, -1, -1);
				if ((propsLen != LZMA_PROPS_SIZE) || (result != SZ_OK))
					header.compressedSize = 0;
				else
					header.compressedSize = (uint32_t)(destLen + propsLen);
			}
			else
				header.compressedSize = 0;
		}
		if (!compressBuf || !header.compressedSize)
		{
			//out of memory or compression failure, but we can still write the uncompressed data
			header.compressionType = 0;
			compress = false;
			pWriter->Write(postHeaderPos, memWriterPos, memWriterBuf);
			Free_AssetsWriter(pDataWriter);
		}
		else
		{
			//compression succeeded
			Free_AssetsWriter(pDataWriter);
			pWriter->Write(postHeaderPos, header.compressedSize, compressBuf);
		}
		if (compressBuf)
			free(compressBuf);
	}
	else
		header.compressedSize = header.uncompressedSize;
	header.Write(pWriter, headerPos);

	return postHeaderPos + header.compressedSize;
}
ASSETSTOOLS_API bool ClassDatabaseFile::IsValid()
{
	return valid;
}

ASSETSTOOLS_API bool ClassDatabaseFile::InsertFrom(ClassDatabaseFile *pOther, ClassDatabaseType *pType)
{
	if (this->dontFreeStringTable)
		return false;
	StringTableWriter strTableWriter = StringTableWriter(this->stringTable, this->header.stringTableLen);

	classes.resize(classes.size()+1);
	ClassDatabaseType &newType = classes[classes.size()-1];
	newType.baseClass = pType->baseClass;
	newType.classId = pType->classId;
	size_t typeNameOffset = strTableWriter.AddString(pType->name.GetString(pOther), true);
	newType.name.fromStringTable = true;
	newType.name.str.stringTableOffset = typeNameOffset;
	newType.fields.resize(pType->fields.size());
	for (size_t i = 0; i < pType->fields.size(); i++)
	{
		ClassDatabaseTypeField &ownField = newType.fields[i];
		ClassDatabaseTypeField &otherField = pType->fields[i];
		size_t nameOffset = strTableWriter.AddString(otherField.fieldName.GetString(pOther), true);
		size_t typeOffset = strTableWriter.AddString(otherField.typeName.GetString(pOther), true);
		
		ownField.typeName.fromStringTable = true;
		ownField.typeName.str.stringTableOffset = typeOffset;
		ownField.fieldName.fromStringTable = true;
		ownField.fieldName.str.stringTableOffset = nameOffset;
		ownField.depth = otherField.depth;
		ownField.isArray = otherField.isArray;
		ownField.size = otherField.size;
		ownField.version = otherField.version;
		ownField.flags2 = otherField.flags2;
	}
	this->stringTable = strTableWriter.GetStringTable();
	this->header.stringTableLen = strTableWriter.GetStringTableLen();
	return true;
}

ASSETSTOOLS_API ClassDatabaseFile::ClassDatabaseFile()
{
	this->stringTable = NULL;
	this->valid = false; this->dontFreeStringTable = false;
	//ZeroMemory(this, sizeof(ClassDatabaseFile));
	//this->classes = std::vector<ClassDatabaseType>();
}
ASSETSTOOLS_API ClassDatabaseFile &ClassDatabaseFile::operator=(const ClassDatabaseFile& other)
{
	valid = other.valid;
	memcpy(&header, &other.header, sizeof(ClassDatabaseFileHeader));
	if (header.unityVersionCount > 0)
	{
		size_t newListLen = header.unityVersionCount * sizeof(char*);
		for (uint32_t i = 0; i < header.unityVersionCount; i++)
			newListLen += strlen(header.pUnityVersions[i])+1;
		header.pUnityVersions = (char**)malloc(newListLen);
		if (header.pUnityVersions == NULL)
		{
			header.unityVersionCount = 0;
		}
		else
		{
			size_t stringPos = header.unityVersionCount * sizeof(char*);
			for (uint32_t i = 0; i < other.header.unityVersionCount; i++)
			{
				header.pUnityVersions[i] = &((char*)header.pUnityVersions)[stringPos];
				size_t strLen = strlen(other.header.pUnityVersions[i]);
				memcpy(header.pUnityVersions[i], other.header.pUnityVersions[i], strLen+1);
				stringPos += (strLen+1);
			}
		}
	}
	else
		header.pUnityVersions = NULL;
	stringTable = (char*)malloc(header.stringTableLen);
	if (stringTable == NULL)
		header.stringTableLen = 0;
	else
		memcpy(stringTable, other.stringTable, header.stringTableLen);

	classes.reserve(other.classes.size());
	for (size_t i = 0; i < other.classes.size(); i++)
		classes.push_back(ClassDatabaseType(other.classes[i]));
	return (*this);
}
ASSETSTOOLS_API ClassDatabaseFile::ClassDatabaseFile(const ClassDatabaseFile& other)
{
	(*this) = other;
}
ASSETSTOOLS_API ClassDatabaseFile::ClassDatabaseFile(ClassDatabaseFile&& other)
{
	valid = other.valid;
	dontFreeStringTable = other.dontFreeStringTable;
	header = other.header;
	other.header.unityVersionCount = 0;
	other.header.pUnityVersions = nullptr;
	classes.swap(other.classes);
	stringTable = other.stringTable;
	other.stringTable = nullptr;
}
ASSETSTOOLS_API void ClassDatabaseFile::Clear()
{
	/*if ((header.assetsVersions != NULL) && (header.assetsVersions != (uint8_t*)&header._tmp))
	{
		free(header.assetsVersions);
		header.assetsVersions = NULL;
	}*/
	if (header.pUnityVersions != NULL)
	{
		free(header.pUnityVersions);
		header.pUnityVersions = NULL;
		header.unityVersionCount = 0;
	}
	/*if (classes != NULL)
	{
		for (uint32_t i = 0; i < classCount; i++)
		{
			if (classes[i].fields != NULL)
				free(classes[i].fields);
		}
		free(classes);
		classes = NULL;
		classCount = 0;
	}*/
	if (!this->dontFreeStringTable && stringTable != NULL)
	{
		free(stringTable);
		stringTable = NULL;
		header.stringTableLen = 0;
	}
	classes.clear();
}
ASSETSTOOLS_API ClassDatabaseFile::~ClassDatabaseFile()
{
	Clear();
}

ASSETSTOOLS_API void FreeClassDatabase_Dummy(ClassDatabaseFile *pFile)
{}

ASSETSTOOLS_API QWORD ClassDatabasePackageHeader::Read(IAssetsReader *pReader, QWORD filePos)
{
	if (pReader->Read(filePos, 4, &magic[0]) != 4)
		return 0;
	filePos += 4;
	if (memcmp(&magic[0], "CLPK", 4))
		return 0;
	if (pReader->Read(-1, 1, &fileVersion) != 1)
		return 0;
	filePos += 1;

	if (fileVersion > 1)
		return 0;

	if (pReader->Read(-1, 1, &compressionType) != 1)
		return 0;
	filePos += 1;
	if (pReader->Read(-1, 4, &stringTableOffset) != 4)
		return 0;
	filePos += 4;
	if (pReader->Read(-1, 4, &stringTableLenUncompressed) != 4)
		return 0;
	filePos += 4;
	if (pReader->Read(-1, 4, &stringTableLenCompressed) != 4)
		return 0;
	filePos += 4;
	if (fileVersion >= 1)
	{
		if (pReader->Read(-1, 4, &fileBlockSize) != 4)
			return 0;
		filePos += 4;
	}
	else
		fileBlockSize = 0;
	if (pReader->Read(-1, 4, &fileCount) != 4)
		return 0;
	filePos += 4;
	files.clear();
	files.reserve(fileCount);
	for (uint32_t i = 0; i < fileCount; i++)
	{
		ClassDatabaseFileRef ref;
		if (pReader->Read(-1, 4, &ref.offset) != 4)
			return 0;
		filePos += 4;
		if (pReader->Read(-1, 4, &ref.length) != 4)
			return 0;
		filePos += 4;
		if (pReader->Read(-1, 15, &ref.name[0]) != 15)
			return 0;
		ref.name[15] = 0;
		filePos += 15;
		files.push_back(ref);
	}
	return filePos;
}
ASSETSTOOLS_API QWORD ClassDatabasePackageHeader::Write(IAssetsWriter *pWriter, QWORD filePos)
{
	if (pWriter->Write(filePos, 4, "CLPK") != 4)
		return 0;
	filePos += 4;
	if (pWriter->Write(-1, 1, &fileVersion) != 1)
		return 0;
	filePos += 1;
	if (pWriter->Write(-1, 1, &compressionType) != 1)
		return 0;
	filePos += 1;
	if (fileVersion > 1)
		return 0;
	if (pWriter->Write(-1, 4, &stringTableOffset) != 4)
		return 0;
	filePos += 4;
	if (pWriter->Write(-1, 4, &stringTableLenUncompressed) != 4)
		return 0;
	filePos += 4;
	if (pWriter->Write(-1, 4, &stringTableLenCompressed) != 4)
		return 0;
	filePos += 4;
	if (fileVersion >= 1)
	{
		if (pWriter->Write(-1, 4, &fileBlockSize) != 4)
			return 0;
		filePos += 4;
	}
	if (pWriter->Write(-1, 4, &fileCount) != 4)
		return 0;
	filePos += 4;
	for (uint32_t i = 0; i < fileCount; i++)
	{
		ClassDatabaseFileRef &ref = files[i];
		if (pWriter->Write(-1, 4, &ref.offset) != 4)
			return 0;
		filePos += 4;
		if (pWriter->Write(-1, 4, &ref.length) != 4)
			return 0;
		filePos += 4;
		if (pWriter->Write(-1, 15, &ref.name[0]) != 15)
			return 0;
		filePos += 15;
	}
	return filePos;
}

ASSETSTOOLS_API void ClassDatabasePackage::Clear()
{
	if (files)
	{
		for (uint32_t i = 0; i < header.fileCount; i++)
		{
			if (files[i])
				delete files[i];
		}
		delete[] files;
		files = NULL;
	}
	if (stringTable)
	{
		free(stringTable);
		stringTable = NULL;
	}
}
ASSETSTOOLS_API bool ClassDatabasePackage::Read(IAssetsReader *pReader)
{
	Clear();
	valid = false;
	QWORD filePos = header.Read(pReader, 0);
	if (filePos == 0)
	{
		Clear();
		return false;
	}
	uint8_t compressAlgo = header.compressionType & 0x1F;

	IAssetsReader *pDatabasesReader = pReader;
	char *databasesBuf = nullptr;
	if (header.compressionType & 0x80) //Compress files in one block
	{
		QWORD compressedSize = (QWORD)header.stringTableOffset - filePos;
		char *compressedBuf = (char*)malloc((size_t)compressedSize);
		if (!compressedBuf)
		{
			Clear();
			return false;
		}
		if (pReader->Read(filePos, compressedSize, compressedBuf) != compressedSize)
		{
			free(compressedBuf);
			Clear();
			return false;
		}
		if (compressAlgo == 0 || (header.compressionType & 0x20))
		{
			pDatabasesReader = Create_AssetsReaderFromMemory(compressedBuf, (size_t)compressedSize, false);
			databasesBuf = compressedBuf;
		}
		else
		{
			databasesBuf = (char*)malloc(header.fileBlockSize);
			if (!databasesBuf)
			{
				free(compressedBuf);
				Clear();
				return false;
			}
			uint32_t uncompressedSize = 0;
			if (compressAlgo == 1)
			{
#ifdef _WITHOUT_LZ4
				free(compressedBuf);
				Clear();
				return false;
#else
				uncompressedSize = (uint32_t)LZ4_decompress_safe(
					compressedBuf, databasesBuf, 
					compressedSize, header.fileBlockSize
				);
#endif
			}
			else if (compressAlgo == 2 && compressedSize > LZMA_PROPS_SIZE)
			{
				size_t lz_uncompressedSize = header.fileBlockSize;
				size_t compressedWithoutProps = compressedSize - LZMA_PROPS_SIZE;
				int result = LzmaUncompress((Byte*)databasesBuf, &lz_uncompressedSize,
					(Byte*)&compressedBuf[LZMA_PROPS_SIZE], &compressedWithoutProps,
					(Byte*)compressedBuf, LZMA_PROPS_SIZE);
				if (result == SZ_OK)
					uncompressedSize = (uint32_t)lz_uncompressedSize;
			}
			free(compressedBuf);
			if (uncompressedSize != header.fileBlockSize)
			{
				free(databasesBuf);
				Clear();
				return false;
			}
			pDatabasesReader = Create_AssetsReaderFromMemory(databasesBuf, (size_t)header.fileBlockSize, false);
		}
	}
	files = new PClassDatabaseFile[header.fileCount];
	ZeroMemory(files, header.fileCount * sizeof(PClassDatabaseFile));
	for (uint32_t i = 0; i < header.fileCount; i++)
	{
		files[i] = new ClassDatabaseFile();
		IAssetsReader *pTempReader = Create_AssetsReaderFromReaderRange(pDatabasesReader, header.files[i].offset, header.files[i].length);
		if (!pTempReader || !files[i]->Read(pTempReader))
		{
			Clear();
			return false;
		}
		if (files[i]->stringTable != NULL)
		{
			free(files[i]->stringTable);
			files[i]->stringTable = NULL;
		}
		Free_AssetsReader(pTempReader);
	}
	if (databasesBuf)
	{
		free(databasesBuf);
		Free_AssetsReader(pDatabasesReader);
	}


	this->stringTable = (char*)malloc(header.stringTableLenUncompressed);
	if (!this->stringTable)
	{
		Clear();
		return false;
	}
	char *compressedBuf;
	if (header.compressionType != 0)
		compressedBuf = (char*)malloc(header.stringTableLenCompressed);
	else
	{
		header.stringTableLenCompressed = header.stringTableLenUncompressed;
		compressedBuf = this->stringTable;
	}
	if (!compressedBuf)
	{
		Clear();
		return false;
	}
	if (pReader->Read(header.stringTableOffset, header.stringTableLenCompressed, compressedBuf) != header.stringTableLenCompressed)
	{
		if (header.compressionType != 0) free(compressedBuf);
		Clear();
		return false;
	}
	if (compressAlgo != 0 && !(header.compressionType & 0x40))
	{
		uint32_t uncompressedSize = 0;
		if (compressAlgo == 1)
		{
#ifdef _WITHOUT_LZ4
			free(compressedBuf);
			Clear();
			return false;
#else
			uncompressedSize = (uint32_t)LZ4_decompress_safe(
				compressedBuf, this->stringTable, 
				header.stringTableLenCompressed, header.stringTableLenUncompressed
			);
#endif
		}
		else if (compressAlgo == 2 && header.stringTableLenCompressed > LZMA_PROPS_SIZE)
		{
			size_t lz_uncompressedSize = header.stringTableLenUncompressed;
			size_t compressedWithoutProps = header.stringTableLenCompressed - LZMA_PROPS_SIZE;
			int result = LzmaUncompress((Byte*)this->stringTable, &lz_uncompressedSize,
				(Byte*)&compressedBuf[LZMA_PROPS_SIZE], &compressedWithoutProps,
				(Byte*)compressedBuf, LZMA_PROPS_SIZE);
			if (result == SZ_OK)
				uncompressedSize = (uint32_t)lz_uncompressedSize;
		}
		free(compressedBuf);
		if (uncompressedSize != header.stringTableLenUncompressed)
		{
			Clear();
			return false;
		}
	}
	for (uint32_t i = 0; i < header.fileCount; i++)
	{
		files[i]->dontFreeStringTable = true;
		files[i]->stringTable = this->stringTable;
		files[i]->header.stringTableLen = this->header.stringTableLenUncompressed;
	}
	return (valid = true);
}

//success == false -> wrote uncompressed data
static bool CompressToWriter(QWORD filePos, IAssetsWriter *pWriter, 
	void *uncompressedBuf, uint32_t uncompressedLen, uint8_t compressAlgo,
	uint32_t &compressedLen)
{
	bool success = true;
	uint32_t maxSize;
	if (compressAlgo == 2)
		maxSize = (uint32_t)(uncompressedLen + uncompressedLen / 3 + 128 + LZMA_PROPS_SIZE);
	else
#ifdef _WITHOUT_LZ4
		return false;
#else
		maxSize = (uint32_t)LZ4_compressBound((int)uncompressedLen);
#endif
	void *compressBuf = malloc(maxSize);
	if (compressBuf)
	{
#ifndef _WITHOUT_LZ4
		if (compressAlgo == 1)
			compressedLen = (uint32_t)LZ4_compress_default((char*)uncompressedBuf, (char*)compressBuf, (int)uncompressedLen, maxSize);
		else 
#endif
		if (compressAlgo == 2)
		{
			size_t destLen = maxSize - LZMA_PROPS_SIZE; size_t propsLen = LZMA_PROPS_SIZE;
			int result = LzmaCompress(
				&((uint8_t*)compressBuf)[LZMA_PROPS_SIZE], &destLen, 
				(uint8_t*)uncompressedBuf, uncompressedLen, 
				(uint8_t*)compressBuf, &propsLen, 
				-1, 0, -1, -1, -1, -1, -1);
			if ((propsLen != LZMA_PROPS_SIZE) || (result != SZ_OK))
				compressedLen = 0;
			else
				compressedLen = (uint32_t)(destLen + propsLen);
		}
		else
			compressedLen = 0;
	}
	if (!compressBuf || !compressedLen)
	{
		//out of memory or compression failure, but we can still write the uncompressed data
		success = false;
		compressedLen = uncompressedLen;
		pWriter->Write(filePos, uncompressedLen, uncompressedBuf);
	}
	else
	{
		//compression succeeded
		pWriter->Write(filePos, compressedLen, compressBuf);
	}
	if (compressBuf)
		free(compressBuf);
	return success;
}
ASSETSTOOLS_API QWORD ClassDatabasePackage::Write(IAssetsWriter *pWriter, QWORD filePos, int optimizeStringTable, uint32_t compress)
{
	compress = (((compress & 0x1F) > 2) ? 0 : (compress & 0x1F)) | (compress & 0xE0);
	uint8_t compressAlgo = compress & 0x1F;
	header.compressionType = compress;
#if ClassDatabasePackageVersion == 1
	header.fileVersion = ((compress & 0xE0) != 0) ? 1 : 0;
#else
	header.fileVersion = ClassDatabasePackageVersion;
#endif

	StringTableWriter strTableWriter;
	if (optimizeStringTable == 2)
	{
		size_t stringCount = 0;
		for (uint32_t i = 0; i < header.fileCount; i++)
		{
			for (uint32_t k = 0; k < (uint32_t)this->files[i]->classes.size(); k++)
			{
				ClassDatabaseType *pType = &this->files[i]->classes[k];
				stringCount += 1 + ((uint32_t)pType->fields.size()) * 2;
			}
		}
		const char **stringList = (const char**)malloc(sizeof(char*) * stringCount);
		bool optResult = false;
		if (stringList)
		{
			size_t curStringIndex = 0;
			for (uint32_t i = 0; i < header.fileCount; i++)
			{
				ClassDatabaseFile *pCurFile = this->files[i];
				for (uint32_t k = 0; k < (uint32_t)pCurFile->classes.size(); k++)
				{
					ClassDatabaseType *pType = &pCurFile->classes[k];
					stringList[curStringIndex] = pType->name.GetString(pCurFile);
					curStringIndex++;
					for (uint32_t l = 0; l < (uint32_t)pType->fields.size(); l++)
					{
						stringList[curStringIndex] = pType->fields[l].fieldName.GetString(pCurFile);
						curStringIndex++;
						stringList[curStringIndex] = pType->fields[l].typeName.GetString(pCurFile);
						curStringIndex++;
					}
				}
			}
			optResult = strTableWriter.AddStringsOptimized(stringList, curStringIndex);
			free(stringList);
		}
		if (!optResult)
			MessageBox(NULL, TEXT("Out of memory while optimizing the string table!"), TEXT("ERROR"), 0);
	}
	for (uint32_t i = 0; i < header.fileCount; i++)
	{
		for (uint32_t k = 0; k < (uint32_t)this->files[i]->classes.size(); k++)
		{
			ClassDatabaseType *pType = (ClassDatabaseType*)&this->files[i]->classes[k];
			pType->name.str.stringTableOffset = (uint32_t)strTableWriter.AddString(pType->name.GetString(this->files[i]), optimizeStringTable ? true : false);
			pType->name.fromStringTable = true;
			for (uint32_t l = 0; l < (uint32_t)pType->fields.size(); l++)
			{
				pType->fields[l].fieldName.str.stringTableOffset = 
					(uint32_t)strTableWriter.AddString(pType->fields[l].fieldName.GetString(this->files[i]), optimizeStringTable ? true : false);
				pType->fields[l].fieldName.fromStringTable = true;
				pType->fields[l].typeName.str.stringTableOffset = 
					(uint32_t)strTableWriter.AddString(pType->fields[l].typeName.GetString(this->files[i]), optimizeStringTable ? true : false);
				pType->fields[l].typeName.fromStringTable = true;
			}
		}
	}
	if (this->stringTable)
	{
		free(this->stringTable);
		this->stringTable = NULL;
	}
	this->stringTable = strTableWriter.GetStringTable();
	header.stringTableLenUncompressed = (uint32_t)strTableWriter.GetStringTableLen();
	QWORD headerPos = filePos;
	filePos = header.Write(pWriter, filePos);

	{
		IAssetsWriter *pFileWriter = pWriter;
		IAssetsWriterToMemory *pBlockWriter = nullptr;
		QWORD blockFilePos = filePos;
		if ((compress & 0x80) && !(compress & 0x20))
		{
			pBlockWriter = Create_AssetsWriterToMemory();
			pFileWriter = pBlockWriter;
			blockFilePos = 0;
		}
		for (uint32_t i = 0; i < header.fileCount; i++)
		{
			if (!this->files[i]->dontFreeStringTable)
			{
				this->files[i]->dontFreeStringTable = true;
				if (this->files[i]->stringTable)
					free(this->files[i]->stringTable);
			}
			this->files[i]->stringTable = this->stringTable;
			this->files[i]->header.stringTableLen = 0; //temporarily for writing
			header.files[i].offset = (uint32_t)blockFilePos;
			blockFilePos = this->files[i]->Write(pFileWriter, blockFilePos, false, (compress & 0x80) ? 0 : compressAlgo, false);
			header.files[i].length = (uint32_t)blockFilePos - header.files[i].offset;
			this->files[i]->header.stringTableLen = header.stringTableLenUncompressed;
		}
		if (compress & 0x80)
		{
			header.fileBlockSize = (uint32_t)blockFilePos;

			void *pBlockBuffer = nullptr; size_t blockBufferSize = 0;
			if (!pBlockWriter->GetBuffer(pBlockBuffer, blockBufferSize))
			{
				MessageBox(NULL, TEXT("Unable to retrieve the raw class databases!"), TEXT("ERROR"), 0);
				header.fileBlockSize = 0;
			}

			uint32_t compressedLen = 0;
			if (!CompressToWriter(filePos, pWriter, pBlockBuffer, header.fileBlockSize, compressAlgo, compressedLen))
				header.compressionType |= 0x20;
			filePos += compressedLen;

			Free_AssetsWriter(pBlockWriter);
		}
	}

	QWORD stringTablePos = filePos;
	header.stringTableOffset = (uint32_t)stringTablePos;
	header.stringTableLenCompressed = header.stringTableLenUncompressed;
	memcpy(&header.magic[0], "CLPK", 4);
	if (compressAlgo && (compress & 0x40) == 0)
	{
		if (!CompressToWriter(filePos, pWriter, this->stringTable, header.stringTableLenUncompressed, compressAlgo, header.stringTableLenCompressed))
			header.compressionType |= 0x40;
	}
	else
		pWriter->Write(stringTablePos, header.stringTableLenUncompressed, this->stringTable);
	header.Write(pWriter, headerPos);
	return stringTablePos + header.stringTableLenCompressed;
}
ASSETSTOOLS_API bool ClassDatabasePackage::RemoveFile(uint32_t index)
{
	if (index >= header.fileCount)
		return false;
	if (files == NULL)
		return false;
	header.files.erase(header.files.begin() + index);
	ClassDatabaseFile **newFiles = new PClassDatabaseFile[(header.fileCount - 1)];
	memcpy(newFiles, files, sizeof(ClassDatabaseFile*) * index);
	memcpy(&newFiles[index], &files[index+1], sizeof(ClassDatabaseFile*) * (header.fileCount - index - 1));
	delete files[index];
	delete[] files;
	files = newFiles;
	header.fileCount--;
	return true;
}
ASSETSTOOLS_API bool ClassDatabasePackage::ImportFile(IAssetsReader *pReader)
{
	ClassDatabaseFile **newFiles = new PClassDatabaseFile[(header.fileCount + 1)];
	memcpy(newFiles, files, sizeof(ClassDatabaseFile*) * header.fileCount);
	ClassDatabaseFile *pFile = new ClassDatabaseFile();
	if (!pFile->Read(pReader))
	{
		delete pFile;
		delete[] newFiles;
		return false;
	}
	newFiles[header.fileCount] = pFile;
	delete[] files;
	files = newFiles;
	ClassDatabaseFileRef fileRef;
	fileRef.length = fileRef.offset = 0;
	memset(fileRef.name, 0, 16);
	header.files.push_back(fileRef);
	header.fileCount++;
	return true;
}
ASSETSTOOLS_API bool ClassDatabasePackage::IsValid()
{
	return valid;
}
	
ASSETSTOOLS_API ClassDatabasePackage::~ClassDatabasePackage()
{
	Clear();
}