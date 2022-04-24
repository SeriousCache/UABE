#include "stdafx.h"
#include "../inc/half.hpp"
#include "TextureFileFormat.h"
#include <texgenpack.h>
#include <ispc_texcomp.h>
#include <astcenc.h>
#include <CrnlibWrap.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <assert.h>
#include <thread>

struct HalfFloat
{
	unsigned short half;
	//http://stackoverflow.com/questions/6162651/half-precision-floating-point-in-java/6162687#6162687
	inline float toFloat()
	{
		return half_float::detail::half2float(half);
	}
	inline void toHalf(float f)
	{
		half = half_float::detail::float2half<std::round_to_nearest>(f);
	}
};
struct RGB9e5Float
{
	unsigned __int32 value;
	inline void toRGB9e5(float in[3])
	{
		static const float highestFloat = 65408.0;

		float r = in[0];
		float g = in[1];
		float b = in[2];

		if (r <= 0.0) r = 0.0;
		else if (r >= highestFloat) r = highestFloat;

		if (g <= 0.0) g = 0.0;
		else if (g >= highestFloat) g = highestFloat;
		
		if (b <= 0.0) b = 0.0;
		else if (b >= highestFloat) b = highestFloat;

		float tempColor;
		if (r > g) tempColor = r;
		else tempColor = g;
		if (tempColor <= b) tempColor = b;
		
		int tempExponent = (int)( ((*((unsigned int*)(&tempColor)) >> 23) & 0xFF) - 127 );
		if (tempExponent < -16) tempExponent = -16;
		tempExponent += 16;
		int curExponentVal = tempExponent - 24;
		int exponentVal = curExponentVal;
		if (curExponentVal < 0) curExponentVal = -curExponentVal;
		if (curExponentVal < 0) curExponentVal = std::numeric_limits<int>::max();
		
		float factorA = 2.0F;
		float factorB = 1.0F;
		while (curExponentVal & 1)
		{
			if (curExponentVal & 1)
				factorB *= factorA;
			factorA *= factorA;
			curExponentVal >>= 1;
		}
		
		float factorC;
		if (exponentVal < 0)
			factorC = 1.0 / factorB;
		else
			factorC = factorB;

		tempColor /= factorC;
		if ( ((int)floor((double)tempColor + 0.5)) == 512 )
		{
			factorC *= 2.0;
			tempExponent++;
		}

		unsigned int newValue = tempExponent;
		newValue <<= 9;
		newValue |= ((unsigned int)floorf((b / factorC) + 0.5)) & 0x1FF;
		newValue <<= 9;
		newValue |= ((unsigned int)floorf((g / factorC) + 0.5)) & 0x1FF;
		newValue <<= 9;
		newValue |= ((unsigned int)floorf((r / factorC) + 0.5)) & 0x1FF;

		value = newValue;
	}
	inline void toFloat(float out[3])
	{
		unsigned int tempFloat_Int = ((value >> 4) & 0x0F800000) + 0x33800000;
		float exponentFloat = *((float*)&tempFloat_Int);
		out[0] = (float)(value & 0x1FF) * exponentFloat;
		out[1] = (float)((value >> 9) & 0x1FF) * exponentFloat;
		out[2] = (float)((value >> 18) & 0x1FF) * exponentFloat;
	}
};

