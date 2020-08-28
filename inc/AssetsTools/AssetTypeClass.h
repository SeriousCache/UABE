#pragma once
#include "defines.h"
#include "AssetsFileFormat.h"
#include "ClassDatabaseFile.h"

class AssetTypeValueField;
//class to read asset files using type information
struct AssetTypeArray
{
	DWORD size;
	//AssetTypeValueField *dataField;
};
struct AssetTypeByteArray
{
	DWORD size;
	BYTE *data;
};

enum EnumValueTypes
{
	ValueType_None,
	ValueType_Bool,
	ValueType_Int8,
	ValueType_UInt8,
	ValueType_Int16,
	ValueType_UInt16,
	ValueType_Int32,
	ValueType_UInt32,
	ValueType_Int64,
	ValueType_UInt64,
	ValueType_Float,
	ValueType_Double,
	ValueType_String,
	ValueType_Array,
	ValueType_ByteArray
};

class AssetTypeValue
{
public:
	union ValueTypes
	{
		AssetTypeArray asArray;
		AssetTypeByteArray asByteArray;

		bool asBool;

		char asInt8;
		unsigned char asUInt8;

		short asInt16;
		unsigned short asUInt16;

		int asInt32;
		unsigned int asUInt32;

		long long int asInt64;
		unsigned long long int asUInt64;

		float asFloat;
		double asDouble;

		char *asString;
	};
protected:
	//bool freeValue;
	EnumValueTypes type; 
	ValueTypes value;
public:
	//Creates an AssetTypeValue.
	//type : the value type which valueContainer stores
	//valueContainer : the buffer for the value type
	//freeIfPointer : should the value get freed if value type is Array/String
	ASSETSTOOLS_API AssetTypeValue(EnumValueTypes type, void *valueContainer);
	ASSETSTOOLS_API AssetTypeValue(const AssetTypeValue &other);
	ASSETSTOOLS_API ~AssetTypeValue();
	inline EnumValueTypes GetType()
	{
		return type;
	}
	ASSETSTOOLS_API void Set(void *valueContainer);
	inline AssetTypeArray *AsArray()
	{
		return (type == ValueType_Array) ? &value.asArray : NULL; 
	}
	inline AssetTypeByteArray *AsByteArray()
	{
		return (type == ValueType_ByteArray) ? &value.asByteArray : NULL; 
	}
	inline char *AsString()
	{
		return (type == ValueType_String) ? value.asString : NULL; 
	}
	inline bool AsBool()
	{
		switch (type)
		{
		case ValueType_Float:
		case ValueType_Double:
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return false;
		default:
			return value.asBool;
		}
	}
	inline int AsInt()
	{
		switch (type)
		{
		case ValueType_Float:
			return (int)value.asFloat;
		case ValueType_Double:
			return (int)value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		case ValueType_Int8:
			return (int)value.asInt8;
		case ValueType_Int16:
			return (int)value.asInt16;
		case ValueType_Int64:
			return (int)value.asInt64;
		default:
			return value.asInt32;
		}
	}
	inline unsigned int AsUInt()
	{
		switch (type)
		{
		case ValueType_Float:
			return (unsigned int)value.asFloat;
		case ValueType_Double:
			return (unsigned int)value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		default:
			return value.asUInt32;
		}
	}
	inline long long int AsInt64()
	{
		switch (type)
		{
		case ValueType_Float:
			return (long long int)value.asFloat;
		case ValueType_Double:
			return (long long int)value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		case ValueType_Int8:
			return (long long int)value.asInt8;
		case ValueType_Int16:
			return (long long int)value.asInt16;
		case ValueType_Int32:
			return (long long int)value.asInt32;
		default:
			return value.asInt64;
		}
	}
	inline unsigned long long int AsUInt64()
	{
		switch (type)
		{
		case ValueType_Float:
			return (unsigned int)value.asFloat;
		case ValueType_Double:
			return (unsigned long long int)value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		default:
			return value.asUInt64;
		}
	}
	inline float AsFloat()
	{
		switch (type)
		{
		case ValueType_Float:
			return value.asFloat;
		case ValueType_Double:
			return (float)value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		case ValueType_Int8:
			return (float)value.asInt8;
		case ValueType_Int16:
			return (float)value.asInt16;
		case ValueType_Int32:
			return (float)value.asInt32;
		default:
			return (float)value.asUInt64;
		}
	}
	inline double AsDouble()
	{
		switch (type)
		{
		case ValueType_Float:
			return (double)value.asFloat;
		case ValueType_Double:
			return value.asDouble;
		case ValueType_String:
		case ValueType_ByteArray:
		case ValueType_Array:
			return 0;
		case ValueType_Int8:
			return (double)value.asInt8;
		case ValueType_Int16:
			return (double)value.asInt16;
		case ValueType_Int32:
			return (double)value.asInt32;
		default:
			return (double)value.asUInt64;
		}
	}
};

