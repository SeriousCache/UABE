#pragma once
#include "defines.h"
#include "AssetTypeClass.h"

#define MinTextureFileSize 0x38
struct TextureFile
{
	struct {
		int textureFormatVersion = 0;       //version code returned by SupportsTextureFormat
	} extra;

	std::string m_Name;
	int m_ForcedFallbackFormat = 0;         //added with Unity 2017.3
	bool m_DownscaleFallback = false;       //added with Unity 2017.3
	bool m_IsAlphaChannelOptional = false;  //added with Unity 2020.2
	unsigned int m_Width = 0;
	unsigned int m_Height = 0;
	uint32_t m_CompleteImageSize = 0;
	int m_MipsStripped = 0;                 //added with Unity 2020.1
	uint32_t m_TextureFormat = 0;			//Values from enum TextureFormat
	int m_MipCount = 0;					    //added with Unity 5.2
	bool m_MipMap = false;					//removed with Unity 5.2
	bool m_IsReadable = false;
	bool m_IsPreProcessed = false;          //added with Unity 2019.4
	bool m_ReadAllowed = false;				//removed with Unity 5.5
	bool m_IgnoreMasterTextureLimit = false;//added with Unity 2019.3
	bool m_StreamingMipmaps = false;		//added with Unity 2018.2
	int m_StreamingMipmapsPriority = 0;	    //added with Unity 2018.2
	int m_ImageCount = 0;
	int m_TextureDimension = 0;
	struct GLTextureSettings
	{
		int m_FilterMode;			        //FilterMode : Point, Bilinear, Trilinear
		int m_Aniso;				        //Anisotropic filtering level.
		float m_MipBias;
		int m_WrapMode;				        //removed with Unity 2017.1 //0x28 or 0x2C; TextureWrapMode : Repeat, Clamp
		int m_WrapU;				        //added with Unity 2017.1
		int m_WrapV;				        //added with Unity 2017.1
		int m_WrapW;				        //added with Unity 2017.1
	} m_TextureSettings = {};
	int m_LightmapFormat = 0;               //LightmapsMode(?) : NonDirectional, CombinedDirectional, SeparateDirectional
	int m_ColorSpace = 0;                   //added with Unity 3.5 //ColorSpace : Gamma, Linear
	std::vector<uint8_t> m_PlatformBlob;    //added with Unity 2020.2
	uint32_t _pictureDataSize = 0;          //The same as m_CompleteImageSize if m_StreamData is empty.
	uint8_t *pPictureData = nullptr;
	struct StreamingInfo                    //added with Unity 5.3
	{
		uint64_t offset;
		unsigned int size;
		std::string path;
	} m_StreamData;
};
enum TextureFormat { //by disunity and UnityEngine.dll
	TexFmt_Alpha8=1, //Unity 1.5 or earlier (already in 1.2.2 according to documentation)
	TexFmt_ARGB4444, //Unity 3.0 (already in 1.2.2)
	TexFmt_RGB24, //Unity 1.5 or earlier (already in 1.2.2)
	TexFmt_RGBA32, //Unity 3.2 (not sure about 1.2.2)
	TexFmt_ARGB32, //Unity 1.5 or earlier (already in 1.2.2)
	TexFmt_UNUSED06,
	TexFmt_RGB565, //Unity 3.0 (already in 1.2.2)
	TexFmt_UNUSED08,
	TexFmt_R16, //Unity 5.0
	TexFmt_DXT1, //Unity 2.0 (already in 1.2.2)
	TexFmt_UNUSED11, //(DXT3 in 1.2.2?)
	TexFmt_DXT5, //Unity 2.0
	TexFmt_RGBA4444, //Unity 4.1
	TexFmt_BGRA32New, //Unity 4.5
	TexFmt_RHalf, //Unity 5.0
	TexFmt_RGHalf, //Unity 5.0
	TexFmt_RGBAHalf, //Unity 5.0
	TexFmt_RFloat, //Unity 5.0
	TexFmt_RGFloat, //Unity 5.0
	TexFmt_RGBAFloat, //Unity 5.0
	TexFmt_YUV2, //Unity 5.0
	TexFmt_RGB9e5Float, //Unity 5.6
	TexFmt_UNUSED23,
	TexFmt_BC6H, //Unity 5.5
	TexFmt_BC7, //Unity 5.5
	TexFmt_BC4, //Unity 5.5
	TexFmt_BC5, //Unity 5.5
	TexFmt_DXT1Crunched, //Unity 5.0 //SupportsTextureFormat version codes 0 (original) and 1 (Unity 2017.3)
	TexFmt_DXT5Crunched, //Unity 5.0 //SupportsTextureFormat version codes 0 (original) and 1 (Unity 2017.3)
	TexFmt_PVRTC_RGB2, //Unity 2.6
	TexFmt_PVRTC_RGBA2, //Unity 2.6
	TexFmt_PVRTC_RGB4, //Unity 2.6
	TexFmt_PVRTC_RGBA4, //Unity 2.6
	TexFmt_ETC_RGB4, //Unity 3.0
	TexFmt_ATC_RGB4, //Unity 3.4, removed in 2018.1
	TexFmt_ATC_RGBA8, //Unity 3.4, removed in 2018.1
	TexFmt_BGRA32Old, //Unity 3.4, removed in Unity 4.5
	TexFmt_UNUSED38, //TexFmt_ATF_RGB_DXT1, added in Unity 3.5, removed in Unity 5.0
	TexFmt_UNUSED39, //TexFmt_ATF_RGBA_JPG, added in Unity 3.5, removed in Unity 5.0
	TexFmt_UNUSED40, //TexFmt_ATF_RGB_JPG, added in Unity 3.5, removed in Unity 5.0
	TexFmt_EAC_R,         //Unity 4.5
	TexFmt_EAC_R_SIGNED,  //Unity 4.5
	TexFmt_EAC_RG,        //Unity 4.5
	TexFmt_EAC_RG_SIGNED, //Unity 4.5
	TexFmt_ETC2_RGB4,     //Unity 4.5
	TexFmt_ETC2_RGBA1,    //Unity 4.5 //R4G4B4A1
	TexFmt_ETC2_RGBA8,    //Unity 4.5 //R8G8B8A8
	TexFmt_ASTC_RGB_4x4=48, //Unity 4.5, removed (2019.1: replaced by ASTC_4x4)   exists for version code < 2
	TexFmt_ASTC_RGB_5x5,    //Unity 4.5, removed (2019.1: replaced by ASTC_5x5)	  exists for version code < 2
	TexFmt_ASTC_RGB_6x6,    //Unity 4.5, removed (2019.1: replaced by ASTC_6x6)	  exists for version code < 2
	TexFmt_ASTC_RGB_8x8,    //Unity 4.5, removed (2019.1: replaced by ASTC_8x8)	  exists for version code < 2
	TexFmt_ASTC_RGB_10x10,  //Unity 4.5, removed (2019.1: replaced by ASTC_10x10) exists for version code < 2
	TexFmt_ASTC_RGB_12x12,  //Unity 4.5, removed (2019.1: replaced by ASTC_12x12) exists for version code < 2
	TexFmt_ASTC_RGBA_4x4,   //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ASTC_RGBA_5x5,   //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ASTC_RGBA_6x6,   //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ASTC_RGBA_8x8,   //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ASTC_RGBA_10x10, //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ASTC_RGBA_12x12, //Unity 4.5, removed (obsoleted in 2019.1)
	TexFmt_ETC_RGB4_3DS,    //Unity 5.0, removed (obsoleted in 2018.3, "Nintendo 3DS no longer supported")
	TexFmt_ETC_RGBA8_3DS,   //Unity 5.0, removed (obsoleted in 2018.3, "Nintendo 3DS no longer supported")
	TexFmt_RG16, //Unity 2017.1
	TexFmt_R8,   //Unity 2017.1
	TexFmt_ETC_RGB4Crunched, //Unity 2017.3  //SupportsTextureFormat version code 1
	TexFmt_ETC2_RGBA8Crunched, //Unity 2017.3  //SupportsTextureFormat version code 1
	TexFmt_ASTC_HDR_4x4,   //Unity 2019.1
	TexFmt_ASTC_HDR_5x5,   //Unity 2019.1
	TexFmt_ASTC_HDR_6x6,   //Unity 2019.1
	TexFmt_ASTC_HDR_8x8,   //Unity 2019.1
	TexFmt_ASTC_HDR_10x10, //Unity 2019.1
	TexFmt_ASTC_HDR_12x12, //Unity 2019.1
	TexFmt_RG32,   //Unity 2020.2
	TexFmt_RGB48,  //Unity 2020.2
	TexFmt_RGBA64, //Unity 2020.2