enum TextureFileFields //enumeration for the array that stores the fields in ReadTextureFile
{
	//fields that aren't in all versions
	TextureFileField_MipCount,
	TextureFileField_MipMap,
	TextureFileField_ReadAllowed,
	//fields that always must be there
	TextureFileField_Name,
	TextureFileField_Width,
	TextureFileField_Height,
	TextureFileField_CompleteImageSize,
	TextureFileField_TextureFormat,
	TextureFileField_IsReadable,
	TextureFileField_ImageCount,
	TextureFileField_TextureDimension,
	TextureFileField_FilterMode,
	TextureFileField_Aniso,
	TextureFileField_MipBias,
	TextureFileField_WrapMode, //except me (since U2017.1), replaced by WrapU&V&W
	TextureFileField_LightmapFormat,
	TextureFileField_ColorSpace, //and me (before U3.5)
	TextureFileField_ImageData,
	//fields added in later versions
	TextureFileField_StreamingInfo_offset,
	TextureFileField_StreamingInfo_size,
	TextureFileField_StreamingInfo_path,
	TextureFileField_WrapU,
	TextureFileField_WrapV,
	TextureFileField_WrapW,
	TextureFileField_ForcedFallbackFormat,
	TextureFileField_DownscaleFallback,
	TextureFileField_StreamingMipmaps,
	TextureFileField_StreamingMipmapsPriority,
	TextureFileField_IgnoreMasterTextureLimit,
	TextureFileField_IsPreProcessed,
	TextureFileField_MipsStripped,
	TextureFileField_IsAlphaChannelOptional,
	TextureFileField_PlatformBlob,
	TextureFileField_Max
};
ASSETSTOOLS_API void PreprocessTextureTemplate(AssetTypeTemplateField& templateBase)
{
	if (AssetTypeTemplateField* pPlatformBlobField = templateBase.SearchChild("m_PlatformBlob"))
	{
		if (AssetTypeTemplateField* pArrayField = pPlatformBlobField->SearchChild("Array"))
			pArrayField->type = "TypelessData"; //Treat as byte array instead of generic array to lower the overhead.
	}
}
ASSETSTOOLS_API bool ReadTextureFile(TextureFile *pOutTex, AssetTypeValueField *pBaseField)
{
	//Last checked: 2021.2.17f1
	AssetTypeValueField *fields[TextureFileField_Max] = 
	{
		pBaseField->Get("m_MipCount"), //added in U5.2
		pBaseField->Get("m_MipMap"), //removed in U5.2
		pBaseField->Get("m_ReadAllowed"), //removed in U5.5

		pBaseField->Get("m_Name"),
		pBaseField->Get("m_Width"),
		pBaseField->Get("m_Height"),
		pBaseField->Get("m_CompleteImageSize"), //Since 2020.1: unsigned int, before: int
		pBaseField->Get("m_TextureFormat"),
		pBaseField->Get("m_IsReadable"),
		pBaseField->Get("m_ImageCount"),
		pBaseField->Get("m_TextureDimension"),
		pBaseField->Get("m_TextureSettings")->Get("m_FilterMode"),
		pBaseField->Get("m_TextureSettings")->Get("m_Aniso"),
		pBaseField->Get("m_TextureSettings")->Get("m_MipBias"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapMode"), //removed in U2017.1
		pBaseField->Get("m_LightmapFormat"),
		pBaseField->Get("m_ColorSpace"), //added in U3.5
		pBaseField->Get("image data"),

		//added in U5.3
		pBaseField->Get("m_StreamData")->Get("offset"), //Since 2020.1: UInt64, before: unsigned int
		pBaseField->Get("m_StreamData")->Get("size"),
		pBaseField->Get("m_StreamData")->Get("path"),
		//added in U2017.1
		pBaseField->Get("m_TextureSettings")->Get("m_WrapU"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapV"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapW"),
		//added in U2017.3
		pBaseField->Get("m_ForcedFallbackFormat"),
		pBaseField->Get("m_DownscaleFallback"),
		//added in U2018.2
		pBaseField->Get("m_StreamingMipmaps"),
		pBaseField->Get("m_StreamingMipmapsPriority"),
		//added in U2019.3
		pBaseField->Get("m_IgnoreMasterTextureLimit"), //bool
		//added in U2019.4
		pBaseField->Get("m_IsPreProcessed"), //bool
		//added in U2020.1
		pBaseField->Get("m_MipsStripped"), //int
		//added in U2020.2
		pBaseField->Get("m_IsAlphaChannelOptional"), //bool
		pBaseField->Get("m_PlatformBlob")->Get("Array"), //Array(UInt8) or TypelessData(UInt8) after PreprocessTextureTemplate.
	};
	for (int i = TextureFileField_Name; i < TextureFileField_StreamingInfo_offset; i++)
	{
		//color space added in U3.5, wrap mode removed in U2017.1
		if (fields[i]->IsDummy() && i != TextureFileField_ColorSpace && i != TextureFileField_WrapMode)
			return false;
	}
	if (fields[TextureFileField_StreamingInfo_offset]->IsDummy() != fields[TextureFileField_StreamingInfo_size]->IsDummy() ||
		fields[TextureFileField_StreamingInfo_size]->IsDummy() != fields[TextureFileField_StreamingInfo_path]->IsDummy())
		return false;
	if (fields[TextureFileField_WrapMode]->IsDummy() == (fields[TextureFileField_WrapU]->IsDummy() ||
		fields[TextureFileField_WrapV]->IsDummy() || fields[TextureFileField_WrapW]->IsDummy()))
		return false;
	if (!fields[TextureFileField_MipCount]->IsDummy())
	{
		pOutTex->m_MipCount = fields[TextureFileField_MipCount]->GetValue()->AsInt();
		pOutTex->m_MipMap = pOutTex->m_MipCount > 1;
	}
	else if (!fields[TextureFileField_MipMap]->IsDummy())
	{
		pOutTex->m_MipCount = -1; //unknown
		pOutTex->m_MipMap = fields[TextureFileField_MipMap]->GetValue()->AsBool();
	}
	else
		return false;
	pOutTex->m_Name = fields[TextureFileField_Name]->GetValue()->AsString();
	pOutTex->m_ForcedFallbackFormat = fields[TextureFileField_ForcedFallbackFormat]->IsDummy() 
		? 0 : fields[TextureFileField_ForcedFallbackFormat]->GetValue()->AsInt();
	pOutTex->m_DownscaleFallback = fields[TextureFileField_DownscaleFallback]->IsDummy() 
		? false : fields[TextureFileField_DownscaleFallback]->GetValue()->AsBool();
	pOutTex->m_IsAlphaChannelOptional = fields[TextureFileField_IsAlphaChannelOptional]->IsDummy()
		? false : fields[TextureFileField_IsAlphaChannelOptional]->GetValue()->AsBool();
	pOutTex->m_Width = fields[TextureFileField_Width]->GetValue()->AsUInt();
	pOutTex->m_Height = fields[TextureFileField_Height]->GetValue()->AsUInt();
	pOutTex->m_CompleteImageSize = fields[TextureFileField_CompleteImageSize]->GetValue()->AsUInt();
	pOutTex->m_MipsStripped = fields[TextureFileField_MipsStripped]->IsDummy()
		? 0 : fields[TextureFileField_MipsStripped]->GetValue()->AsInt();
	pOutTex->m_TextureFormat = fields[TextureFileField_TextureFormat]->GetValue()->AsUInt();
	pOutTex->m_IsReadable = fields[TextureFileField_IsReadable]->GetValue()->AsBool();
	pOutTex->m_IsPreProcessed = fields[TextureFileField_IsPreProcessed]->IsDummy()
		? 0 : fields[TextureFileField_IsPreProcessed]->GetValue()->AsBool();
	pOutTex->m_ReadAllowed = fields[TextureFileField_ReadAllowed]->IsDummy()
		? true : fields[TextureFileField_ReadAllowed]->GetValue()->AsBool();
	pOutTex->m_IgnoreMasterTextureLimit = fields[TextureFileField_IgnoreMasterTextureLimit]->IsDummy()
		? 0 : fields[TextureFileField_IgnoreMasterTextureLimit]->GetValue()->AsBool();
	pOutTex->m_StreamingMipmaps = fields[TextureFileField_StreamingMipmaps]->IsDummy()
		? true : fields[TextureFileField_StreamingMipmaps]->GetValue()->AsBool();
	pOutTex->m_StreamingMipmapsPriority = fields[TextureFileField_StreamingMipmapsPriority]->IsDummy()
		? 0 : fields[TextureFileField_StreamingMipmapsPriority]->GetValue()->AsInt();
	pOutTex->m_ImageCount = fields[TextureFileField_ImageCount]->GetValue()->AsInt();
	pOutTex->m_TextureDimension = fields[TextureFileField_TextureDimension]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_FilterMode = fields[TextureFileField_FilterMode]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_Aniso = fields[TextureFileField_Aniso]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_MipBias = fields[TextureFileField_MipBias]->GetValue()->AsFloat();
	pOutTex->m_TextureSettings.m_WrapMode = fields[TextureFileField_WrapMode]->IsDummy()
		? 0 : fields[TextureFileField_WrapMode]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_WrapU = fields[TextureFileField_WrapU]->IsDummy()
		? 0 : fields[TextureFileField_WrapU]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_WrapV = fields[TextureFileField_WrapV]->IsDummy()
		? 0 : fields[TextureFileField_WrapV]->GetValue()->AsInt();
	pOutTex->m_TextureSettings.m_WrapW = fields[TextureFileField_WrapW]->IsDummy()
		? 0 : fields[TextureFileField_WrapW]->GetValue()->AsInt();
	pOutTex->m_LightmapFormat = fields[TextureFileField_LightmapFormat]->GetValue()->AsInt();
	if (!fields[TextureFileField_ColorSpace]->IsDummy())
		pOutTex->m_ColorSpace = fields[TextureFileField_ColorSpace]->GetValue()->AsInt();
	else
		pOutTex->m_ColorSpace = 0;
	pOutTex->m_PlatformBlob.clear();
	if (!fields[TextureFileField_PlatformBlob]->IsDummy())
	{
		if (auto* pByteArray = fields[TextureFileField_PlatformBlob]->GetValue()->AsByteArray())
			pOutTex->m_PlatformBlob.assign(&pByteArray->data[0], &pByteArray->data[pByteArray->size]);
		else if (auto* pArray = fields[TextureFileField_PlatformBlob]->GetValue()->AsArray())
		{
			if (pArray->size == fields[TextureFileField_PlatformBlob]->GetChildrenCount())
			{
				pOutTex->m_PlatformBlob.resize(pArray->size);
				for (unsigned int i = 0; i < pArray->size; ++i)
				{
					auto *pCurByteField = fields[TextureFileField_PlatformBlob]->Get(i);
					if (pCurByteField->GetValue() && pCurByteField->GetValue()->GetType() == ValueType_UInt8)
						pOutTex->m_PlatformBlob[i] = (uint8_t)pCurByteField->GetValue()->AsUInt();
				}
			}
			else
				assert(false);
		}
	}
	pOutTex->_pictureDataSize = fields[TextureFileField_ImageData]->GetValue()->AsByteArray()->size;
	pOutTex->pPictureData = fields[TextureFileField_ImageData]->GetValue()->AsByteArray()->data;
	memcpy(pOutTex->pPictureData, pOutTex->pPictureData, pOutTex->_pictureDataSize);
	if (!fields[TextureFileField_StreamingInfo_offset]->IsDummy())
	{
		pOutTex->m_StreamData.offset = fields[TextureFileField_StreamingInfo_offset]->GetValue()->AsUInt64();
		pOutTex->m_StreamData.size = fields[TextureFileField_StreamingInfo_size]->GetValue()->AsUInt();
		pOutTex->m_StreamData.path = fields[TextureFileField_StreamingInfo_path]->GetValue()->AsString();
	}
	else
	{
		pOutTex->m_StreamData.offset = pOutTex->m_StreamData.size = 0;
		pOutTex->m_StreamData.path.clear();
	}
	pOutTex->extra.textureFormatVersion = 0;
	return true;
}
ASSETSTOOLS_API bool WriteTextureFile(TextureFile *pInTex, AssetTypeValueField *pBaseField,
	std::vector<std::unique_ptr<uint8_t[]>>& allocatedMemory)
{
	AssetTypeValueField *fields[] = 
	{
		pBaseField->Get("m_MipCount"), //added in U5.2
		pBaseField->Get("m_MipMap"), //not in U5.2
		pBaseField->Get("m_ReadAllowed"), //removed in U5.5

		pBaseField->Get("m_Name"),
		pBaseField->Get("m_Width"),
		pBaseField->Get("m_Height"),
		pBaseField->Get("m_CompleteImageSize"),
		pBaseField->Get("m_TextureFormat"),
		pBaseField->Get("m_IsReadable"),
		pBaseField->Get("m_ImageCount"),
		pBaseField->Get("m_TextureDimension"),
		pBaseField->Get("m_TextureSettings")->Get("m_FilterMode"),
		pBaseField->Get("m_TextureSettings")->Get("m_Aniso"),
		pBaseField->Get("m_TextureSettings")->Get("m_MipBias"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapMode"), //removed in U2017.1
		pBaseField->Get("m_LightmapFormat"),
		pBaseField->Get("m_ColorSpace"), //added in U3.5
		pBaseField->Get("image data"),

		//added in U5.3
		pBaseField->Get("m_StreamData")->Get("offset"), 
		pBaseField->Get("m_StreamData")->Get("size"),
		pBaseField->Get("m_StreamData")->Get("path"),
		//added in U2017.1
		pBaseField->Get("m_TextureSettings")->Get("m_WrapU"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapV"),
		pBaseField->Get("m_TextureSettings")->Get("m_WrapW"),
		//added in U2017.3
		pBaseField->Get("m_ForcedFallbackFormat"),
		pBaseField->Get("m_DownscaleFallback"),
		//added in U2018.2
		pBaseField->Get("m_StreamingMipmaps"),
		pBaseField->Get("m_StreamingMipmapsPriority"),
		//added in U2019.3
		pBaseField->Get("m_IgnoreMasterTextureLimit"), //bool
		//added in U2019.4
		pBaseField->Get("m_IsPreProcessed"), //bool
		//added in U2020.1
		pBaseField->Get("m_MipsStripped"), //int
		//added in U2020.2
		pBaseField->Get("m_IsAlphaChannelOptional"), //bool
		pBaseField->Get("m_PlatformBlob")->Get("Array"), //Array(UInt8) or TypelessData(UInt8) after PreprocessTextureTemplate.
	};
	for (int i = TextureFileField_Name; i < TextureFileField_StreamingInfo_offset; i++)
	{
		//color space added in U3.5, wrap mode removed in U2017.1
		if (fields[i]->IsDummy() && i != TextureFileField_ColorSpace && i != TextureFileField_WrapMode)
			return false;
	}
	if (fields[TextureFileField_StreamingInfo_offset]->IsDummy() != fields[TextureFileField_StreamingInfo_size]->IsDummy() ||
		fields[TextureFileField_StreamingInfo_size]->IsDummy() != fields[TextureFileField_StreamingInfo_path]->IsDummy())
		return false;
	if (fields[TextureFileField_WrapMode]->IsDummy() == (fields[TextureFileField_WrapU]->IsDummy() ||
		fields[TextureFileField_WrapV]->IsDummy() || fields[TextureFileField_WrapW]->IsDummy()))
		return false;
	if (fields[TextureFileField_StreamingMipmaps]->IsDummy() != fields[TextureFileField_StreamingMipmapsPriority]->IsDummy())
		return false;
	if (!fields[TextureFileField_MipCount]->IsDummy())
	{
		fields[TextureFileField_MipCount]->GetValue()->Set(&pInTex->m_MipCount, ValueType_Int32);
	}
	else if (!fields[TextureFileField_MipMap]->IsDummy())
	{
		fields[TextureFileField_MipMap]->GetValue()->Set(&pInTex->m_MipMap, ValueType_Bool);
	}
	else
		return false;

	fields[TextureFileField_Name]->GetValue()->Set(const_cast<char*>(pInTex->m_Name.c_str()));

	if (!fields[TextureFileField_ForcedFallbackFormat]->IsDummy())
		fields[TextureFileField_ForcedFallbackFormat]->GetValue()->Set(&pInTex->m_ForcedFallbackFormat, ValueType_Int32);
	if (!fields[TextureFileField_DownscaleFallback]->IsDummy())
		fields[TextureFileField_DownscaleFallback]->GetValue()->Set(&pInTex->m_DownscaleFallback, ValueType_Bool);
	if (!fields[TextureFileField_IsAlphaChannelOptional]->IsDummy())
		fields[TextureFileField_IsAlphaChannelOptional]->GetValue()->Set(&pInTex->m_IsAlphaChannelOptional, ValueType_Bool);

	fields[TextureFileField_Width]->GetValue()->Set(&pInTex->m_Width, ValueType_UInt32);
	fields[TextureFileField_Height]->GetValue()->Set(&pInTex->m_Height, ValueType_UInt32);
	fields[TextureFileField_CompleteImageSize]->GetValue()->Set(&pInTex->m_CompleteImageSize, ValueType_UInt32);
	if (!fields[TextureFileField_MipsStripped]->IsDummy())
		fields[TextureFileField_MipsStripped]->GetValue()->Set(&pInTex->m_MipsStripped, ValueType_Int32);
	fields[TextureFileField_TextureFormat]->GetValue()->Set(&pInTex->m_TextureFormat, ValueType_UInt32);
	fields[TextureFileField_IsReadable]->GetValue()->Set(&pInTex->m_IsReadable, ValueType_Bool);
	if (!fields[TextureFileField_IsPreProcessed]->IsDummy())
		fields[TextureFileField_IsPreProcessed]->GetValue()->Set(&pInTex->m_IsPreProcessed, ValueType_Bool);
	if (!fields[TextureFileField_ReadAllowed]->IsDummy())
		fields[TextureFileField_ReadAllowed]->GetValue()->Set(&pInTex->m_ReadAllowed, ValueType_Bool);
	if (!fields[TextureFileField_IgnoreMasterTextureLimit]->IsDummy())
		fields[TextureFileField_IgnoreMasterTextureLimit]->GetValue()->Set(&pInTex->m_IgnoreMasterTextureLimit, ValueType_Bool);
	if (!fields[TextureFileField_StreamingMipmaps]->IsDummy())
	{
		fields[TextureFileField_StreamingMipmaps]->GetValue()->Set(&pInTex->m_StreamingMipmaps, ValueType_Bool);
		fields[TextureFileField_StreamingMipmapsPriority]->GetValue()->Set(&pInTex->m_StreamingMipmapsPriority, ValueType_Int32);
	}
	fields[TextureFileField_ImageCount]->GetValue()->Set(&pInTex->m_ImageCount, ValueType_Int32);
	fields[TextureFileField_TextureDimension]->GetValue()->Set(&pInTex->m_TextureDimension, ValueType_Int32);
	fields[TextureFileField_FilterMode]->GetValue()->Set(&pInTex->m_TextureSettings.m_FilterMode, ValueType_Int32);
	fields[TextureFileField_Aniso]->GetValue()->Set(&pInTex->m_TextureSettings.m_Aniso, ValueType_Int32);
	fields[TextureFileField_MipBias]->GetValue()->Set(&pInTex->m_TextureSettings.m_MipBias, ValueType_Float);
	if (!fields[TextureFileField_WrapMode]->IsDummy())
		fields[TextureFileField_WrapMode]->GetValue()->Set(&pInTex->m_TextureSettings.m_WrapMode, ValueType_Int32);
	else
	{
		fields[TextureFileField_WrapU]->GetValue()->Set(&pInTex->m_TextureSettings.m_WrapU, ValueType_Int32);
		fields[TextureFileField_WrapV]->GetValue()->Set(&pInTex->m_TextureSettings.m_WrapV, ValueType_Int32);
		fields[TextureFileField_WrapW]->GetValue()->Set(&pInTex->m_TextureSettings.m_WrapW, ValueType_Int32);
	}
	fields[TextureFileField_LightmapFormat]->GetValue()->Set(&pInTex->m_LightmapFormat, ValueType_Int32);
	if (!fields[TextureFileField_ColorSpace]->IsDummy()) //color space added in U3.5
		fields[TextureFileField_ColorSpace]->GetValue()->Set(&pInTex->m_ColorSpace, ValueType_Int32);
	if (!fields[TextureFileField_PlatformBlob]->IsDummy())
	{
		//Create an AssetTypeValue of type ByteArray.
		uint8_t* valueMem = new uint8_t[sizeof(AssetTypeValue)];
		allocatedMemory.emplace_back(valueMem);
		AssetTypeValue* pNewValue = (AssetTypeValue*)valueMem;
		AssetTypeByteArray byteArray = {};
		byteArray.data = pInTex->m_PlatformBlob.data();
		byteArray.size = (uint32_t)std::min<size_t>(pInTex->m_PlatformBlob.size(), std::numeric_limits<uint32_t>::max());
		*pNewValue = AssetTypeValue(ValueType_ByteArray, &byteArray);
		//Assign it to the field.
		fields[TextureFileField_PlatformBlob]->Read(pNewValue,
			fields[TextureFileField_PlatformBlob]->GetTemplateField(),
			0, nullptr);
	}
	AssetTypeByteArray byteArray;
	byteArray.size = pInTex->_pictureDataSize;
	byteArray.data = pInTex->pPictureData;
	fields[TextureFileField_ImageData]->GetValue()->Set(&byteArray, ValueType_ByteArray);
	if (!fields[TextureFileField_StreamingInfo_offset]->IsDummy())
	{
		fields[TextureFileField_StreamingInfo_offset]->GetValue()->Set(&pInTex->m_StreamData.offset, ValueType_UInt64);
		fields[TextureFileField_StreamingInfo_size]->GetValue()->Set(&pInTex->m_StreamData.size, ValueType_UInt32);
		fields[TextureFileField_StreamingInfo_path]->GetValue()->Set(const_cast<char*>(pInTex->m_StreamData.path.c_str()), ValueType_String);
	}
	return true;
}
#include "EngineVersion.h"
bool SupportsTextureFormat(AssetsFile *pAssetsFile, TextureFormat texFmt, int &version)
{
	EngineVersion engineVersion = EngineVersion::parse(pAssetsFile->typeTree.unityVersion);
	if (engineVersion.year >= 2019
		|| pAssetsFile->header.format > 19) //>= 2019.1
		version = 2;
	else if ((engineVersion.year == 2017 && engineVersion.release >= 3) || engineVersion.year > 2017
		|| pAssetsFile->header.format > 17) //>= 2017.3
		version = 1;
	else
		version = 0;

	switch (texFmt)
	{
		case TexFmt_Alpha8:
		case TexFmt_ARGB4444:
		case TexFmt_RGB24:
		case TexFmt_RGBA32:
		case TexFmt_ARGB32:
		case TexFmt_RGB565:
		case TexFmt_DXT1:
			return true;
		case TexFmt_DXT5:
			return engineVersion.year >= 2; //actually excludes Unity 2 (up to format 6) because Unity 3.0 (format 8) is the first one with a version name embedded
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA4:
			return engineVersion.year > 2 || (engineVersion.year == 2 && engineVersion.release >= 6); //same as above
		case TexFmt_ETC_RGB4:
			return engineVersion.year >= 3;
		case TexFmt_ATC_RGB4:
		case TexFmt_ATC_RGBA8:
			return (engineVersion.year >= 4 && engineVersion.year <= 2017) || (engineVersion.year == 3 && engineVersion.release >= 4);
		case TexFmt_BGRA32Old:
			return (engineVersion.year == 4 && engineVersion.release < 5) || (engineVersion.year == 3 && engineVersion.release >= 4);
		case TexFmt_UNUSED38:
		case TexFmt_UNUSED39:
		case TexFmt_UNUSED40:
			return engineVersion.year == 4 || (engineVersion.year == 3 && engineVersion.release >= 5);
		case TexFmt_RGBA4444:
			return engineVersion.year > 4 || (engineVersion.year == 4 && engineVersion.release >= 1);
		case TexFmt_BGRA32New:
		case TexFmt_EAC_R:
		case TexFmt_EAC_R_SIGNED:
		case TexFmt_EAC_RG:
		case TexFmt_EAC_RG_SIGNED:
		case TexFmt_ETC2_RGB4:
		case TexFmt_ETC2_RGBA1:
		case TexFmt_ETC2_RGBA8:
		case TexFmt_ASTC_RGB_4x4:  //version >= 2: TexFmt_ASTC_4x4
		case TexFmt_ASTC_RGB_5x5:  //version >= 2: TexFmt_ASTC_5x5
		case TexFmt_ASTC_RGB_6x6:  //version >= 2: TexFmt_ASTC_6x6
		case TexFmt_ASTC_RGB_8x8:  //version >= 2: TexFmt_ASTC_8x8
		case TexFmt_ASTC_RGB_10x10://version >= 2: TexFmt_ASTC_10x10
		case TexFmt_ASTC_RGB_12x12://version >= 2: TexFmt_ASTC_12x12
			return engineVersion.year > 4 || (engineVersion.year == 4 && engineVersion.release >= 5);
		case TexFmt_ASTC_RGBA_4x4:
		case TexFmt_ASTC_RGBA_5x5:
		case TexFmt_ASTC_RGBA_6x6:
		case TexFmt_ASTC_RGBA_8x8:
		case TexFmt_ASTC_RGBA_10x10:
		case TexFmt_ASTC_RGBA_12x12:
			return (engineVersion.year > 4 || (engineVersion.year == 4 && engineVersion.release >= 5)) && engineVersion.year < 2019;
		case TexFmt_R16:
		case TexFmt_RHalf:
		case TexFmt_RGHalf:
		case TexFmt_RGBAHalf:
		case TexFmt_RFloat:
		case TexFmt_RGFloat:
		case TexFmt_RGBAFloat:
		case TexFmt_YUV2:
		case TexFmt_DXT1Crunched:
		case TexFmt_DXT5Crunched:
			return engineVersion.year >= 5;
		case TexFmt_ETC_RGB4_3DS:
		case TexFmt_ETC_RGBA8_3DS:
			return engineVersion.year >= 5 && (engineVersion.year < 2018 || (engineVersion.year == 2018 && engineVersion.release < 3));
		case TexFmt_BC6H:
		case TexFmt_BC7:
		case TexFmt_BC4:
		case TexFmt_BC5:
			return engineVersion.year > 5 || (engineVersion.year == 5 && engineVersion.release >= 5);
		case TexFmt_RGB9e5Float:
			return engineVersion.year > 5 || (engineVersion.year == 5 && engineVersion.release >= 6);
		case TexFmt_RG16:
		case TexFmt_R8:
			return engineVersion.year > 2017 || (engineVersion.year == 2017 && engineVersion.release >= 1);
		case TexFmt_ETC_RGB4Crunched:
		case TexFmt_ETC2_RGBA8Crunched:
			return engineVersion.year > 2017 || (engineVersion.year == 2017 && engineVersion.release >= 3);
		case TexFmt_ASTC_HDR_4x4:
		case TexFmt_ASTC_HDR_5x5:
		case TexFmt_ASTC_HDR_6x6:
		case TexFmt_ASTC_HDR_8x8:
		case TexFmt_ASTC_HDR_10x10:
		case TexFmt_ASTC_HDR_12x12:
			return engineVersion.year >= 2019;
		case TexFmt_RG32:
		case TexFmt_RGB48:
		case TexFmt_RGBA64:
			return engineVersion.year > 2020 || (engineVersion.year == 2020 && engineVersion.release >= 2);
		default:
			return false;
	}
}

#include <squish.h>

#define _24to32(in) (in + 0x000000FF)
void Write24BitTo(uint32_t in, void *buf)
{
	((uint8_t*)buf)[2] = (uint8_t)((in & 0xFF000000) >> 24);
	((uint8_t*)buf)[1] = (uint8_t)((in & 0x00FF0000) >> 16);
	((uint8_t*)buf)[0] = (uint8_t)((in & 0x0000FF00) >> 8);
}
void Write24To32BitTo(uint32_t in, void *buf)
{
	((uint32_t*)buf)[0] = (in & 0xFFFFFF00) + 0x000000FF;
}
void Write32BitTo(uint32_t in, void *buf)
{
	((uint32_t*)buf)[0] = in;
}
typedef void(__cdecl *_WritePixelFormatTo)(uint32_t, void*);

struct UncompressedColorChannel //not more than 8bit per channel
{
	uint32_t mask; //mask of the color channel in the byte
	char shift; //amount of bits to lshift to compress = amount of bits to rshift to decompress
};
struct UncompressedTextureFormat
{
	UncompressedColorChannel red, green, blue, alpha;
};

bool Uncompressed_ToRGBA32(TextureFile *pTex, void *pOutBuf)
{
	switch (pTex->m_TextureFormat)
	{
		case TexFmt_RGB24:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 3))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = pTex->pPictureData[i * 3 + 0]; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = pTex->pPictureData[i * 3 + 1]; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = pTex->pPictureData[i * 3 + 2]; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGBA32:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			memcpy(pOutBuf, pTex->pPictureData, (pTex->m_Width * pTex->m_Height) * 4);
		}
		break;
		case TexFmt_BGRA32Old:
		case TexFmt_BGRA32New:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = pTex->pPictureData[i * 4 + 2]; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = pTex->pPictureData[i * 4 + 1]; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = pTex->pPictureData[i * 4 + 0]; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = pTex->pPictureData[i * 4 + 3]; //A
			}
		}
		break;
		case TexFmt_ARGB32:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = pTex->pPictureData[i * 4 + 1]; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = pTex->pPictureData[i * 4 + 2]; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = pTex->pPictureData[i * 4 + 3]; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = pTex->pPictureData[i * 4 + 0]; //A
			}
		}
		break;
		case TexFmt_ARGB4444:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			//GBAR
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 1] = (pTex->pPictureData[i * 2] & 0xF0) | (pTex->pPictureData[i * 2] >> 4); //| 0x0F; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = ((pTex->pPictureData[i * 2] & 0x0F) << 4) | (pTex->pPictureData[i * 2] & 0x0F); //| 0x0F; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = (pTex->pPictureData[i * 2 + 1] & 0xF0) | (pTex->pPictureData[i * 2 + 1] >> 4); //| 0x0F; //A
				((uint8_t*)pOutBuf)[i * 4 + 0] = ((pTex->pPictureData[i * 2 + 1] & 0x0F) << 4) | (pTex->pPictureData[i * 2 + 1] & 0x0F); //| 0x0F; //R
			}
		}
		break;
		case TexFmt_RGBA4444:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			//BARG
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 2] = (pTex->pPictureData[i * 2] & 0xF0) | (pTex->pPictureData[i * 2] >> 4); //| 0x0F; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = ((pTex->pPictureData[i * 2] & 0x0F) << 4) | (pTex->pPictureData[i * 2] & 0x0F); //| 0x0F; //A
				((uint8_t*)pOutBuf)[i * 4 + 0] = (pTex->pPictureData[i * 2 + 1] & 0xF0) | (pTex->pPictureData[i * 2 + 1] >> 4); //| 0x0F; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = ((pTex->pPictureData[i * 2 + 1] & 0x0F) << 4) | (pTex->pPictureData[i * 2 + 1] & 0x0F); //| 0x0F; //G
			}
		}
		break;
		case TexFmt_RGB565:
			//the R/G/B values won't be changed, so colors will look different in 8 bits per color 
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			//g3(low)b5g3(high)r5
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				uint16_t rgb565 = *(uint16_t*)(&pTex->pPictureData[i * 2]);
				//rgb565 = ((rgb565 & 0xFF00) >> 8) | ((rgb565 & 0x00FF) << 8); 
				uint8_t r5 = (rgb565 >> 11) & 31;
				uint8_t g6 = (rgb565 >> 5) & 63;
				uint8_t b5 = (rgb565) & 31;

				//multiply by 17 -> for maximum 5bit red we get maximum 8bit red, for minimum 5bit red we get minimum 8bit red
				((uint8_t*)pOutBuf)[i * 4 + 0] = (r5 << 3) | (r5 & 7);// | 7; 
				((uint8_t*)pOutBuf)[i * 4 + 1] = (g6 << 2) | (g6 & 3);// | 3;
				((uint8_t*)pOutBuf)[i * 4 + 2] = (b5 << 3) | (b5 & 7);// | 7;
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_Alpha8:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 1))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = 255;
				((uint8_t*)pOutBuf)[i * 4 + 1] = 255;
				((uint8_t*)pOutBuf)[i * 4 + 2] = 255;
				((uint8_t*)pOutBuf)[i * 4 + 3] = pTex->pPictureData[i]; //A
			}
		}
		break;
		case TexFmt_RHalf:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float red = ((HalfFloat*)&pTex->pPictureData[i * 2])->toFloat();

				//Cut off values outside the SDR range.
				if (red >= 1.0F) red = 1.0F;
				else if (red <= 0.0F) red = 0.0F;

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(red * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = 0; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGHalf:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float colors[2];
				colors[0] = ((HalfFloat*)&pTex->pPictureData[i * 4])->toFloat();
				colors[1] = ((HalfFloat*)&pTex->pPictureData[i * 4 + 2])->toFloat();

				//Cut off values outside the SDR range.
				for (unsigned int j = 0; j < 2; j++)
				{
					if (colors[j] >= 1.0F) colors[j] = 1.0F;
					else if (colors[j] <= 0.0F) colors[j] = 0.0F;
				}

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(colors[0] * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (uint8_t)(colors[1] * 255.0F); //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGBAHalf:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 8))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float colors[4];
				colors[0] = ((HalfFloat*)&pTex->pPictureData[i * 8])->toFloat();
				colors[1] = ((HalfFloat*)&pTex->pPictureData[i * 8 + 2])->toFloat();
				colors[2] = ((HalfFloat*)&pTex->pPictureData[i * 8 + 4])->toFloat();
				colors[3] = ((HalfFloat*)&pTex->pPictureData[i * 8 + 6])->toFloat();

				//Cut off values outside the SDR range.
				for (unsigned int j = 0; j < 4; j++)
				{
					if (colors[j] >= 1.0F) colors[j] = 1.0F;
					else if (colors[j] <= 0.0F) colors[j] = 0.0F;
				}

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(colors[0] * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (uint8_t)(colors[1] * 255.0F); //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = (uint8_t)(colors[2] * 255.0F); //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = (uint8_t)(colors[3] * 255.0F); //A
			}
		}
		break;
		case TexFmt_RFloat:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float red = *(float*)&pTex->pPictureData[i * 4];

				//Cut off values outside the SDR range.
				if (red >= 1.0F) red = 1.0F;
				else if (red <= 0.0F) red = 0.0F;

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(red * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = 0; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGFloat:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 8))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float colors[2];
				colors[0] = *(float*)&pTex->pPictureData[i * 8]; //R
				colors[1] = *(float*)&pTex->pPictureData[i * 8 + 4]; //G

				//Cut off values outside the SDR range.
				for (unsigned int j = 0; j < 2; j++)
				{
					if (colors[j] >= 1.0F) colors[j] = 1.0F;
					else if (colors[j] <= 0.0F) colors[j] = 0.0F;
				}

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(colors[0] * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (uint8_t)(colors[1] * 255.0F); //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGBAFloat:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 16))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				float colors[4];
				colors[0] = *(float*)&pTex->pPictureData[i * 16]; //R
				colors[1] = *(float*)&pTex->pPictureData[i * 16 + 4]; //G
				colors[2] = *(float*)&pTex->pPictureData[i * 16 + 8]; //B
				colors[3] = *(float*)&pTex->pPictureData[i * 16 + 12]; //A

				//Cut off values outside the SDR range.
				for (unsigned int j = 0; j < 4; j++)
				{
					if (colors[j] >= 1.0F) colors[j] = 1.0F;
					else if (colors[j] <= 0.0F) colors[j] = 0.0F;
				}

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(colors[0] * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (uint8_t)(colors[1] * 255.0F); //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = (uint8_t)(colors[2] * 255.0F); //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = (uint8_t)(colors[3] * 255.0F); //A
			}
		}
		break;
		case TexFmt_RGB9e5Float:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				RGB9e5Float rgbFloat;
				rgbFloat.value = *(unsigned int*)&pTex->pPictureData[i * 4];
				float colors[3] = {};
				rgbFloat.toFloat(colors);

				//Cut off values outside the SDR range.
				for (unsigned int j = 0; j < 3; j++)
				{
					if (colors[j] >= 1.0F) colors[j] = 1.0F;
					else if (colors[j] <= 0.0F) colors[j] = 0.0F;
				}

				((uint8_t*)pOutBuf)[i * 4 + 0] = (uint8_t)(colors[0] * 255.0F); //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (uint8_t)(colors[1] * 255.0F); //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = (uint8_t)(colors[2] * 255.0F); //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_R8:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 1))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = pTex->pPictureData[i]; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = 0; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_R16:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = (*(uint16_t*)&pTex->pPictureData[i*2]) >> 8; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = 0; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RG16:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 2))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = pTex->pPictureData[i * 2]; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = pTex->pPictureData[i * 2 + 1]; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RG32:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 4))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = (*(uint16_t*)&pTex->pPictureData[i * 4 + 0]) >> 8; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (*(uint16_t*)&pTex->pPictureData[i * 4 + 2]) >> 8; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = 0; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGB48:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 6))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = (*(uint16_t*)&pTex->pPictureData[i * 6 + 0]) >> 8; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (*(uint16_t*)&pTex->pPictureData[i * 6 + 2]) >> 8; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = (*(uint16_t*)&pTex->pPictureData[i * 6 + 4]) >> 8; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = 255; //A
			}
		}
		break;
		case TexFmt_RGBA64:
		{
			if (pTex->_pictureDataSize < (pTex->m_Width * pTex->m_Height * 8))
				return false;
			for (unsigned int i = 0; i < (pTex->m_Width * pTex->m_Height); i++)
			{
				((uint8_t*)pOutBuf)[i * 4 + 0] = (*(uint16_t*)&pTex->pPictureData[i * 8 + 0]) >> 8; //R
				((uint8_t*)pOutBuf)[i * 4 + 1] = (*(uint16_t*)&pTex->pPictureData[i * 8 + 2]) >> 8; //G
				((uint8_t*)pOutBuf)[i * 4 + 2] = (*(uint16_t*)&pTex->pPictureData[i * 8 + 4]) >> 8; //B
				((uint8_t*)pOutBuf)[i * 4 + 3] = (*(uint16_t*)&pTex->pPictureData[i * 8 + 6]) >> 8; //A
			}
		}
		break;
		default:
			return false;
	}
	return true;
}