class AssetTypeValueField;
class AssetTypeTemplateField
{
public:
	const char *name;
	const char *type;
	EnumValueTypes valueType;
	bool freeNames; //Required if name/type are copied for this instance.
	bool isArray;
	bool align;
	bool hasValue;
	DWORD childrenCount;
	AssetTypeTemplateField *children;
	
public:
	ASSETSTOOLS_API AssetTypeTemplateField();
	ASSETSTOOLS_API ~AssetTypeTemplateField();
	ASSETSTOOLS_API void Clear();
	ASSETSTOOLS_API bool From0D(Type_0D *pU5Type, DWORD fieldIndex, bool copyNames = false);
	ASSETSTOOLS_API bool FromClassDatabase(ClassDatabaseFile *pFile, ClassDatabaseType *pType, DWORD fieldIndex, bool copyNames = false);
	ASSETSTOOLS_API bool From07(TypeField_07 *pTypeField, bool copyNames = false);
	ASSETSTOOLS_API QWORD MakeValue(IAssetsReader *pReader, QWORD filePos, QWORD fileLen, AssetTypeValueField **ppValueField, bool bigEndian);
	ASSETSTOOLS_API bool AddChildren(DWORD count);

	ASSETSTOOLS_API AssetTypeTemplateField *SearchChild(const char* name);
};
ASSETSTOOLS_API void ClearAssetTypeValueField(AssetTypeValueField *pValueField);
class AssetTypeValueField
{
protected:
	AssetTypeTemplateField *templateField;
	
	DWORD childrenCount;
	AssetTypeValueField **pChildren;
	AssetTypeValue *value; //pointer so it may also have no value (NULL)
public:

	ASSETSTOOLS_API void Read(AssetTypeValue *pValue, AssetTypeTemplateField *pTemplate, DWORD childrenCount, AssetTypeValueField **pChildren);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, bool bigEndian);

	//ASSETSTOOLS_API void Clear();

	//get a child field by its name
	ASSETSTOOLS_API AssetTypeValueField* operator[](const char* name);
	//get a child field by its index
	ASSETSTOOLS_API AssetTypeValueField* operator[](DWORD index);

	inline AssetTypeValueField* Get(const char* name) { return (*this)[name]; }
	inline AssetTypeValueField* Get(unsigned int index) { return (*this)[index]; }

	inline const char *GetName() { return templateField->name; }
	inline const char *GetType() { return templateField->type; }
	inline AssetTypeValue *GetValue() { return value; }
	inline AssetTypeTemplateField *GetTemplateField() { return templateField; }
	inline AssetTypeValueField **GetChildrenList() { return pChildren; }
	inline void SetChildrenList(AssetTypeValueField **pChildren, DWORD childrenCount) { this->pChildren = pChildren; this->childrenCount = childrenCount; }

	inline DWORD GetChildrenCount() { return childrenCount; }

	ASSETSTOOLS_API bool IsDummy();

	ASSETSTOOLS_API QWORD GetByteSize(QWORD filePos = 0);
};
ASSETSTOOLS_API EnumValueTypes GetValueTypeByTypeName(const char *type);
ASSETSTOOLS_API AssetTypeValueField* GetDummyAssetTypeField();

class AssetTypeInstance
{
	DWORD baseFieldCount;
	AssetTypeValueField **baseFields;
	DWORD allocationCount; DWORD allocationBufLen;
	void **memoryToClear;
public:
	ASSETSTOOLS_API AssetTypeInstance(DWORD baseFieldCount, AssetTypeTemplateField **ppBaseFields, QWORD fileLen, IAssetsReader *pReader, bool bigEndian, QWORD filePos = 0);
	ASSETSTOOLS_API bool SetChildList(AssetTypeValueField *pValueField, AssetTypeValueField **pChildrenList, DWORD childrenCount, bool freeMemory = true);
	ASSETSTOOLS_API bool AddTempMemory(void *pMemory);
	ASSETSTOOLS_API ~AssetTypeInstance();

	inline AssetTypeValueField *GetBaseField(DWORD index = 0)
	{
		if (index >= baseFieldCount)
			return GetDummyAssetTypeField();
		return baseFields[index];
	}
};