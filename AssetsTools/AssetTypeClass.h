#pragma once
#include "defines.h"
#include "AssetsFileFormat.h"
#include "ClassDatabaseFile.h"
#include <vector>
#include <exception>

//Exception type for AssetTypeValue::Set.
class AssetTypeValue_ConfusionError : public std::exception
{
	std::string desc;
public:
	inline AssetTypeValue_ConfusionError(std::string _desc)
		: desc(std::move(_desc))
	{}
	inline const char* what()
	{
		return desc.c_str();
	}
};

class AssetTypeValueField;
//class to read asset files using type information
struct AssetTypeArray
{
	uint32_t size;
	//AssetTypeValueField *dataField;
};
struct AssetTypeByteArray
{
	uint32_t size;
	uint8_t *data;
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
	inline EnumValueTypes GetType() const
	{
		return type;
	}
	//valueContainer :
	// - For primitive types (bool, int, uint, float, double) this is a pointer to the value.
	// - For strings, this is the char* C-style string pointer.
	// - For Array and ByteArray, this is the AssetTypeArray* / AssetTypeByteArray* value.
	//type : If set, is checked against GetType() to prevent type confusion. Does NOT change the set value type!
	// -> Bigger (u)ints can be assigned to smaller; no signed/unsigned check, bools treated as (u)int8.
	//    For instance, ValueType_UInt64 can be assigned to AssetTypeValue with GetType()==ValueType_Int32
	//                  but not the other way round.
	// -> Other types require an exact match.
	// Throws an AssetTypeValue_ConfusionError if a critical mismatch is detected.
	ASSETSTOOLS_API void Set(void *valueContainer, EnumValueTypes type = ValueType_None);
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
	inline bool AsBool() const
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
	inline int AsInt() const
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
	inline unsigned int AsUInt() const
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
	inline long long int AsInt64() const
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
	inline unsigned long long int AsUInt64() const
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
	inline float AsFloat() const
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
		case ValueType_Int64:
			return (float)value.asInt64;
		default:
			return (float)value.asUInt64;
		}
	}
	inline double AsDouble() const
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
		case ValueType_Int64:
			return (double)value.asInt64;
		default:
			return (double)value.asUInt64;
		}
	}
};

class AssetTypeValueField;
class AssetTypeTemplateField
{
public:
	std::string name;
	std::string type;
	EnumValueTypes valueType;
	bool isArray;
	bool align;
	bool hasValue;
	std::vector<AssetTypeTemplateField> children;
	
public:
	ASSETSTOOLS_API AssetTypeTemplateField();
	ASSETSTOOLS_API ~AssetTypeTemplateField();
	ASSETSTOOLS_API void Clear();
	ASSETSTOOLS_API bool From0D(Type_0D *pU5Type, uint32_t fieldIndex);
	ASSETSTOOLS_API bool FromClassDatabase(ClassDatabaseFile *pFile, ClassDatabaseType *pType, uint32_t fieldIndex);
	ASSETSTOOLS_API bool From07(TypeField_07 *pTypeField);
	ASSETSTOOLS_API QWORD MakeValue(IAssetsReader *pReader, QWORD filePos, QWORD fileLen, AssetTypeValueField **ppValueField, bool bigEndian);
	ASSETSTOOLS_API bool AddChildren(uint32_t count);

	ASSETSTOOLS_API AssetTypeTemplateField *SearchChild(const char* name);
};
ASSETSTOOLS_API void ClearAssetTypeValueField(AssetTypeValueField *pValueField);
class AssetTypeValueField
{
protected:
	AssetTypeTemplateField *templateField;
	
	uint32_t childrenCount;
	AssetTypeValueField **pChildren;
	AssetTypeValue *value; //pointer so it may also have no value (NULL)
public:

	ASSETSTOOLS_API void Read(AssetTypeValue *pValue, AssetTypeTemplateField *pTemplate, uint32_t childrenCount, AssetTypeValueField **pChildren);
	ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, bool bigEndian);

	//ASSETSTOOLS_API void Clear();

	//get a child field by its name
	ASSETSTOOLS_API AssetTypeValueField* operator[](const char* name) const;
	//get a child field by its index
	ASSETSTOOLS_API AssetTypeValueField* operator[](uint32_t index) const;

	inline AssetTypeValueField* Get(const char* name) const { return (*this)[name]; }
	inline AssetTypeValueField* Get(unsigned int index) const { return (*this)[index]; }

	inline const std::string &GetName() const { return templateField->name; }
	inline const std::string &GetType() const { return templateField->type; }
	inline AssetTypeValue *GetValue() const { return value; }
	inline AssetTypeTemplateField *GetTemplateField() const { return templateField; }
	inline AssetTypeValueField **GetChildrenList() const { return pChildren; }
	inline void SetChildrenList(AssetTypeValueField **pChildren, uint32_t childrenCount) { this->pChildren = pChildren; this->childrenCount = childrenCount; }

	inline uint32_t GetChildrenCount() const { return childrenCount; }

	ASSETSTOOLS_API bool IsDummy() const;

	ASSETSTOOLS_API QWORD GetByteSize(QWORD filePos = 0);
};
ASSETSTOOLS_API EnumValueTypes GetValueTypeByTypeName(const char *type);
ASSETSTOOLS_API AssetTypeValueField* GetDummyAssetTypeField();

class AssetTypeInstance
{
	std::vector<AssetTypeValueField*> baseFields;
	std::vector<void*> memoryToClear;
	
	ASSETSTOOLS_API void Clear();

private:
	ASSETSTOOLS_API AssetTypeInstance(AssetTypeInstance &other) = delete;
	ASSETSTOOLS_API AssetTypeInstance &operator=(const AssetTypeInstance &other) = delete;
public:
	inline AssetTypeInstance()
	{}
	inline AssetTypeInstance(AssetTypeInstance &&other) noexcept
	{
		(*this) = std::move(other);
	}
	inline AssetTypeInstance &operator=(AssetTypeInstance&& other) noexcept
	{
		this->Clear();
		baseFields = std::move(other.baseFields);
		memoryToClear = std::move(other.memoryToClear);
		return *this;
	}
	ASSETSTOOLS_API AssetTypeInstance(uint32_t baseFieldCount, AssetTypeTemplateField **ppBaseFields, QWORD fileLen, IAssetsReader *pReader, bool bigEndian, QWORD filePos = 0);
	ASSETSTOOLS_API bool SetChildList(AssetTypeValueField *pValueField, AssetTypeValueField **pChildrenList, uint32_t childrenCount, bool freeMemory = true);
	ASSETSTOOLS_API bool AddTempMemory(void *pMemory);
	ASSETSTOOLS_API ~AssetTypeInstance();

	inline AssetTypeValueField *GetBaseField(uint32_t index = 0)
	{
		if (index >= baseFields.size())
			return GetDummyAssetTypeField();
		return baseFields[index];
	}
};