int highestAlpha = 0;
bool Compressed_ToRGBA32(TextureFile *pTex, void *pOutBuf)
{
	//pvrtexture::PixelType pvr_pxType;
	switch (pTex->m_TextureFormat)
	{
		case TexFmt_ETC_RGB4Crunched:
		case TexFmt_ETC2_RGBA8Crunched:
		case TexFmt_DXT1Crunched:
		case TexFmt_DXT5Crunched:
		{
			std::vector<uint8_t> decrunchBuf;
			TextureFormat decrunchFormat = (TextureFormat)0;
			if (pTex->extra.textureFormatVersion >= 1)
			{
				if (!DecrunchTextureData_Unity(pTex, decrunchBuf, decrunchFormat))
					return false;
			}
			else
			{
				if (!DecrunchTextureData_Legacy(pTex, decrunchBuf, decrunchFormat))
					return false;
			}
			bool ret = false;
			{
				uint32_t origFormat = pTex->m_TextureFormat;
				uint8_t* pOrigBuf = pTex->pPictureData;
				uint32_t origSize = pTex->_pictureDataSize;
				uint32_t origCompleteSize = pTex->m_CompleteImageSize;

				pTex->m_TextureFormat = (uint32_t)decrunchFormat;
				pTex->pPictureData = decrunchBuf.data();
				pTex->_pictureDataSize = (uint32_t)decrunchBuf.size();
				pTex->m_CompleteImageSize = (uint32_t)decrunchBuf.size();

				ret = Compressed_ToRGBA32(pTex, pOutBuf);

				pTex->m_TextureFormat = origFormat;
				pTex->pPictureData = pOrigBuf;
				pTex->_pictureDataSize = origSize;
				pTex->m_CompleteImageSize = origCompleteSize;
			}
			return ret;
		}
		break;
		case TexFmt_DXT1:
		case TexFmt_DXT5:
		{
			pTex->m_MipCount = 1;
			void *DXTBuf = pTex->pPictureData; size_t DXTBufLen = pTex->_pictureDataSize;
			int squishDXTType; TextureFormat dxtTextureFormat;
			if (pTex->m_TextureFormat == TexFmt_DXT1)
			{
				dxtTextureFormat = TexFmt_DXT1;
				squishDXTType = squish::kDxt1;
			}
			else if (pTex->m_TextureFormat == TexFmt_DXT5)
			{
				dxtTextureFormat = TexFmt_DXT5;
				squishDXTType = squish::kDxt5;
			}
			if (pTex->_pictureDataSize < (size_t)GetCompressedTextureDataSize(pTex->m_Width, pTex->m_Height, dxtTextureFormat))
				return false;
			squish::DecompressImage((squish::u8*)pOutBuf, pTex->m_Width, pTex->m_Height, pTex->pPictureData, squishDXTType);//pTex->m_Width * pTex->m_Height * 4;
			return true;
		}
		break;
		case TexFmt_BC4:
		case TexFmt_BC5:
		case TexFmt_BC6H:
		case TexFmt_BC7:
			{
				if (pTex->_pictureDataSize < (size_t)GetCompressedTextureDataSize(pTex->m_Width, pTex->m_Height, (TextureFormat)pTex->m_TextureFormat))
				{
					break;
				}
				Image image = {};
				Texture texture = {};
				texture.pixels = (unsigned int*)pTex->pPictureData;
				texture.width = pTex->m_Width;
				texture.height = pTex->m_Height;
				texture.extended_width = (texture.width + 3) & (~3); //4x4 block texture format
				texture.extended_height = (texture.height + 3) & (~3);
				switch (pTex->m_TextureFormat)
				{
				case TexFmt_BC4:
					texture.type = TEXTURE_TYPE_RGTC1;
					break;
				case TexFmt_BC5:
					texture.type = TEXTURE_TYPE_RGTC2;
					break;
				case TexFmt_BC6H:
					texture.type = TEXTURE_TYPE_BPTC_FLOAT;
					break;
				case TexFmt_BC7:
					texture.type = TEXTURE_TYPE_BPTC;
					break;
				}
				texture.info = match_texture_type(texture.type);
				texture.bits_per_block = texture.info->bits_per_block;
				texture.block_width = texture.info->block_width;
				texture.block_height = texture.info->block_height;
				set_texture_decoding_function(&texture, NULL);
				convert_texture_to_image(&texture, &image);
				if (image.pixels == NULL)
					return false;
				memset(&texture, 0, sizeof(texture));
				if (pTex->m_TextureFormat == TexFmt_BC4 || pTex->m_TextureFormat == TexFmt_BC5)
				{
					convert_image_to_8_bit_format(&image, 4, 0);
					if (image.pixels == NULL)
						return false;
				}
				copy_image_to_uncompressed_texture(&image, TEXTURE_TYPE_UNCOMPRESSED_RGBA8, &texture);
				destroy_image(&image);
				if (texture.pixels == NULL)
					return false;
				memcpy(pOutBuf, texture.pixels, pTex->m_Width * pTex->m_Height * 4);
				destroy_texture(&texture);
				return true;
				//option_texture_format
				//option_mipmaps
			}
			break;
		case TexFmt_YUV2: //not actually compressed but it's also handled
		case TexFmt_EAC_R:
		case TexFmt_EAC_R_SIGNED:
		case TexFmt_EAC_RG:
		case TexFmt_EAC_RG_SIGNED:
		case TexFmt_ETC_RGB4:
		case TexFmt_ETC_RGB4_3DS:
		case TexFmt_ETC_RGBA8_3DS:
		case TexFmt_ETC2_RGB4:
		case TexFmt_ETC2_RGBA1:
		case TexFmt_ETC2_RGBA8:
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA4:
			{
				typedef size_t(_cdecl *Wrap_Decompress)(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void *pInBuf, size_t inBufLen, void *pOutBuf, size_t outBufLen);
				HMODULE hModule = LoadLibrary(TEXT("TexToolWrap.dll"));
				if (hModule)
				{
					Wrap_Decompress Decompress = (Wrap_Decompress)GetProcAddress(hModule, "Decompress");
					size_t decompSize = 0;
					if (Decompress)
						decompSize = 
						Decompress(pTex->m_TextureFormat,
							pTex->m_Height, pTex->m_Width, pTex->m_MipCount, 
							pTex->pPictureData, pTex->_pictureDataSize, 
							pOutBuf, 4 * pTex->m_Width * pTex->m_Height);
					FreeLibrary(hModule);
					return decompSize == (4 * pTex->m_Width * pTex->m_Height);
				}
			}
			break;
		case TexFmt_ASTC_RGB_4x4:
		case TexFmt_ASTC_RGB_5x5:
		case TexFmt_ASTC_RGB_6x6:
		case TexFmt_ASTC_RGB_8x8:
		case TexFmt_ASTC_RGB_10x10:
		case TexFmt_ASTC_RGB_12x12:
		case TexFmt_ASTC_RGBA_4x4:
		case TexFmt_ASTC_RGBA_5x5:
		case TexFmt_ASTC_RGBA_6x6:
		case TexFmt_ASTC_RGBA_8x8:
		case TexFmt_ASTC_RGBA_10x10:
		case TexFmt_ASTC_RGBA_12x12:
		case TexFmt_ASTC_HDR_4x4:
		case TexFmt_ASTC_HDR_5x5:
		case TexFmt_ASTC_HDR_6x6:
		case TexFmt_ASTC_HDR_8x8:
		case TexFmt_ASTC_HDR_10x10:
		case TexFmt_ASTC_HDR_12x12:
			{
				int blockDim = 4;
				DWORD textureFormat = pTex->m_TextureFormat;
				static_assert(TexFmt_ASTC_4x4 == TexFmt_ASTC_RGB_4x4, "Outdated assumption for texture format enum values");
				if (pTex->extra.textureFormatVersion >= 2
					&& textureFormat >= TexFmt_ASTC_4x4 && textureFormat <= TexFmt_ASTC_12x12)
				{
					textureFormat = (textureFormat - TexFmt_ASTC_4x4) + TexFmt_ASTC_RGBA_4x4;
				}
				switch (textureFormat)
				{
					case TexFmt_ASTC_HDR_4x4: case TexFmt_ASTC_4x4: blockDim = 4; break;
					case TexFmt_ASTC_HDR_5x5: case TexFmt_ASTC_5x5: blockDim = 5; break;
					case TexFmt_ASTC_HDR_6x6: case TexFmt_ASTC_6x6: blockDim = 6; break;
					case TexFmt_ASTC_HDR_8x8: case TexFmt_ASTC_8x8: blockDim = 8; break;
					case TexFmt_ASTC_HDR_10x10: case TexFmt_ASTC_10x10: blockDim = 10; break;
					case TexFmt_ASTC_HDR_12x12: case TexFmt_ASTC_12x12: blockDim = 12; break;
					default: assert(false);
				}
				bool isHDR = (textureFormat >= TexFmt_ASTC_HDR_4x4 && textureFormat <= TexFmt_ASTC_HDR_12x12);
				astcenc_config astcenc_cfg;
				if (astcenc_config_init(isHDR ? ASTCENC_PRF_HDR : ASTCENC_PRF_LDR,
					blockDim, blockDim, 1,
					ASTCENC_PRE_FAST,
					ASTCENC_FLG_DECOMPRESS_ONLY, &astcenc_cfg)
					!= ASTCENC_SUCCESS)
					return false;
				//Note: May be noticably more efficient to reuse a context, as per the astcenc documentation.
				astcenc_context* pAstcencContext = nullptr;
				if (astcenc_context_alloc(&astcenc_cfg, 1, &pAstcencContext)
					!= ASTCENC_SUCCESS)
					return false;
				astcenc_decompress_reset(pAstcencContext);
				astcenc_image image_out = {};
				void* dataSlices[1] = { pOutBuf }; //z size = 1
				image_out.data = &dataSlices[0];
				image_out.data_type = ASTCENC_TYPE_U8;
				image_out.dim_x = pTex->m_Width;
				image_out.dim_y = pTex->m_Height;
				image_out.dim_z = 1;
				astcenc_swizzle swizzle = {};
				swizzle.r = ASTCENC_SWZ_R; 
				swizzle.g = ASTCENC_SWZ_G;
				swizzle.b = ASTCENC_SWZ_B;
				swizzle.a = ASTCENC_SWZ_A;
				auto result = astcenc_decompress_image(pAstcencContext, pTex->pPictureData, pTex->_pictureDataSize, &image_out, &swizzle, 0);
				astcenc_context_free(pAstcencContext);
				if (result != ASTCENC_SUCCESS)
					return false;
				return true;
			}
			break;
	}
	return false;
}