	TexFmt_ASTC_4x4 = 48, //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
	TexFmt_ASTC_5x5,      //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
	TexFmt_ASTC_6x6,      //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
	TexFmt_ASTC_8x8,      //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
	TexFmt_ASTC_10x10,    //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
	TexFmt_ASTC_12x12,    //Unity 2019.1  exists for version code >= 2; Format equivalent to old ASTC_RGBA_*
};

//Fills in a TextureFile structure using the values from the value field.
ASSETSTOOLS_API bool ReadTextureFile(TextureFile *pOutTex, AssetTypeValueField *pBaseField);
//Writes a TextureFile structure into a Texture2D AssetTypeValueField.
// Any memory allocated by WriteTextureFile is pushed back to allocatedMemory.
// Note: Directly uses buffer pointers from the texture in the values (e.g. pInTex->m_Name)!
//       -> pInTex should not be touched until pBaseField is freed.
//Can throw a AssetTypeValue_ConfusionError.
ASSETSTOOLS_API bool WriteTextureFile(TextureFile* pInTex, AssetTypeValueField* pBaseField,
	std::vector<std::unique_ptr<uint8_t[]>> &allocatedMemory);
//Determines whether an AssetsFile supports a specific texture format.
// version is set to 2 for 2019.1+, else to 1 for 2017.3+ and else to 0 to mark breaking format changes.
ASSETSTOOLS_API bool SupportsTextureFormat(AssetsFile *pAssetsFile, TextureFormat texFmt, int &version);
//Retrieves RGBA32 (byte order) texture data from a previously initialized TextureFile structure. pOutBuf is expected to have at least width*height*4 bytes of space.
ASSETSTOOLS_API bool GetTextureData(TextureFile *pTex, void *pOutBuf);
//Retrieves the size of compressed data of a specific texture format.
ASSETSTOOLS_API size_t GetCompressedTextureDataSize(int width, int height, TextureFormat texFmt);
//Converts the RGBA32 (byte order) input buffer for the texture format specified in the TextureFile structure. The output buffer is pPictureData (_pictureDataSize).
ASSETSTOOLS_API bool MakeTextureData(TextureFile *pTex, void *pRGBA32Buf, int compressQuality = 0);
//ASSETSTOOLS_API unsigned int GetCompressedTextureDataSizeCrunch(TextureFile *pTex);