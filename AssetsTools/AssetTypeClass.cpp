#include "stdafx.h"
#include "../AssetsTools/AssetTypeClass.h"
#include <typeinfo>
#include <assert.h>

ASSETSTOOLS_API AssetTypeValue::AssetTypeValue(EnumValueTypes type, void *valueContainer)
{
	//freeValue = false;
	this->type = type;
	Set(valueContainer);
}
ASSETSTOOLS_API AssetTypeValue::AssetTypeValue(const AssetTypeValue &other)
{
	//freeValue = false;
	this->type = other.type;
	this->value = other.value;
}
ASSETSTOOLS_API void AssetTypeValue::Set(void *valueContainer, EnumValueTypes contType)
{
	bool mismatch = false;
	memset(&value, 0, sizeof(ValueTypes));
	switch (this->type)
	{
		case ValueType_None:
			break;
		case ValueType_Bool:
			if (contType >= ValueType_None && contType <= ValueType_UInt64)
				value.asBool = *(bool*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Int8:
		case ValueType_UInt8:
			if (contType >= ValueType_None && contType <= ValueType_UInt64)
				value.asInt8 = *(char*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Int16:
		case ValueType_UInt16:
			if (contType == ValueType_None || (contType >= ValueType_Int16 && contType <= ValueType_UInt64))
				value.asInt16 = *(short*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Int32:
		case ValueType_UInt32:
			if (contType == ValueType_None || (contType >= ValueType_Int32 && contType <= ValueType_UInt64))
				value.asInt32 = *(int*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Int64:
		case ValueType_UInt64:
			if (contType == ValueType_None || (contType >= ValueType_Int64 && contType <= ValueType_UInt64))
				value.asInt64 = *(long long int*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Float:
			if (contType == ValueType_None || contType == ValueType_Float)
				value.asFloat = *(float*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_Double:
			if (contType == ValueType_None || contType == ValueType_Double)
				value.asDouble = *(double*)valueContainer;
			else mismatch = true;
			break;
		case ValueType_String:
			if (contType == ValueType_None || contType == ValueType_String)
				value.asString = (char*)valueContainer;
			else mismatch = true;
			//freeValue = freeIfPointer;
			break;
		case ValueType_Array:
			if (contType == ValueType_None || contType == ValueType_Array)
				memcpy(&this->value.asArray, valueContainer, sizeof(AssetTypeArray));
			else mismatch = true;
			//if (freeIfPointer)
			//	free(valueContainer);
			break;
		case ValueType_ByteArray:
			if (contType == ValueType_None || contType == ValueType_ByteArray)
				memcpy(&this->value.asByteArray, valueContainer, sizeof(AssetTypeByteArray));
			else mismatch = true;
			break;
	}
	if (mismatch)
		throw AssetTypeValue_ConfusionError("AssetTypeValue::Set: Mismatching value type supplied.");
}
ASSETSTOOLS_API AssetTypeValue::~AssetTypeValue()
{
	/*if (freeValue && ((type == ValueType_String) || (type == ValueType_Array)))
	{
		free(value.asString);
		value.asString = NULL;
	}*/
}

ASSETSTOOLS_API AssetTypeValueField* AssetTypeValueField::operator[](const char* name) const
{
	if (childrenCount == -1)
		return GetDummyAssetTypeField();
	for (uint32_t i = 0; i < childrenCount; i++)
	{
		if (pChildren[i]->templateField != NULL)
		{
			if (pChildren[i]->templateField->name == name)
				return pChildren[i];
		}
	}
	return GetDummyAssetTypeField();
}

ASSETSTOOLS_API AssetTypeValueField* AssetTypeValueField::operator[](uint32_t index) const
{
	if (childrenCount == -1)
		return GetDummyAssetTypeField();
	if (index >= childrenCount)
		return GetDummyAssetTypeField();
	return pChildren[index];
}
ASSETSTOOLS_API void AssetTypeValueField::Read(AssetTypeValue *pValue, AssetTypeTemplateField *pTemplate, uint32_t childrenCount, AssetTypeValueField **pChildren)
{
	this->value = pValue;
	this->templateField = pTemplate;
	this->childrenCount = childrenCount;
	this->pChildren = pChildren;
	//char valueContainer[16];
	//value = new AssetTypeValue(templateField->type, valueContainer);
}
ASSETSTOOLS_API QWORD AssetTypeValueField::Write(IAssetsWriter *pWriter, QWORD filePos, bool bigEndian)
{
	QWORD qwValueTmp;
	float fValueTmp;
	double dValueTmp;
	uint32_t dwValueTmp;
	uint8_t byValueTmp;
	bool doPadding = this->templateField->align;
	if (this->templateField->children.empty()
		&& this->value != NULL && this->value->GetType() != ValueType_ByteArray)
	{
		switch (this->templateField->valueType)
		{
			case ValueType_Bool:
			case ValueType_Int8:
			case ValueType_UInt8:
				byValueTmp = this->value->AsInt() & 0xFF;
				pWriter->Write(filePos, 1, &byValueTmp); filePos++;
				break;
			case ValueType_Int16:
			case ValueType_UInt16:
				dwValueTmp = this->value->AsInt() & 0xFFFF;
				if (bigEndian)
					SwapEndians_((dwValueTmp <<= 16));
				pWriter->Write(filePos, 2, &dwValueTmp); filePos+=2;
				break;
			case ValueType_Int32:
			case ValueType_UInt32:
				dwValueTmp = this->value->AsInt();
				if (bigEndian)
					SwapEndians_(dwValueTmp);
				pWriter->Write(filePos, 4, &dwValueTmp); filePos+=4;
				break;
			case ValueType_Int64:
			case ValueType_UInt64:
				qwValueTmp = this->value->AsInt64();
				if (bigEndian)
					SwapEndians_(qwValueTmp);
				pWriter->Write(filePos, 8, &qwValueTmp); filePos+=8;
				break;
			case ValueType_Float:
				fValueTmp = this->value->AsFloat();
				if (bigEndian)
					SwapEndians_(fValueTmp);
				pWriter->Write(filePos, 4, &fValueTmp); filePos+=4;
				break;
			case ValueType_Double:
				dValueTmp = this->value->AsDouble();
				if (bigEndian)
					SwapEndians_(dValueTmp);
				pWriter->Write(filePos, 8, &dValueTmp); filePos+=8;
				break;
		}
	}
	else if (this->value != NULL && this->value->GetType() == ValueType_String)
	{
		const char *strVal = this->value->AsString();
		if (strVal == NULL)
			strVal = "";
		uint32_t curStrLen = (uint32_t)strlen(strVal);
		dwValueTmp = curStrLen;
		if (bigEndian)
			SwapEndians_(dwValueTmp);
		pWriter->Write(filePos, 4, &dwValueTmp); filePos+=4;
		pWriter->Write(filePos, curStrLen, strVal); filePos+=curStrLen;
		if ((this->templateField->children.size() == 1) && this->templateField->children[0].align)
			doPadding = true;
	}
	else if (this->value != NULL
		&& (this->value->GetType() == ValueType_Array || this->value->GetType() == ValueType_ByteArray))
	{
		if (this->value->GetType() == ValueType_ByteArray)
		{
			uint32_t curByteLen = this->value->AsByteArray()->size;
			dwValueTmp = curByteLen;
			if (bigEndian)
				SwapEndians_(dwValueTmp);
			pWriter->Write(filePos, 4, &dwValueTmp); filePos += 4;
			pWriter->Write(filePos, curByteLen, this->value->AsByteArray()->data); filePos += curByteLen;
		}
		else
		{
			uint32_t curArrLen = this->value->AsArray()->size;
			dwValueTmp = curArrLen;
			pWriter->Write(filePos, 4, &dwValueTmp); filePos += 4;
			for (uint32_t i = 0; i < curArrLen; i++)
			{
				filePos = this->pChildren[i]->Write(pWriter, filePos, bigEndian);
			}
		}
		if ((this->templateField->children.size() == 1) && this->templateField->children[0].align)
			doPadding = true; //For special case: String overwritten with ByteArray value.
	}
	else if (this->childrenCount > 0)
	{
		for (uint32_t i = 0; i < this->childrenCount; i++)
		{
			filePos = this->pChildren[i]->Write(pWriter, filePos, bigEndian);
		}
	}
	if (doPadding)
	{
		int paddingLen = 3-(((filePos&3)-1) & 3);
		if (paddingLen > 0)
		{
			dwValueTmp = 0;
			pWriter->Write(filePos, paddingLen, &dwValueTmp); filePos += paddingLen;
		}
	}
	return filePos;
}
ASSETSTOOLS_API QWORD AssetTypeValueField::GetByteSize(QWORD filePos)
{
	bool doPadding = this->templateField->align;
	if ((this->templateField->children.empty()) && (this->value != NULL))
	{
		switch (this->templateField->valueType)
		{
			case ValueType_Bool:
			case ValueType_Int8:
			case ValueType_UInt8:
				filePos++;
				break;
			case ValueType_Int16:
			case ValueType_UInt16:
				filePos+=2;
				break;
			case ValueType_Int32:
			case ValueType_UInt32:
			case ValueType_Float:
				filePos+=4;
				break;
			case ValueType_Int64:
			case ValueType_UInt64:
			case ValueType_Double:
				filePos+=8;
				break;
		}
	}
	else if ((this->templateField->valueType == ValueType_String) && (this->value != NULL))
	{
		filePos+=4 + strlen(this->value->AsString());
		if ((this->templateField->children.size() > 0) && this->templateField->children[0].align)
			doPadding = true;
	}
	else if (this->templateField->isArray && (this->value != NULL))
	{
		filePos += 4;
		if (!_stricmp(this->templateField->type.c_str(), "TypelessData"))
			filePos += this->value->AsByteArray()->size;
		else
		{
			for (uint32_t i = 0; i < this->value->AsArray()->size; i++)
			{
				filePos = this->pChildren[i]->GetByteSize(filePos);
			}
		}
	}
	else if (this->childrenCount > 0)
	{
		for (uint32_t i = 0; i < this->childrenCount; i++)
		{
			filePos = this->pChildren[i]->GetByteSize(filePos);
		}
	}
	if (doPadding)
		filePos = (filePos+3)&(~3);
	return filePos;
}

void ClearAssetTypeValueField(AssetTypeValueField *pValueField)
{
	free(pValueField);
}

ASSETSTOOLS_API bool AssetTypeValueField::IsDummy() const
{
	return (childrenCount == -1);
}

class _DummyAssetTypeField : public AssetTypeValueField
{
public:
	_DummyAssetTypeField()
	{
		this->pChildren = NULL;
		this->childrenCount = -1;
		this->templateField = NULL;
		this->value = NULL;
	}
};

_DummyAssetTypeField dummyAssetTypeField = _DummyAssetTypeField();
AssetTypeValueField* GetDummyAssetTypeField()
{
	return &dummyAssetTypeField;
}

EnumValueTypes GetValueTypeByTypeName(const char *type)
{
	EnumValueTypes ret = ValueType_None;
	if (!_stricmp(type, "string"))
	{
		ret = ValueType_String;
	}
	else if (!_stricmp(type, "SInt8") || !_stricmp(type, "char"))
	{
		ret = ValueType_Int8;
	}
	else if (!_stricmp(type, "UInt8") || !_stricmp(type, "unsigned char"))
	{
		ret = ValueType_UInt8;
	}
	else if (!_stricmp(type, "SInt16") || !_stricmp(type, "short"))
	{
		ret = ValueType_Int16;
	}
	else if (!_stricmp(type, "UInt16") || !_stricmp(type, "unsigned short"))
	{
		ret = ValueType_UInt16;
	}
	else if (!_stricmp(type, "SInt32") || !_stricmp(type, "int") ||  !_stricmp(type, "Type*"))
	{
		ret = ValueType_Int32;
	}
	else if (!_stricmp(type, "UInt32") || !_stricmp(type, "unsigned int"))
	{
		ret = ValueType_UInt32;
	}
	else if (!_stricmp(type, "SInt64") || !_stricmp(type, "long"))
	{
		ret = ValueType_Int64;
	}
	else if (!_stricmp(type, "UInt64") || !_stricmp(type, "FileSize") || !_stricmp(type, "unsigned long"))
	{
		ret = ValueType_UInt64;
	}
	else if (!_stricmp(type, "float"))
	{
		ret = ValueType_Float;
	}
	else if (!_stricmp(type, "double"))
	{
		ret = ValueType_Double;
	}
	else if (!_stricmp(type, "bool"))
	{
		ret = ValueType_Bool;
	}
	return ret;
}

int _RecursiveGetValueFieldCount(AssetTypeTemplateField *pChild, IAssetsReader *pReader, QWORD maxFilePos, QWORD *pFilePos, size_t *pValueByteLen, size_t *pChildListLen, size_t *pRawDataLen, bool *pReadFailed, bool endianness)
{
	QWORD filePos = *pFilePos;
	size_t valueByteLen = *pValueByteLen;
	size_t childListLen = *pChildListLen;
	size_t rawDataLen = *pRawDataLen;
	int ret = 0;
	if (!(*pReadFailed))
	{
		ret = 1;
		if (pChild->isArray && (pChild->children.size() == 2))
		{
			valueByteLen += sizeof(AssetTypeValue);
			unsigned int arrayLen;
			if ((pChild->children[0].valueType == ValueType_Int32) || (pChild->children[0].valueType == ValueType_UInt32))
			{
				pReader->Read(filePos, 4, &arrayLen); filePos += 4;
				if (endianness)
					SwapEndians_(arrayLen);
				if (!_stricmp(pChild->type.c_str(), "TypelessData"))
				{
					rawDataLen += arrayLen;
					filePos += arrayLen;
					if (filePos > maxFilePos)
					{
						*pReadFailed = true;
					}
				}
				else
				{
					childListLen += sizeof(AssetTypeValueField*) * arrayLen;
					for (uint32_t i = 0; i < arrayLen; i++)
					{
						ret += _RecursiveGetValueFieldCount(&pChild->children[1], pReader, maxFilePos, &filePos, &valueByteLen, &childListLen, &rawDataLen, pReadFailed, endianness);
						if ((*pReadFailed) || (filePos > maxFilePos))
						{
							*pReadFailed = true;
							break;
						}
					}
				}
				if (pChild->align)
					filePos = (filePos + 3) & (~3);
			}
			else
				assert(false);
			//else
			//	MessageBox(0, TEXT("Invalid array value type!"), TEXT("ERROR"), 16);
		}
		else if (pChild->valueType == ValueType_String)
		{
			unsigned int stringLen;
			pReader->Read(filePos, 4, &stringLen); filePos += 4;
			if (endianness)
				SwapEndians_(stringLen);
			if ((filePos + stringLen) > maxFilePos)
			{
				*pReadFailed = true;
			}
			else
			{
				filePos += stringLen;
				if (pChild->align || ((pChild->children.size() > 0) && pChild->children[0].align))
					filePos = (filePos+3)&(~3);
				valueByteLen += (sizeof(AssetTypeValue) + stringLen + 1);
			}
		}
		else if (pChild->children.empty())
		{
			switch (pChild->valueType)
			{
				case ValueType_Bool:
				case ValueType_Int8:
				case ValueType_UInt8:
					filePos++;
					break;
				case ValueType_Int16:
				case ValueType_UInt16:
					filePos+=2;
					break;
				case ValueType_Int32:
				case ValueType_UInt32:
				case ValueType_Float:
					filePos+=4;
					break;
				case ValueType_Int64:
				case ValueType_UInt64:
				case ValueType_Double:
					filePos+=8;
					break;
			}
			valueByteLen += sizeof(AssetTypeValue);
			if (pChild->align)
				filePos = (filePos+3)&(~3);
			if (filePos > maxFilePos)
			{
				*pReadFailed = true;
			}
		}
		else
		{
			childListLen += sizeof(AssetTypeValueField*) * pChild->children.size();
			for (uint32_t i = 0; i < (uint32_t)pChild->children.size(); i++)
			{
				ret += _RecursiveGetValueFieldCount(&pChild->children[i], pReader, maxFilePos, &filePos, &valueByteLen, &childListLen, &rawDataLen, pReadFailed, endianness);
			}
			if (pChild->align)
				filePos = (filePos+3)&(~3);
		}
	}
	*pRawDataLen = rawDataLen;
	*pChildListLen = childListLen;
	*pValueByteLen = valueByteLen;
	*pFilePos = filePos;
	return ret;
}
QWORD _RecursiveMakeValues(AssetTypeTemplateField *pTemplate, IAssetsReader *pReader, QWORD filePos, QWORD maxFilePos,
	AssetTypeValueField *pValueFields, uint32_t &valueFieldIndex, AssetTypeValue *&pCurValue, AssetTypeValueField**&pCurValueFieldList, 
	uint8_t *&pCurRawData, bool bigEndian)
{
	if (pTemplate->isArray)
	{
		if (pTemplate->children.size() == 2)
		{
			if ((pTemplate->children[0].valueType == ValueType_Int32) || (pTemplate->children[0].valueType == ValueType_UInt32))
			{
				unsigned int arrayLen;
				pReader->Read(filePos, 4, &arrayLen); filePos += 4;
				if (bigEndian)
					SwapEndians_(arrayLen);
				if (!_stricmp(pTemplate->type.c_str(), "TypelessData"))
				{
					AssetTypeByteArray _tmpArray;
					_tmpArray.size = arrayLen;
					_tmpArray.data = pCurRawData;
					pReader->Read(filePos, arrayLen, pCurRawData); filePos += arrayLen;
					if (filePos <= maxFilePos)
					{
						pCurRawData = &pCurRawData[arrayLen];
						AssetTypeValue _tmpValue = AssetTypeValue(ValueType_ByteArray, &_tmpArray);
						memcpy(pCurValue, &_tmpValue, sizeof(AssetTypeValue));
						pValueFields[valueFieldIndex].Read(pCurValue, pTemplate, 0, NULL);
						valueFieldIndex++;
						pCurValue = (AssetTypeValue*)((uintptr_t)pCurValue + sizeof(AssetTypeValue));
					}
				}
				else
				{
					AssetTypeArray _tmpArray;
					_tmpArray.size = arrayLen;
					//_tmpArray.dataField = &pValueFields[valueFieldIndex+1];

					AssetTypeValue _tmpValue = AssetTypeValue(ValueType_Array, &_tmpArray);
					memcpy(pCurValue, &_tmpValue, sizeof(AssetTypeValue));

					AssetTypeValueField** arrayItemList = pCurValueFieldList;
					pCurValueFieldList = (AssetTypeValueField**)((uintptr_t)pCurValueFieldList + (sizeof(AssetTypeValueField*) * arrayLen));
					uint32_t curValueFieldIndex = 0;

					pValueFields[valueFieldIndex].Read(pCurValue, pTemplate, arrayLen, arrayItemList);
					pCurValue = (AssetTypeValue*)((uintptr_t)pCurValue + sizeof(AssetTypeValue));
					valueFieldIndex++;
					for (uint32_t i = 0; i < arrayLen; i++)
					{
						arrayItemList[curValueFieldIndex] = &pValueFields[valueFieldIndex];
						filePos = _RecursiveMakeValues(&pTemplate->children[1], pReader, filePos, maxFilePos, pValueFields, valueFieldIndex, pCurValue, pCurValueFieldList, pCurRawData, bigEndian);
						curValueFieldIndex++;
						if (filePos > maxFilePos)
							break;
					}
				}
				if (pTemplate->align)
					filePos = (filePos + 3) & (~3);
			}
			else
				assert(false);
			//else
			//	MessageBox(0, TEXT("Invalid array value type!"), TEXT("ERROR"), 16);
		}
		else
			assert(false);
		//else
		//	MessageBox(0, TEXT("Invalid array!"), TEXT("ERROR"), 16);
	}
	else if (pTemplate->valueType == ValueType_String)
	{
		unsigned int stringLen;
		pReader->Read(filePos, 4, &stringLen); filePos += 4;
		if (bigEndian)
			SwapEndians_(stringLen);
		if ((filePos + stringLen) > maxFilePos)
			stringLen = (unsigned int)(maxFilePos - filePos);
		char *stringLocation = (char*)((uintptr_t)pCurValue + sizeof(AssetTypeValue)); stringLocation[stringLen] = 0;
		pReader->Read(filePos, stringLen, stringLocation); filePos += stringLen;
		AssetTypeValue _tmpValue = AssetTypeValue(ValueType_String, stringLocation);
		memcpy(pCurValue, &_tmpValue, sizeof(AssetTypeValue));
		pValueFields[valueFieldIndex].Read(pCurValue, pTemplate, 0, NULL);
		
		if (pTemplate->align || ((pTemplate->children.size() > 0) && pTemplate->children[0].align))
			filePos = (filePos+3)&(~3);
		valueFieldIndex++;
		pCurValue = (AssetTypeValue*)((uintptr_t)pCurValue + sizeof(AssetTypeValue) + stringLen + 1);
	}
	else if (pTemplate->children.empty())
	{
		char _valueContainer[8] = {0,0,0,0,0,0,0,0};
		char *valueContainer = _valueContainer;
		switch (pTemplate->valueType)
		{
			case ValueType_Bool:
			case ValueType_Int8:
			case ValueType_UInt8:
				pReader->Read(filePos, 1, valueContainer);
				filePos++;
				break;
			case ValueType_Int16:
			case ValueType_UInt16:
				pReader->Read(filePos, 2, valueContainer);
				if (bigEndian)
					SwapEndians_(*(uint16_t*)valueContainer);
				filePos+=2;
				break;
			case ValueType_Int32:
			case ValueType_UInt32:
			case ValueType_Float:
				pReader->Read(filePos, 4, valueContainer);
				if (bigEndian)
					SwapEndians_(*(uint32_t*)valueContainer);
				filePos+=4;
				break;
			case ValueType_Int64:
			case ValueType_UInt64:
			case ValueType_Double:
				pReader->Read(filePos, 8, valueContainer);
				if (bigEndian)
					SwapEndians_(*(QWORD*)valueContainer);
				filePos+=8;
				break;
			case ValueType_String:
				filePos = filePos;
				break;
		}
		if (pTemplate->align)
			filePos = (filePos+3)&(~3);
		if (filePos <= maxFilePos)
		{
			AssetTypeValue _tmpValue = AssetTypeValue(pTemplate->valueType, valueContainer);
			memcpy(pCurValue, &_tmpValue, sizeof(AssetTypeValue));
			pValueFields[valueFieldIndex].Read(pCurValue, pTemplate, 0, NULL);
			valueFieldIndex++;
			pCurValue = (AssetTypeValue*)((uintptr_t)pCurValue + sizeof(AssetTypeValue));
		}
	}
	else
	{
		AssetTypeValueField** templateChildList = pCurValueFieldList;
		pCurValueFieldList = (AssetTypeValueField**)((uintptr_t)pCurValueFieldList + (sizeof(AssetTypeValueField*) * pTemplate->children.size()));
		uint32_t curValueFieldIndex = 0;

		pValueFields[valueFieldIndex].Read(NULL, pTemplate, (uint32_t)pTemplate->children.size(), templateChildList);
		valueFieldIndex++;

		for (uint32_t i = 0; i < (uint32_t)pTemplate->children.size(); i++)
		{
			templateChildList[curValueFieldIndex] = &pValueFields[valueFieldIndex];
			filePos = _RecursiveMakeValues(&pTemplate->children[i], pReader, filePos, maxFilePos, pValueFields, valueFieldIndex, pCurValue, pCurValueFieldList, pCurRawData, bigEndian);
			curValueFieldIndex++;
		}
		if (pTemplate->align)
			filePos = (filePos+3)&(~3);
	}
	return filePos;
}

ASSETSTOOLS_API AssetTypeTemplateField::AssetTypeTemplateField()
	: valueType(ValueType_None),
	isArray(false), align(false), hasValue(false)
{}
ASSETSTOOLS_API void AssetTypeTemplateField::Clear()
{
	name.clear();
	type.clear();
	children.clear();
}
ASSETSTOOLS_API AssetTypeTemplateField::~AssetTypeTemplateField()
{}

ASSETSTOOLS_API QWORD AssetTypeTemplateField::MakeValue(IAssetsReader *pReader, QWORD filePos, QWORD fileLen, AssetTypeValueField **ppValueField, bool endianness)
{
	AssetTypeValue *newValue = NULL;
	QWORD tmpFilePos = filePos;
	size_t newValueByteLen = 0; size_t childListByteLen = 0; size_t rawDataByteLen = 0;
	//Set to true if it goes EOF while reading an array; This allows parsing empty files and having them filled with zeros without risking crashes on invalid files. 
	bool readFailed = false;
	int newChildrenCount = _RecursiveGetValueFieldCount(this, pReader, filePos+fileLen, &tmpFilePos, &newValueByteLen, &childListByteLen, &rawDataByteLen, &readFailed, endianness);
	//ppValueField will be set to pValueFieldMemory so the caller knows which pointer to free
	if (readFailed)
	{
		*ppValueField = NULL;
		return filePos;
	}
	void *pValueFieldMemory = malloc((newChildrenCount * sizeof(AssetTypeValueField)) + newValueByteLen + childListByteLen + rawDataByteLen);
	if (pValueFieldMemory == NULL)
	{
		*ppValueField = NULL;
		return filePos;
	}
	AssetTypeValueField *pValueFields = (AssetTypeValueField*)pValueFieldMemory;
	AssetTypeValue *pCurValue = (AssetTypeValue*)(&((uint8_t*)pValueFieldMemory)[newChildrenCount * sizeof(AssetTypeValueField)]);

	AssetTypeValueField **pCurValueList = (AssetTypeValueField**)(&((uint8_t*)pValueFieldMemory)[newChildrenCount * sizeof(AssetTypeValueField) + newValueByteLen]);
	uint8_t *pCurRawByte = (uint8_t*)(&((uint8_t*)pValueFieldMemory)[newChildrenCount * sizeof(AssetTypeValueField) + newValueByteLen + childListByteLen]);

	uint32_t valueFieldIndex = 0; 
	filePos = _RecursiveMakeValues(this, pReader, filePos, filePos+fileLen, pValueFields, valueFieldIndex, pCurValue, pCurValueList, pCurRawByte, endianness);
	//_RecursiveDumpValues(pValueFields, 0);
	*ppValueField = &pValueFields[0];
	return filePos;
}

ASSETSTOOLS_API bool AssetTypeTemplateField::From0D(Type_0D *pU5Type, uint32_t fieldIndex)
{
	if (pU5Type->typeFieldsExCount <= fieldIndex)
	{
		memset(this, 0, sizeof(AssetTypeTemplateField));
		return false;
	}
	TypeField_0D *pTypeField = &pU5Type->pTypeFieldsEx[fieldIndex];
	type = pTypeField->GetTypeString(pU5Type->pStringTable, pU5Type->stringTableLen);
	name = pTypeField->GetNameString(pU5Type->pStringTable, pU5Type->stringTableLen);
	if (!type.empty())
		valueType = GetValueTypeByTypeName(type.c_str());
	else
		valueType = ValueType_None;
	isArray = (pTypeField->isArray & 1) != 0;
	if (isArray)
		valueType = ValueType_Array;
	align = (pTypeField->flags & 0x4000) != 0;

	size_t newChildCount = 0; uint8_t directChildDepth = 0;
	for (uint32_t i = fieldIndex+1; i < pU5Type->typeFieldsExCount; i++)
	{
		if (pU5Type->pTypeFieldsEx[i].depth <= pTypeField->depth)
			break;
		if (!directChildDepth)
		{
			directChildDepth = pU5Type->pTypeFieldsEx[i].depth;
			newChildCount++;
		}
		else
		{
			if (pU5Type->pTypeFieldsEx[i].depth == directChildDepth)
				newChildCount++;
		}
	}
	hasValue = (newChildCount == 0);
	children.resize(newChildCount);
	size_t childIndex = 0; bool ret = true;
	for (uint32_t i = fieldIndex+1; i < pU5Type->typeFieldsExCount; i++)
	{
		if (pU5Type->pTypeFieldsEx[i].depth <= pTypeField->depth)
			break;
		if (pU5Type->pTypeFieldsEx[i].depth == directChildDepth)
		{
			if (!children[childIndex].From0D(pU5Type, i))
				ret = false;
			childIndex++;
		}
	}
	return ret;
}
ASSETSTOOLS_API bool AssetTypeTemplateField::FromClassDatabase(ClassDatabaseFile *pFile, ClassDatabaseType *pType, uint32_t fieldIndex)
{
	if (pType->fields.size() <= fieldIndex)
	{
		memset(this, 0, sizeof(AssetTypeTemplateField));
		return false;
	}
	children.clear();
	ClassDatabaseTypeField *pTypeField = &pType->fields[fieldIndex];
	isArray = (pTypeField->isArray & 1) != 0;
	name = pTypeField->fieldName.GetString(pFile);
	type = pTypeField->typeName.GetString(pFile);
	if (!type.empty())
		valueType = GetValueTypeByTypeName(type.c_str());
	else
		valueType = ValueType_None;
	align = (pTypeField->flags2 & 0x4000) != 0;

	size_t newChildCount = 0;
	uint8_t directChildDepth = 0;
	for (uint32_t i = fieldIndex+1; i < pType->fields.size(); i++)
	{
		if (pType->fields[i].depth <= pTypeField->depth)
			break;
		if (!directChildDepth)
		{
			directChildDepth = pType->fields[i].depth;
			newChildCount++;
		}
		else
		{
			if (pType->fields[i].depth == directChildDepth)
				newChildCount++;
		}
	}
	hasValue = (pType->fields.size() > (fieldIndex+1)) ? (newChildCount == 0) : true;
	children.resize(newChildCount);
	size_t childIndex = 0; bool ret = true;
	for (uint32_t i = fieldIndex+1; i < pType->fields.size(); i++)
	{
		if (pType->fields[i].depth <= pTypeField->depth)
			break;
		if (pType->fields[i].depth == directChildDepth)
		{
			if (!children[childIndex].FromClassDatabase(pFile, pType, i))
				ret = false;
			childIndex++;
		}
	}
	return ret;
}
ASSETSTOOLS_API bool AssetTypeTemplateField::From07(TypeField_07 *pTypeField)
{
	isArray = pTypeField->arrayFlag != 0;
	align = (pTypeField->flags2 & 0x4000) != NULL;
	name = pTypeField->name;
	type = pTypeField->type;
	if (!type.empty())
		valueType = GetValueTypeByTypeName(type.c_str());
	else
		valueType = ValueType_None;

	hasValue = (pTypeField->childrenCount == 0);
	children.resize(pTypeField->childrenCount);
	bool ret = true;
	for (uint32_t i = 0; i < pTypeField->childrenCount; i++)
	{
		if (!children[i].From07(&pTypeField->children[i]))
			ret = false;
	}
	return ret;
}
ASSETSTOOLS_API bool AssetTypeTemplateField::AddChildren(uint32_t count)
{
	if ((children.size() + count) < children.size()) //overflow
		return false;
	children.resize(children.size() + count);
	return true;
}
ASSETSTOOLS_API AssetTypeTemplateField *AssetTypeTemplateField::SearchChild(const char* name)
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i].name == name)
			return &children[i];
	}
	return NULL;
}

ASSETSTOOLS_API AssetTypeInstance::AssetTypeInstance(uint32_t baseFieldCount, AssetTypeTemplateField **ppBaseFields, QWORD fileLen, IAssetsReader *pReader, bool bigEndian, QWORD filePos)
{
	this->baseFields.resize(baseFieldCount);
	this->memoryToClear.resize(baseFieldCount);
	QWORD nullPos = filePos;
	for (uint32_t i = 0; i < baseFieldCount; i++)
	{
		filePos = ppBaseFields[i]->MakeValue(pReader, filePos, fileLen - (filePos - nullPos), &this->baseFields[i], bigEndian);
		this->memoryToClear[i] = this->baseFields[i];
		if (this->baseFields[i] == NULL)
		{
			this->baseFields.resize(i);
			this->memoryToClear.resize(i);
			break;
		}
	}
}
ASSETSTOOLS_API bool AssetTypeInstance::SetChildList(AssetTypeValueField *pValueField, AssetTypeValueField **pChildrenList, uint32_t childrenCount, bool freeMemory)
{
	if (pValueField->GetChildrenList() == pChildrenList)
	{
		if (pValueField->GetChildrenCount() == childrenCount)
			return true;
		pValueField->SetChildrenList(pChildrenList, childrenCount);
		return true;
	}
	for (size_t i = 0; i < this->memoryToClear.size(); i++)
	{
		if (this->memoryToClear[i] == pValueField->GetChildrenList())
		{
			for (uint32_t _i = 0; _i < pValueField->GetChildrenCount(); _i++)
			{
				bool found = false; AssetTypeValueField *pTarget = pValueField->Get(_i);
				for (uint32_t _k = 0; _k < childrenCount; _k++)
				{
					if (pTarget == pChildrenList[_k])
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					for (size_t k = 0; k < this->memoryToClear.size(); k++)
					{
						if (this->memoryToClear[k] == pTarget)
						{
							this->memoryToClear.erase(this->memoryToClear.begin() + k);
							break;
						}
					}
				}
			}
			free(this->memoryToClear[i]);
			pValueField->SetChildrenList(pChildrenList, childrenCount);
			if (freeMemory)
			{
				this->memoryToClear[i] = pChildrenList;
			}
			else
			{
				this->memoryToClear.erase(this->memoryToClear.begin() + i);
			}
			return true;
		}
	}
	pValueField->SetChildrenList(pChildrenList, childrenCount);
	if (freeMemory)
	{
		this->memoryToClear.push_back(pChildrenList);
	}
	return true;
}
ASSETSTOOLS_API bool AssetTypeInstance::AddTempMemory(void *pMemory)
{
	this->memoryToClear.push_back(pMemory);
	return true;
}
void AssetTypeInstance::Clear()
{
	for (size_t i = 0; i < this->memoryToClear.size(); i++)
	{
		if (this->memoryToClear[i])
			free(this->memoryToClear[i]);
	}
	this->memoryToClear.clear();
	this->baseFields.clear();
}

ASSETSTOOLS_API AssetTypeInstance::~AssetTypeInstance()
{
	Clear();
}