static int astcTexFmt_GetBlockDim(TextureFormat texFmt)
{
	switch (texFmt)
	{
	case TexFmt_ASTC_RGB_4x4: 
	case TexFmt_ASTC_RGBA_4x4:
	case TexFmt_ASTC_HDR_4x4:
		return 4;
	case TexFmt_ASTC_RGB_5x5:
	case TexFmt_ASTC_RGBA_5x5:
	case TexFmt_ASTC_HDR_5x5:
		return 5;
	case TexFmt_ASTC_RGB_6x6:
	case TexFmt_ASTC_RGBA_6x6:
	case TexFmt_ASTC_HDR_6x6:
		return 6;
	case TexFmt_ASTC_RGB_8x8:
	case TexFmt_ASTC_RGBA_8x8:
	case TexFmt_ASTC_HDR_8x8:
		return 8;
	case TexFmt_ASTC_RGB_10x10:
	case TexFmt_ASTC_RGBA_10x10:
	case TexFmt_ASTC_HDR_10x10:
		return 10;
	case TexFmt_ASTC_RGB_12x12:
	case TexFmt_ASTC_RGBA_12x12:
	case TexFmt_ASTC_HDR_12x12:
		return 12;
	default:
		throw std::invalid_argument("astcTexFmt_GetBlockDim expects a ASTC texture format value.");
	}
}
size_t GetCompressedTextureDataSize(int width, int height, TextureFormat texFmt)
{
	if (width < 0 || height < 0)
		throw std::invalid_argument("negative width or height");
	//DXT1 : ARGB file size factor (at 4x4 pixel blocks) : 1/8
	//DXT5 : ARGB file size factor (at 4x4 pixel blocks) : 1/4
	//BC4 : ARGB file size factor (at 4x4 pixel blocks) : 1/8
	//BC5 : ARGB file size factor (at 4x4 pixel blocks) : 1/4
	//BC6H : ARGB file size factor (at 4x4 pixel blocks) : 1/4
	//BC7 : ARGB file size factor (at 4x4 pixel blocks) : 1/4

	switch (texFmt)
	{
		case TexFmt_DXT1:
		case TexFmt_BC4: //same size as DXT1
			return (unsigned int)squish::GetStorageRequirements(width, height, squish::kDxt1);
		case TexFmt_DXT5:
		case TexFmt_BC5: //same size as DXT5
		case TexFmt_BC6H: //same size as DXT5
		case TexFmt_BC7: //same size as DXT5
			return (unsigned int)squish::GetStorageRequirements(width, height, squish::kDxt5);
		case TexFmt_YUV2: //not actually compressed but it's also handled
		case TexFmt_EAC_R:
		case TexFmt_EAC_R_SIGNED:
		case TexFmt_EAC_RG:
		case TexFmt_EAC_RG_SIGNED:
		case TexFmt_ETC_RGB4:
		case TexFmt_ETC_RGB4_3DS:
		case TexFmt_ETC_RGBA8_3DS:
		case TexFmt_ETC2_RGB4:
		case TexFmt_ETC2_RGBA1:
		case TexFmt_ETC2_RGBA8:
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA4:
		{
			typedef unsigned int(_cdecl *Wrap_GetMaxCompressedSize)(int width, int height, uint32_t texFmt);
			HMODULE hModule = LoadLibrary(TEXT("TexToolWrap.dll"));
			if (hModule)
			{
				Wrap_GetMaxCompressedSize GetMaxCompressedSize = (Wrap_GetMaxCompressedSize)GetProcAddress(hModule, "GetMaxCompressedSize");
				size_t decompSize = 0;
				if (GetMaxCompressedSize)
					decompSize = GetMaxCompressedSize(width, height, (uint32_t)texFmt);
				FreeLibrary(hModule);
				return decompSize;
			}
		}
		break;
		case TexFmt_ASTC_RGB_4x4:
		case TexFmt_ASTC_RGB_5x5:
		case TexFmt_ASTC_RGB_6x6:
		case TexFmt_ASTC_RGB_8x8:
		case TexFmt_ASTC_RGB_10x10:
		case TexFmt_ASTC_RGB_12x12:
		case TexFmt_ASTC_RGBA_4x4:
		case TexFmt_ASTC_RGBA_5x5:
		case TexFmt_ASTC_RGBA_6x6:
		case TexFmt_ASTC_RGBA_8x8:
		case TexFmt_ASTC_RGBA_10x10:
		case TexFmt_ASTC_RGBA_12x12:
		case TexFmt_ASTC_HDR_4x4:
		case TexFmt_ASTC_HDR_5x5:
		case TexFmt_ASTC_HDR_6x6:
		case TexFmt_ASTC_HDR_8x8:
		case TexFmt_ASTC_HDR_10x10:
		case TexFmt_ASTC_HDR_12x12:
		{
			int blockDim = astcTexFmt_GetBlockDim(texFmt);
			int blocksHor = (width / blockDim) + ((width % blockDim) ? 1 : 0);
			int blocksVert = (height / blockDim) + ((height % blockDim) ? 1 : 0);
			//128 bit per block, apparently for both SDR and HDR.
			return (size_t)blocksHor * (size_t)blocksVert * (128 / 8);
		}
		break;
		default:
			return 0;
	}
	return 0;
}

static void texgenpack_compress_callback(BlockUserData *user_data) {}
typedef void(__cdecl* ISPCCompressBlocksFn)(const rgba_surface* src, uint8_t* dst, void* settings);
void CompressDXTBlockCallback(const rgba_surface* src, uint8_t* dst, void* settings)
{
	squish::CompressImage((squish::u8*)src->ptr, src->width, src->height, dst, reinterpret_cast<int>(settings));
}
struct ISPCCompressThreadPar
{
	rgba_surface surface;
	uint8_t *outBuf;
	void *encSettings;
	ISPCCompressBlocksFn compressFn;
	HANDLE threadHandle;
};
DWORD _stdcall ISPCCompressThreadEntry(PVOID tpar)
{
	ISPCCompressThreadPar *pThreadPar = (ISPCCompressThreadPar*)tpar;
	pThreadPar->compressFn(&pThreadPar->surface, pThreadPar->outBuf, pThreadPar->encSettings);
	return 0;
}
inline uint32_t GetNumberOfProcessors()
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	uint32_t numThreads = sysInfo.dwNumberOfProcessors;
	if (!numThreads) numThreads = 1;
	return numThreads;
}
//Input must fit in 4x4 blocks; bytesPerBlock is 8 for DXT1, 16 for DXT5/BC6H/BC7
void ISPCTexCompressMt(rgba_surface *surface, uint8_t *outBuf, void *settings, ISPCCompressBlocksFn compressFn, unsigned int bytesPerBlock)
{
	if (surface->height <= 0)
		return;
	uint32_t numThreads = GetNumberOfProcessors();
	uint32_t verticalBlocks = (uint32_t)(surface->height >> 2);
	if (numThreads > verticalBlocks)
		numThreads = verticalBlocks;
	uint32_t vBlocksPerThread = verticalBlocks / numThreads;
	uint32_t vBlocksUnassigned = verticalBlocks % numThreads;
	ISPCCompressThreadPar *pThreadPars = new ISPCCompressThreadPar[numThreads];
	uint32_t curPixelLine = 0;
	for (uint32_t i = 0; i < numThreads; i++)
	{
		pThreadPars[i].surface.ptr = &surface->ptr[curPixelLine*surface->stride];
		pThreadPars[i].surface.width = surface->width;
		pThreadPars[i].surface.stride = surface->stride;
		pThreadPars[i].surface.height = vBlocksPerThread << 2;
		if (vBlocksUnassigned)
		{
			pThreadPars[i].surface.height += 4;
			vBlocksUnassigned--;
		}
		pThreadPars[i].outBuf = &outBuf[(curPixelLine >> 2) * (surface->width >> 2) * bytesPerBlock];
			//&outBuf[curPixelLine * surface->width]; //1 byte per pixel
		pThreadPars[i].encSettings = settings;
		pThreadPars[i].compressFn = compressFn;
		curPixelLine += pThreadPars[i].surface.height;
		pThreadPars[i].threadHandle = CreateThread(NULL, 0, ISPCCompressThreadEntry, &pThreadPars[i], 0, NULL);
	}
	for (uint32_t i = 0; i < numThreads; i++)
	{
		WaitForSingleObject(pThreadPars[i].threadHandle, INFINITE);
		CloseHandle(pThreadPars[i].threadHandle);
	}
	delete[] pThreadPars;
}

//Simple linear filter.
//Also allows in-place generation of the next mip map level (i.e. inBuf == outBuf).
//Updates width and height parameters for the width/height of outBuf.
//Returns false if there is no next mip map level, in which case outBuf is untouched.
bool MakeNextMipmapLevel_RGBA32(const void *_inBuf, void *_outBuf, unsigned int &width, unsigned int &height)
{
	const uint8_t *inBuf = (const uint8_t*)_inBuf;
	uint32_t *outBuf = (uint32_t*)_outBuf;
	if (width && height)
	{
		unsigned int newWidth;
		unsigned int newHeight;
		if ((width >> 1) >= 1 && (height >> 1) >= 1)
		{
			newWidth = width >> 1;
			newHeight = height >> 1;
			for (unsigned int y = 1; y < height; y += 2)
			{
				for (unsigned int x = 1; x < width; x += 2)
				{
					uint8_t r = ((int)(inBuf)[4*((y-1) * width + (x-1))] +
						(int)(inBuf)[4*(y * width + (x-1))] +
						(int)(inBuf)[4*((y-1) * width + x)] +
						(int)(inBuf)[4*(y * width + x)])
						>> 2;
					uint8_t g = ((int)inBuf[1+4*((y-1) * width + (x-1))] +
						(int)inBuf[1+4*(y * width + (x-1))] +
						(int)inBuf[1+4*((y-1) * width + x)] +
						(int)inBuf[1+4*(y * width + x)])
						>> 2;
					uint8_t b = ((int)inBuf[2+4*((y-1) * width + (x-1))] +
						(int)inBuf[2+4*(y * width + (x-1))] +
						(int)inBuf[2+4*((y-1) * width + x)] +
						(int)inBuf[2+4*(y * width + x)])
						>> 2;
					uint8_t a = ((int)inBuf[3+4*((y-1) * width + (x-1))] +
						(int)inBuf[3+4*(y * width + (x-1))] +
						(int)inBuf[3+4*((y-1) * width + x)] +
						(int)inBuf[3+4*(y * width + x)])
						>> 2;
					outBuf[((y-1)>>1) * newWidth + ((x-1)>>1)] = r | (g << 8) | (b << 16) | (a << 24);
				}
			}
		}
		else
		{
			unsigned int _len;
			if ((width >> 1) >= 1) //=> curWidth >= 2, curHeight == 1
			{
				//(curHeight / 2) : ]0;1[
				newWidth = width >> 1;
				newHeight = 1;
				_len = width;
			}
			else if ((height >> 1) >= 1) //=> curWidth == 1, curHeight >= 2
			{
				//(curWidth / 2) : ]0;1[
				newWidth = 1;
				newHeight = height >> 1;
				_len = height;
			}
			else
				return false;
			for (unsigned int i = 1; i < _len; i += 2)
			{
				uint8_t r = ((int)inBuf[4*(i-1)] +
					(int)inBuf[4*(i)])
					>> 1;
				uint8_t g = ((int)inBuf[1+4*(i-1)] +
					(int)inBuf[1+4*(i)])
					>> 1;
				uint8_t b = ((int)inBuf[2+4*(i-1)] +
					(int)inBuf[2+4*(i)])
					>> 1;
				uint8_t a = ((int)inBuf[3+4*(i-1)] +
					(int)inBuf[3+4*(i)])
					>> 1;
				outBuf[(i-1)>>1] = r | (g << 8) | (b << 16) | (a << 24);
			}
		}
		width = newWidth;
		height = newHeight;
	}
	else
		return false;
	return true;
}

static rgba_surface ispc_texcomp_prepare_surface_RGBA32(void* pRGBA32Buf, unsigned int curWidth, unsigned int curHeight,
	std::unique_ptr<uint8_t[]> &pExtendedBuffer, unsigned int numOutChannels=4, unsigned int channelBits=8)
{
	if (numOutChannels == 0 || numOutChannels >= 4)
		throw std::invalid_argument("ispc_texcomp_prepare_surface_RGBA32: numOutChannels should be in [1,4]");
	if (channelBits != 8 && channelBits != 16)
		throw std::invalid_argument("ispc_texcomp_prepare_surface_RGBA32: channelBits must be either 8 or 16");
	if (curWidth > (unsigned int)std::numeric_limits<int>::max()
		|| curHeight > (unsigned int)std::numeric_limits<int>::max()
		|| (curWidth * 4 * sizeof(uint8_t)) > (unsigned int)std::numeric_limits<int>::max())
		throw std::invalid_argument("ispc_texcomp_prepare_surface_RGBA32: width or height out of range");
	rgba_surface surface = { (uint8_t*)pRGBA32Buf, (int)curWidth, (int)curHeight, (int)(curWidth * 4 * sizeof(uint8_t)) };
	rgba_surface surfaceOut = surface;

	unsigned int extendedWidth = (curWidth + 3) & (~3);
	unsigned int extendedHeight = (curHeight + 3) & (~3);
	if (curWidth != extendedWidth || curHeight != extendedHeight || numOutChannels != 4)
	{
		pExtendedBuffer.reset(new uint8_t[extendedWidth * extendedHeight * numOutChannels * (channelBits/8)]);
		//Copy the pixels, but only the first <numOutChannels> bytes per input pixel.
		for (unsigned int y = 0; y < curHeight; ++y)
		for (unsigned int x = 0; x < curWidth; ++x)
		{
			uint8_t *pIn = &((uint8_t*)pRGBA32Buf)[(y * curWidth + x) * 4];
			uint8_t *pOut = &pExtendedBuffer[(y * extendedWidth + x) * numOutChannels * (channelBits/8)];
			//Copy the respective channel; if channelBits=16,
			// replicate the channel value for each output byte (e.g. 0x1F -> 0x1F1F).
			for (unsigned int iCh = 0; iCh < numOutChannels; ++iCh)
				for (unsigned int iBy = 0; iBy < channelBits/8; ++iBy)
					pOut[iCh * channelBits/8 + iBy] = pIn[iCh];
		}
		surfaceOut.width = extendedWidth;
		surfaceOut.height = extendedHeight;
		surfaceOut.stride = extendedWidth * numOutChannels * (channelBits / 8) * sizeof(uint8_t);
		surfaceOut.ptr = pExtendedBuffer.get();

		surface.ptr = surfaceOut.ptr;
		surface.stride = surfaceOut.stride;
		//Padding for the extended image dimensions (replicate the border pixel).
		//Helper function by ispc_texcomp.
		ReplicateBorders(&surfaceOut, &surface, 0, 0, channelBits * numOutChannels);
	}
	return surfaceOut;
}

void RGBA32_ToCompressed(TextureFile *pTex, void *pOutBuf, void *pRGBA32Buf, QWORD &outputSize, int compressQuality, unsigned int curWidth, unsigned int curHeight)
{
	if (pTex->m_TextureFormat == TexFmt_DXT1Crunched || pTex->m_TextureFormat == TexFmt_DXT5Crunched
		|| pTex->m_TextureFormat == TexFmt_ETC_RGB4Crunched || pTex->m_TextureFormat == TexFmt_ETC2_RGBA8Crunched)
	{
		(pTex->extra.textureFormatVersion >= 1 ? CrunchTextureData_RGBA32_Unity : CrunchTextureData_RGBA32_Legacy)
			(pTex, pRGBA32Buf, pOutBuf, outputSize, compressQuality, curWidth, curHeight);
	}
	else
	{
		switch (pTex->m_TextureFormat)
		{
		case TexFmt_DXT1:
		case TexFmt_DXT5:
			{
				//GetCompressedTextureDataSize takes care of DXT block size alignment (4x4).
				size_t texLen = GetCompressedTextureDataSize(curWidth, curHeight, (TextureFormat)pTex->m_TextureFormat);
				//int squishDXTType = ((pTex->m_TextureFormat == TexFmt_DXT1 || pTex->m_TextureFormat == TexFmt_DXT1Crunched) ? squish::kDxt1 : squish::kDxt5);
				if (outputSize >= texLen)
				{
					int dxtFlags = ((pTex->m_TextureFormat == TexFmt_DXT1) ? squish::kDxt1 : squish::kDxt5);
					bool mt = false;
					switch (compressQuality)
					{
						case 1: mt = true; //normal mt
						case 0: //normal
						default:
							dxtFlags |= squish::kColourClusterFit;
							break;
						case 3: mt = true; //very fast mt
						case 2: //very fast
							dxtFlags |= squish::kColourRangeFit;
							break;
						case 5: mt = true; //slow mt
						case 4: //slow
							dxtFlags |= squish::kColourIterativeClusterFit;
							break;
					}
					if (mt)
					{
						unsigned int extendedWidth = (curWidth + 3) & (~3);
						unsigned int extendedHeight = (curHeight + 3) & (~3);
						uint8_t *pRGBAExt = new uint8_t[extendedWidth * extendedHeight * 4];
						for (unsigned int y = 0; y < curHeight; y++)
						{
							memcpy(&pRGBAExt[y*extendedWidth*4], &((uint8_t*)pRGBA32Buf)[y*curWidth*4], curWidth * 4);
							for (unsigned int x = curWidth; x < extendedWidth; x++)
							{
								unsigned int outIndex = (y*extendedWidth+x)*4;
								unsigned int inIndex = (y*extendedWidth+curWidth-1)*4;
								pRGBAExt[outIndex] = pRGBAExt[inIndex];
								pRGBAExt[outIndex+1] = pRGBAExt[inIndex+1];
								pRGBAExt[outIndex+2] = pRGBAExt[inIndex+2];
								pRGBAExt[outIndex+3] = pRGBAExt[inIndex+3];
							}
						}
						for (unsigned int y = curHeight; y < extendedHeight; y++)
						{
							memcpy(&pRGBAExt[(y*extendedWidth)*4], &pRGBAExt[((curHeight-1)*extendedWidth)*4], extendedWidth*4 * sizeof(uint8_t));
						}
						rgba_surface surface;
						surface.width = extendedWidth;
						surface.height = extendedHeight;
						surface.stride = extendedWidth * 4;
						surface.ptr = pRGBAExt;
						ISPCTexCompressMt(
							&surface, 
							(uint8_t*)pOutBuf, 
							(void*)dxtFlags, 
							CompressDXTBlockCallback, 
							(pTex->m_TextureFormat == TexFmt_DXT1) ? 8 : 16
							);
						delete[] pRGBAExt;
					}
					else
						squish::CompressImage((squish::u8*)pRGBA32Buf, curWidth, curHeight, pOutBuf, dxtFlags);
					outputSize = texLen;
				}
				else
					outputSize = 0;
			}
			break;
		case TexFmt_BC4:
		case TexFmt_BC5:
			{
				bool mt = false;
				switch (compressQuality)
				{
				case 5:
				case 6:
				case 7:
				case 8:
				case 9:
					mt = true;
					break;
				default:
					mt = false;
				}
				std::unique_ptr<uint8_t[]> pExtendedBuffer;
				//BC4 only uses the red channel, BC5 uses red and green.
				//The compressor expects the input data to only have these channels.
				rgba_surface surface = ispc_texcomp_prepare_surface_RGBA32(pRGBA32Buf, curWidth, curHeight, pExtendedBuffer, (pTex->m_TextureFormat == TexFmt_BC4) ? 1 : 2);
				size_t texLen = GetCompressedTextureDataSize(surface.width, surface.height, (TextureFormat)pTex->m_TextureFormat);
				if (outputSize >= texLen)
				{
					if (pTex->m_TextureFormat == TexFmt_BC4)
					{
						if (mt)
							ISPCTexCompressMt(&surface, (uint8_t*)pOutBuf, nullptr, (ISPCCompressBlocksFn)CompressBlocksBC4, 8);
						else
							CompressBlocksBC4(&surface, (uint8_t*)pOutBuf);
					}
					else //if (pTex->m_TextureFormat == TexFmt_BC5)
					{
						if (mt)
							ISPCTexCompressMt(&surface, (uint8_t*)pOutBuf, nullptr, (ISPCCompressBlocksFn)CompressBlocksBC5, 16);
						else
							CompressBlocksBC5(&surface, (uint8_t*)pOutBuf);
					}
					outputSize = texLen;
				}
				else
					outputSize = 0;
			}
			break;
		case TexFmt_BC6H:
			{
				bc6h_enc_settings settings;
				bool mt = false;
				switch (compressQuality)
				{
				case 5: mt = true;
				case 0:
					GetProfile_bc6h_veryfast(&settings);
					break;
				case 6: mt = true;
				case 1:
					GetProfile_bc6h_fast(&settings);
					break;
				case 7: mt = true;
				case 2:
				default:
					GetProfile_bc6h_basic(&settings);
					break;
				case 8: mt = true;
				case 3:
					GetProfile_bc6h_slow(&settings);
					break;
				case 9: mt = true;
				case 4:
					GetProfile_bc6h_veryslow(&settings);
					break;
				}
				std::unique_ptr<uint8_t[]> pExtendedBuffer;
				//BC6H uses r,g,b with 16bit each; the alpha channel is present in the inputs but ignored.
				rgba_surface surface = ispc_texcomp_prepare_surface_RGBA32(pRGBA32Buf, curWidth, curHeight, pExtendedBuffer, 4, 16);
				size_t texLen = GetCompressedTextureDataSize(surface.width, surface.height, (TextureFormat)pTex->m_TextureFormat);
				if (outputSize >= texLen)
				{
					if (mt)
						ISPCTexCompressMt(&surface, (uint8_t*)pOutBuf, &settings, (ISPCCompressBlocksFn)CompressBlocksBC6H, 16);
					else
						CompressBlocksBC6H(&surface, (uint8_t*)pOutBuf, &settings);
					outputSize = texLen;
				}
				else
					outputSize = 0;
			}
			break;
		case TexFmt_BC7:
			{
				bc7_enc_settings settings;
				bool mt = false;
				switch (compressQuality)
				{
				case 5: mt = true;
				case 0:
					GetProfile_alpha_ultrafast(&settings);
					break;
				case 6: mt = true;
				case 1:
					GetProfile_alpha_veryfast(&settings);
					break;
				case 7: mt = true;
				case 2:
					GetProfile_alpha_fast(&settings);
					break;
				case 8: mt = true;
				case 3:
				default:
					GetProfile_alpha_basic(&settings);
					break;
				case 9: mt = true;
				case 4:
					GetProfile_alpha_slow(&settings);
					break;
				}
				std::unique_ptr<uint8_t[]> pExtendedBuffer;
				//Regular RGBA 32bpp input.
				rgba_surface surface = ispc_texcomp_prepare_surface_RGBA32(pRGBA32Buf, curWidth, curHeight, pExtendedBuffer, 4, 8);
				size_t texLen = GetCompressedTextureDataSize(surface.width, surface.height, (TextureFormat)pTex->m_TextureFormat);
				if (outputSize >= texLen)
				{
					if (mt)
						ISPCTexCompressMt(&surface, (uint8_t*)pOutBuf, &settings, (ISPCCompressBlocksFn)CompressBlocksBC7, 16);
					else
						CompressBlocksBC7(&surface, (uint8_t*)pOutBuf, &settings);
					outputSize = texLen;
				}
				else
					outputSize = 0;
			}
			break;
		case TexFmt_YUV2: //not actually compressed but it's also handled
		case TexFmt_EAC_R:
		case TexFmt_EAC_R_SIGNED:
		case TexFmt_EAC_RG:
		case TexFmt_EAC_RG_SIGNED:
		case TexFmt_ETC_RGB4:
		case TexFmt_ETC_RGB4_3DS:
		case TexFmt_ETC_RGBA8_3DS:
		case TexFmt_ETC2_RGB4:
		case TexFmt_ETC2_RGBA1:
		case TexFmt_ETC2_RGBA8:
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA4:
			{
				typedef size_t(_cdecl* Wrap_Compress)(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void* pInBuf, size_t inBufLen, void* pOutBuf, size_t outBufLen, int compressQuality);
				HMODULE hModule = LoadLibrary(TEXT("TexToolWrap.dll"));
				if (hModule)
				{
					Wrap_Compress Compress = (Wrap_Compress)GetProcAddress(hModule, "Compress");
					if (Compress)
						outputSize =
						Compress(pTex->m_TextureFormat,
							curHeight, curWidth, 1,
							pRGBA32Buf, curWidth * curHeight * 4,
							pOutBuf, outputSize, compressQuality);
					else
						outputSize = 0;
					FreeLibrary(hModule);
				}
			}
			break;
		case TexFmt_ASTC_RGB_4x4:
		case TexFmt_ASTC_RGB_5x5:
		case TexFmt_ASTC_RGB_6x6:
		case TexFmt_ASTC_RGB_8x8:
		case TexFmt_ASTC_RGB_10x10:
		case TexFmt_ASTC_RGB_12x12:
		case TexFmt_ASTC_RGBA_4x4:
		case TexFmt_ASTC_RGBA_5x5:
		case TexFmt_ASTC_RGBA_6x6:
		case TexFmt_ASTC_RGBA_8x8:
		case TexFmt_ASTC_RGBA_10x10:
		case TexFmt_ASTC_RGBA_12x12:
			{
				bool mt = false;
				float astcenc_profile = ASTCENC_PRE_FAST;
				switch (compressQuality)
				{
				case 5: mt = true;
				case 0:
					astcenc_profile = ASTCENC_PRE_FASTEST;
					break;
				case 6: mt = true;
				case 1:
					astcenc_profile = ASTCENC_PRE_FAST;
					break;
				case 7: mt = true;
				case 2:
					astcenc_profile = ASTCENC_PRE_MEDIUM;
					break;
				case 8: mt = true;
				case 3:
				default:
					astcenc_profile = ASTCENC_PRE_THOROUGH;
					break;
				case 9: mt = true;
				case 4:
					astcenc_profile = ASTCENC_PRE_EXHAUSTIVE;
					break;
				}

				int blockDim = astcTexFmt_GetBlockDim((TextureFormat)pTex->m_TextureFormat);
				bool isHDR = (pTex->m_TextureFormat >= TexFmt_ASTC_HDR_4x4 && pTex->m_TextureFormat <= TexFmt_ASTC_HDR_12x12);

				astcenc_config astcenc_cfg;
				if (astcenc_config_init(isHDR ? ASTCENC_PRF_HDR : ASTCENC_PRF_LDR,
					blockDim, blockDim, 1,
					astcenc_profile,
					0, &astcenc_cfg)
					!= ASTCENC_SUCCESS)
					return;
				//Note: May be noticably more efficient to reuse a context, as per the astcenc documentation.
				astcenc_context* pAstcencContext = nullptr;
				unsigned int numThreads = 1;
				if (mt)
				{
					numThreads = std::thread::hardware_concurrency();
					if (numThreads == 0) numThreads = 2;
				}
				if (astcenc_context_alloc(&astcenc_cfg, numThreads, &pAstcencContext)
					!= ASTCENC_SUCCESS)
					return;
				astcenc_compress_reset(pAstcencContext);
				astcenc_image image_in = {};
				void* dataSlices[1] = { pRGBA32Buf }; //z size = 1
				image_in.data = &dataSlices[0];
				image_in.data_type = ASTCENC_TYPE_U8;
				image_in.dim_x = pTex->m_Width;
				image_in.dim_y = pTex->m_Height;
				image_in.dim_z = 1;
				astcenc_swizzle swizzle = {};
				swizzle.r = ASTCENC_SWZ_R;
				swizzle.g = ASTCENC_SWZ_G;
				swizzle.b = ASTCENC_SWZ_B;
				swizzle.a = ASTCENC_SWZ_A;
				std::vector<std::jthread> threads(numThreads - 1);
				for (unsigned int i = 1; i < numThreads; ++i)
				{
					threads[i - 1] = std::jthread([pAstcencContext, &image_in, &swizzle, pOutBuf, outputSize, i]()
					{
						astcenc_compress_image(pAstcencContext, &image_in, &swizzle, (uint8_t*)pOutBuf, outputSize, i);
					});
				}
				auto result = astcenc_compress_image(pAstcencContext, &image_in, &swizzle, (uint8_t*)pOutBuf, outputSize, 0);
				threads.clear();
				astcenc_context_free(pAstcencContext);
				if (result != ASTCENC_SUCCESS)
					return;
				outputSize = GetCompressedTextureDataSize((int)pTex->m_Width, (int)pTex->m_Height, (TextureFormat)pTex->m_TextureFormat);
			}
			break;
		}
	}
}

ASSETSTOOLS_API bool MakeTextureData(TextureFile *pTex, void *pRGBA32Buf, int compressQuality)
{
	uint8_t *outPictureData = pTex->pPictureData;
	if (pTex->m_TextureFormat == TexFmt_DXT1Crunched || pTex->m_TextureFormat == TexFmt_DXT5Crunched
		|| pTex->m_TextureFormat == TexFmt_ETC_RGB4Crunched || pTex->m_TextureFormat == TexFmt_ETC2_RGBA8Crunched)
	{
		QWORD outputSize = pTex->_pictureDataSize;
		
		if ((pTex->extra.textureFormatVersion >= 1 ? CrunchTextureData_RGBA32_Unity : CrunchTextureData_RGBA32_Legacy)
			  (pTex, pRGBA32Buf, outPictureData, outputSize, compressQuality, pTex->m_Width, pTex->m_Height))
		{
			pTex->_pictureDataSize = pTex->m_CompleteImageSize = (uint32_t)outputSize;
			return true;
		}
		else
			return false;
	}
	int curMipCount = pTex->m_MipCount;
	int totalMipCount = 0;
	unsigned int curWidth = pTex->m_Width;
	unsigned int curHeight = pTex->m_Height;
	QWORD curOutIndex = 0;
	do {
		switch (pTex->m_TextureFormat) {
			case TexFmt_ARGB32:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						outPictureData[curOutIndex+i*4] = rgba[3]; //A
						outPictureData[curOutIndex+i*4+1] = rgba[0]; //R
						outPictureData[curOutIndex+i*4+2] = rgba[1]; //B
						outPictureData[curOutIndex+i*4+3] = rgba[2]; //G
					}
					curOutIndex += (curWidth * curHeight * 4);
				}
				break;
			case TexFmt_BGRA32Old:
			case TexFmt_BGRA32New:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						outPictureData[curOutIndex+i*4] = rgba[2]; //B
						outPictureData[curOutIndex+i*4+1] = rgba[1]; //G
						outPictureData[curOutIndex+i*4+2] = rgba[0]; //R
						outPictureData[curOutIndex+i*4+3] = rgba[3]; //A
					}
					curOutIndex += (curWidth * curHeight * 4);
				}
				break;
			case TexFmt_RGBA32:
				if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
					return false;
				memcpy(&outPictureData[curOutIndex], pRGBA32Buf, curWidth * curHeight * 4);
				curOutIndex += (curWidth * curHeight * 4);
				break;
			case TexFmt_RGB24:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 3))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						outPictureData[curOutIndex+i*3] = rgba[0]; //R
						outPictureData[curOutIndex+i*3+1] = rgba[1]; //G
						outPictureData[curOutIndex+i*3+2] = rgba[2]; //B
					}
					curOutIndex += (curWidth * curHeight * 3);
				}
				break;
			case TexFmt_ARGB4444:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 2))
						return false;
					//G,B,A,R
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						// >> 4 is equal to / 16 (to reduce the 8bit data to 4bit)
						outPictureData[curOutIndex+i*2] = (((rgba[1] >> 4) & 15) << 4) | ((rgba[2] >> 4) & 15); //G,B
						outPictureData[curOutIndex+i*2+1] = (((rgba[3] >> 4) & 15) << 4) | ((rgba[0] >> 4) & 15); //A,R 
					}
					curOutIndex += (curWidth * curHeight * 2);
				}
				break;
			case TexFmt_RGBA4444:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 2))
						return false;
					//BARG
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						// >> 4 is equal to / 16 (to reduce the 8bit data to 4bit)
						outPictureData[curOutIndex+i*2+1] = (((rgba[0] >> 4) & 15) << 4) | ((rgba[1] >> 4) & 15); //R,G
						outPictureData[curOutIndex+i*2] = (((rgba[2] >> 4) & 15) << 4) | ((rgba[3] >> 4) & 15); //B,A
					}
					curOutIndex += (curWidth * curHeight * 2);
				}
				break;
			case TexFmt_RGB565:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 2))
						return false;
					//g3(low)b5g3(high)r5
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t rgba[4];
						*((uint32_t*)rgba) = ((uint32_t*)pRGBA32Buf)[i];
						// >> 3 is equal to / 8 (to reduce the 8bit data to 5bit)
						uint8_t r5 = (rgba[0] >> 3) & 31;
						// >> 2 is equal to / 4 (to reduce the 8bit data to 6bit)
						uint8_t g6 = (rgba[1] >> 2) & 63;
						// >> 3 is equal to / 8 (to reduce the 8bit data to 5bit)
						uint8_t b5 = (rgba[2] >> 3) & 31;
						uint16_t rgb565 = ((uint16_t)r5 << 11) | ((uint16_t)g6 << 5) | ((uint16_t)b5);
						*(uint16_t*)(&outPictureData[curOutIndex+i*2]) = rgb565;//((rgb565 & 0xFF00) >> 8) | ((rgb565 & 0x00FF) << 8);
					}
					curOutIndex += (curWidth * curHeight * 2);
				}
				break;
			case TexFmt_Alpha8:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						outPictureData[curOutIndex+i] = ((uint8_t*)pRGBA32Buf)[i*4+3];
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_RHalf:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 2))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					HalfFloat hf;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*2]) = hf.half;
					}
					curOutIndex += (curWidth * curHeight * 2);
				}
				break;
			case TexFmt_RGHalf:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					HalfFloat hf;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*4]) = hf.half;
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4+1] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*4+2]) = hf.half;
					}
					curOutIndex += (curWidth * curHeight * 4);
				}
				break;
			case TexFmt_RGBAHalf:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 8))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					HalfFloat hf;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*8]) = hf.half;
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4+1] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*8+2]) = hf.half;
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4+2] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*8+4]) = hf.half;
						hf.toHalf((float)((uint8_t*)pRGBA32Buf)[i*4+3] / 255.0F);
						*(uint16_t*)(&outPictureData[curOutIndex+i*8+6]) = hf.half;
					}
					curOutIndex += (curWidth * curHeight * 8);
				}
				break;
			case TexFmt_RFloat:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						*(float*)(&outPictureData[curOutIndex+i*4]) = (float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F;
					}
					curOutIndex += (curWidth * curHeight * 4);
				}
				break;
			case TexFmt_RGFloat:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 8))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						*(float*)(&outPictureData[curOutIndex+i*8]) = (float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F;
						*(float*)(&outPictureData[curOutIndex+i*8+4]) = (float)((uint8_t*)pRGBA32Buf)[i*4+1] / 255.0F;
					}
					curOutIndex += (curWidth * curHeight * 8);
				}
				break;
			case TexFmt_RGBAFloat:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 16))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						*(float*)(&outPictureData[curOutIndex+i*16]) = (float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F;
						*(float*)(&outPictureData[curOutIndex+i*16+4]) = (float)((uint8_t*)pRGBA32Buf)[i*4+1] / 255.0F;
						*(float*)(&outPictureData[curOutIndex+i*16+8]) = (float)((uint8_t*)pRGBA32Buf)[i*4+2] / 255.0F;
						*(float*)(&outPictureData[curOutIndex+i*16+12]) = (float)((uint8_t*)pRGBA32Buf)[i*4+3] / 255.0F;
					}
					curOutIndex += (curWidth * curHeight * 16);
				}
				break;
			case TexFmt_RGB9e5Float:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 4))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						float color[3] = {
							(float)((uint8_t*)pRGBA32Buf)[i*4] / 255.0F, 
							(float)((uint8_t*)pRGBA32Buf)[i*4+1] / 255.0F, 
							(float)((uint8_t*)pRGBA32Buf)[i*4+2] / 255.0F
						};
						RGB9e5Float rgbFloat; rgbFloat.toRGB9e5(color);
						*(unsigned int*)(&outPictureData[curOutIndex+i*4]) = rgbFloat.value;
					}
					curOutIndex += (curWidth * curHeight * 4);
				}
				break;
			case TexFmt_RG16:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight * 2))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						outPictureData[curOutIndex+2*i] = ((uint8_t*)pRGBA32Buf)[i*4];
						outPictureData[curOutIndex+2*i+1] = ((uint8_t*)pRGBA32Buf)[i*4+1];
					}
					curOutIndex += (curWidth * curHeight * 2);
				}
				break;
			case TexFmt_R8:
				{
					if ((curOutIndex > pTex->_pictureDataSize) || (pTex->_pictureDataSize - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						outPictureData[curOutIndex+i] = ((uint8_t*)pRGBA32Buf)[i*4];
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_R16:
				{
					if ((curOutIndex > pTex->_pictureDataSize/2) || (pTex->_pictureDataSize/2 - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t r = ((uint8_t*)pRGBA32Buf)[i * 4];
						((uint16_t*)outPictureData)[curOutIndex + i] = ((uint16_t)r << 8) | r;
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_RG32:
				{
					if ((curOutIndex > pTex->_pictureDataSize / 4) || (pTex->_pictureDataSize / 4 - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t r = ((uint8_t*)pRGBA32Buf)[i * 4];
						uint8_t g = ((uint8_t*)pRGBA32Buf)[i * 4 + 1];
						((uint16_t*)outPictureData)[(curOutIndex + i) * 2] = ((uint16_t)r << 8) | r;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 2 + 1] = ((uint16_t)g << 8) | g;
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_RGB48:
				{
					if ((curOutIndex > pTex->_pictureDataSize / 6) || (pTex->_pictureDataSize / 6 - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t r = ((uint8_t*)pRGBA32Buf)[i * 4];
						uint8_t g = ((uint8_t*)pRGBA32Buf)[i * 4 + 1];
						uint8_t b = ((uint8_t*)pRGBA32Buf)[i * 4 + 2];
						((uint16_t*)outPictureData)[(curOutIndex + i) * 3] = ((uint16_t)r << 8) | r;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 3 + 1] = ((uint16_t)g << 8) | g;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 3 + 2] = ((uint16_t)b << 8) | b;
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_RGBA64:
				{
					if ((curOutIndex > pTex->_pictureDataSize / 8) || (pTex->_pictureDataSize / 8 - curOutIndex) < (curWidth * curHeight))
						return false;
					uint32_t pixelCount = curWidth * curHeight;
					for (uint32_t i = 0; i < pixelCount; i++)
					{
						uint8_t r = ((uint8_t*)pRGBA32Buf)[i * 4];
						uint8_t g = ((uint8_t*)pRGBA32Buf)[i * 4 + 1];
						uint8_t b = ((uint8_t*)pRGBA32Buf)[i * 4 + 2];
						uint8_t a = ((uint8_t*)pRGBA32Buf)[i * 4 + 3];
						((uint16_t*)outPictureData)[(curOutIndex + i) * 4] = ((uint16_t)r << 8) | r;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 4 + 1] = ((uint16_t)g << 8) | g;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 4 + 2] = ((uint16_t)b << 8) | b;
						((uint16_t*)outPictureData)[(curOutIndex + i) * 4 + 3] = ((uint16_t)a << 8) | a;
					}
					curOutIndex += (curWidth * curHeight);
				}
				break;
			case TexFmt_DXT1:
			case TexFmt_DXT1Crunched:
			case TexFmt_DXT5:
			case TexFmt_DXT5Crunched:
			case TexFmt_BC4:
			case TexFmt_BC5:
			case TexFmt_BC6H:
			case TexFmt_BC7:
			case TexFmt_YUV2: //not actually compressed but it's also handled
			case TexFmt_EAC_R:
			case TexFmt_EAC_R_SIGNED:
			case TexFmt_EAC_RG:
			case TexFmt_EAC_RG_SIGNED:
			case TexFmt_ETC_RGB4:
			case TexFmt_ETC_RGB4Crunched:
			case TexFmt_ETC_RGB4_3DS:
			case TexFmt_ETC_RGBA8_3DS:
			case TexFmt_ETC2_RGB4:
			case TexFmt_ETC2_RGBA1:
			case TexFmt_ETC2_RGBA8:
			case TexFmt_ETC2_RGBA8Crunched:
			case TexFmt_PVRTC_RGB2:
			case TexFmt_PVRTC_RGBA2:
			case TexFmt_PVRTC_RGB4:
			case TexFmt_PVRTC_RGBA4:
			case TexFmt_ASTC_RGB_4x4:
			case TexFmt_ASTC_RGB_5x5:
			case TexFmt_ASTC_RGB_6x6:
			case TexFmt_ASTC_RGB_8x8:
			case TexFmt_ASTC_RGB_10x10:
			case TexFmt_ASTC_RGB_12x12:
			case TexFmt_ASTC_RGBA_4x4:
			case TexFmt_ASTC_RGBA_5x5:
			case TexFmt_ASTC_RGBA_6x6:
			case TexFmt_ASTC_RGBA_8x8:
			case TexFmt_ASTC_RGBA_10x10:
			case TexFmt_ASTC_RGBA_12x12:
			case TexFmt_ASTC_HDR_4x4:
			case TexFmt_ASTC_HDR_5x5:
			case TexFmt_ASTC_HDR_6x6:
			case TexFmt_ASTC_HDR_8x8:
			case TexFmt_ASTC_HDR_10x10:
			case TexFmt_ASTC_HDR_12x12:
				{
					QWORD compressedSize = pTex->_pictureDataSize - curOutIndex;
					RGBA32_ToCompressed(pTex, &pTex->pPictureData[curOutIndex], pRGBA32Buf, compressedSize, compressQuality, curWidth, curHeight);
					/*pTex->_pictureDataSize = pTex->m_CompleteImageSize = (uint32_t)*/curOutIndex += compressedSize;
					if (compressedSize == 0 && (curWidth * curHeight) != 0)
					{
						printf("Failed converting texture format RGBA32 to %i!", pTex->m_TextureFormat);
						return false;
					}
				}
				break;
			default:
				printf("Unsupported texture format %i!", pTex->m_TextureFormat);
				return false;
		}
		totalMipCount++;
		if ((curMipCount > 1) && MakeNextMipmapLevel_RGBA32(pRGBA32Buf, pRGBA32Buf, curWidth, curHeight))
			curMipCount--;
		else
			break;
	} while (true);
	pTex->_pictureDataSize = pTex->m_CompleteImageSize = (uint32_t)curOutIndex;
	pTex->m_MipCount = totalMipCount;
	//printf("Successfully converted texture format RGBA32 to %i!", pTex->m_TextureFormat);
	return true;
}

bool GetTextureData(TextureFile *pTex, void *pOutBuf)
{
	switch (pTex->m_TextureFormat) {
		case TexFmt_Alpha8:
        case TexFmt_RGB24:
        case TexFmt_RGBA32:
        case TexFmt_BGRA32Old:
        case TexFmt_BGRA32New:
        case TexFmt_ARGB32:
        case TexFmt_ARGB4444:
        case TexFmt_RGBA4444:
        case TexFmt_RGB565:
		case TexFmt_R16:
		case TexFmt_RHalf:
		case TexFmt_RGHalf:
		case TexFmt_RGBAHalf:
		case TexFmt_RFloat:
		case TexFmt_RGFloat:
		case TexFmt_RGBAFloat:
		case TexFmt_RGB9e5Float:
		case TexFmt_RG16:
		case TexFmt_R8:
			if (!Uncompressed_ToRGBA32(pTex, pOutBuf))
			{
				return false;
			}
			break;
		case TexFmt_DXT1:
		case TexFmt_DXT1Crunched:
        case TexFmt_DXT5:
        case TexFmt_DXT5Crunched:
		case TexFmt_BC4:
		case TexFmt_BC5:
		case TexFmt_BC6H:
		case TexFmt_BC7:
		case TexFmt_YUV2: //not actually compressed but it's also handled
		case TexFmt_EAC_R:
		case TexFmt_EAC_R_SIGNED:
		case TexFmt_EAC_RG:
		case TexFmt_EAC_RG_SIGNED:
		case TexFmt_ETC_RGB4:
		case TexFmt_ETC_RGB4Crunched:
		case TexFmt_ETC_RGB4_3DS:
		case TexFmt_ETC_RGBA8_3DS:
		case TexFmt_ETC2_RGB4:
		case TexFmt_ETC2_RGBA1:
		case TexFmt_ETC2_RGBA8:
		case TexFmt_ETC2_RGBA8Crunched:
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA4:
		case TexFmt_ASTC_RGB_4x4:
		case TexFmt_ASTC_RGB_5x5:
		case TexFmt_ASTC_RGB_6x6:
		case TexFmt_ASTC_RGB_8x8:
		case TexFmt_ASTC_RGB_10x10:
		case TexFmt_ASTC_RGB_12x12:
		case TexFmt_ASTC_RGBA_4x4:
		case TexFmt_ASTC_RGBA_5x5:
		case TexFmt_ASTC_RGBA_6x6:
		case TexFmt_ASTC_RGBA_8x8:
		case TexFmt_ASTC_RGBA_10x10:
		case TexFmt_ASTC_RGBA_12x12:
			if (!Compressed_ToRGBA32(pTex, pOutBuf))
			{
				printf("Failed decompressing from texture format %i!", pTex->m_TextureFormat);
				return false;
			}
			break;
		default:
			printf("Unsupported texture format %i!", pTex->m_TextureFormat);
			return false;
	}
	//printf("Successfully converted texture format %i to RGBA32!", pTex->m_TextureFormat);
	return true;
}
