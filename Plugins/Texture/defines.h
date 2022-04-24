#pragma once

#include <vector>
#include <wchar.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "resource.h"

#include "../UABE_Generic/PluginManager.h"
#include <TextureFileFormat.h>
#include <AssetsReplacer.h>
#include <AssetsFileReader.h>

//following enum : Partially Copyright (c) Imagination Technologies Limited.
enum ECompressorQuality
{
	eDXTnNormal=0,
	eDXTnNormalMt,
	eDXTnVeryFast,
	eDXTnVeryFastMt,
	eDXTnSlow,
	eDXTnSlowMt,

	ePVRTCFastest=0,        //!< PVRTC fastest
	ePVRTCFast,             //!< PVRTC fast
	ePVRTCNormal,           //!< PVRTC normal
	ePVRTCHigh,             //!< PVRTC high
	ePVRTCBest,             //!< PVRTC best
	eNumPVRTCModes,         //!< Number of PVRTC modes

	eETCFast=0,             //!< ETC fast
	eETCFastPerceptual,     //!< ETC fast perceptual
	eETCSlow,               //!< ETC slow
	eETCSlowPerceptual,     //!< ETC slow perceptual
	eNumETCModes,           //!< Number of ETC modes

	eASTCVeryFast=0,        //!< ASTC very fast
	eASTCFast,              //!< ASTC fast
	eASTCMedium,            //!< ASTC medium
	eASTCThorough,          //!< ASTC thorough
	eASTCExhaustive,        //!< ASTC exhaustive
	eNumASTCModes,          //!< Number of ASTC modes
	//following entries : custom
	eBCnVeryFast=0,			//!< Texgenpack Ultra preset
	eBCnFast=8,				//!< Texgenpack Fast preset
	eBCnMedium=16,			//!< Texgenpack Medium preset
	eBCnSlow=32,			//!< Texgenpack Slow preset
	eBCnVerySlow=40,		//!< Texgenpack Very Slow (COMPRESSION_LEVEL_CLASS2+7)
	eBCnPlacebo=50,			//!< Texgenpack Placebo (COMPRESSION_LEVEL_CLASS2+17)

	eBC6HVeryFast=0,		//!< ISPC Compressor Very Fast preset
	eBC6HFast,				//!< ISPC Compressor Fast preset
	eBC6HBasic,				//!< ISPC Compressor Basic preset
	eBC6HSlow,				//!< ISPC Compressor Slow preset
	eBC6HVerySlow,			//!< ISPC Compressor Very Slow preset
	eBC6HVeryFastMT,		//!< ISPC Compressor Very Fast preset, multithread
	eBC6HFastMT,			//!< ISPC Compressor Fast preset, multithread
	eBC6HBasicMT,			//!< ISPC Compressor Basic preset, multithread
	eBC6HSlowMT,			//!< ISPC Compressor Slow preset, multithread
	eBC6HVerySlowMT,		//!< ISPC Compressor Very Slow preset, multithread
	
	eBC7UltraFast=0,		//!< ISPC Compressor Ultra Fast preset, multithread
	eBC7VeryFast,			//!< ISPC Compressor Very Fast preset, multithread
	eBC7Fast,				//!< ISPC Compressor Fast preset, multithread
	eBC7Basic,				//!< ISPC Compressor Basic preset, multithread
	eBC7Slow,				//!< ISPC Compressor Slow preset, multithread
	eBC7UltraFastMT,		//!< ISPC Compressor Ultra Fast preset, multithread
	eBC7VeryFastMT,			//!< ISPC Compressor Very Fast preset, multithread
	eBC7FastMT,				//!< ISPC Compressor Fast preset, multithread
	eBC7BasicMT,			//!< ISPC Compressor Basic preset, multithread
	eBC7SlowMT,				//!< ISPC Compressor Slow preset, multithread
};


class TextureImportParam
{
public:
	AssetIdentifier asset;
	std::vector<size_t> hideDialogElementsList; //Indices to ImportDialogPairs_size

	std::string importTextureName;
	bool assetWasTextureFile;
	//If importTextureInfo.pPictureData == NULL but importTextureData is not empty,
	//  importTextureData contains the RGBA32 data to override the original texture with,
	//  and does not contain any mip maps.
	//Otherwise, importTextureInfo.pPictureData is set to importTextureData.data().
	TextureFile importTextureInfo;
	std::vector<uint8_t> importTextureData;
	inline void assignCompressedTextureData(std::vector<uint8_t> _data)
	{
		this->importTextureData = std::move(_data);
		importTextureInfo.pPictureData = this->importTextureData.data();
	}
	bool textureDataModified;

	class
	{
	public:
		bool isBatchEntry;
		std::string batchFilenameOverride;

		bool hasNewSettings;

		std::string newName;
		unsigned int newTextureFormat;
		bool newMipMap;
		bool newReadable;
		bool newReadAllowed;
		unsigned int newFilterMode;
		unsigned int newAnisoLevel;
		float newMipBias;
		unsigned int newWrapMode;
		unsigned int newWrapModeU;
		unsigned int newWrapModeV;
		unsigned int newLightmapFmt;
		unsigned int newColorSpace;
	} batchInfo;

	ECompressorQuality qualitySelection;
	bool qualitySelected;
public:
	inline TextureImportParam(AssetIdentifier asset,
		bool assetWasTextureFile, bool isBatchImportEntry,
		ECompressorQuality qualitySelection = ePVRTCFastest)
	{
		this->asset = asset;
		this->assetWasTextureFile = assetWasTextureFile;
		this->batchInfo.isBatchEntry = isBatchImportEntry;
		this->batchInfo.hasNewSettings = false;
		this->qualitySelection = qualitySelection;
		this->qualitySelected = false;
		this->textureDataModified = false;
	}
};


//Retrieves a list of compression preset names for a specified texture format and returns the index of the default preset.
static size_t GetQualityPresetList(unsigned int textureFormat, std::vector<const wchar_t*> &names)
{
	switch (textureFormat)
	{
		case TexFmt_DXT1:
		case TexFmt_DXT5:
		case TexFmt_DXT1Crunched:
		case TexFmt_DXT5Crunched:
		case TexFmt_ETC_RGB4Crunched:
		case TexFmt_ETC2_RGBA8Crunched:
			names.push_back(TEXT("Very Fast"));
			names.push_back(TEXT("Very Fast (multithread)"));
			names.push_back(TEXT("Normal"));
			names.push_back(TEXT("Normal (multithread)"));
			names.push_back(TEXT("Slow"));
			names.push_back(TEXT("Slow (multithread)"));
			return 3;
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
			names.push_back(TEXT("Very Fast"));
			names.push_back(TEXT("Fast"));
			names.push_back(TEXT("Medium"));
			names.push_back(TEXT("Thorough"));
			names.push_back(TEXT("Exhaustive"));
			return 2;
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
			names.push_back(TEXT("Fast"));
			names.push_back(TEXT("Fast Perceptual"));
			names.push_back(TEXT("Slow"));
			names.push_back(TEXT("Slow Perceptual"));
			return 2;
		case TexFmt_PVRTC_RGB2:
		case TexFmt_PVRTC_RGB4:
		case TexFmt_PVRTC_RGBA2:
		case TexFmt_PVRTC_RGBA4:
			names.push_back(TEXT("Fastest"));
			names.push_back(TEXT("Fast"));
			names.push_back(TEXT("Normal"));
			names.push_back(TEXT("High"));
			names.push_back(TEXT("Best"));
			return 2;
		case TexFmt_BC4:
		case TexFmt_BC5:
			names.push_back(TEXT("Very Fast"));
			names.push_back(TEXT("Fast"));
			names.push_back(TEXT("Medium"));
			names.push_back(TEXT("Slow"));
			names.push_back(TEXT("Very Slow"));
			names.push_back(TEXT("Placebo"));
			return 2;
		case TexFmt_BC6H:
			names.push_back(TEXT("Very Fast")); //0
			names.push_back(TEXT("Very Fast (multithread)")); //5
			names.push_back(TEXT("Fast")); //1
			names.push_back(TEXT("Fast (multithread)")); //6
			names.push_back(TEXT("Basic")); //2
			names.push_back(TEXT("Basic (multithread)")); //7
			names.push_back(TEXT("Slow")); //3
			names.push_back(TEXT("Slow (multithread)")); //8
			names.push_back(TEXT("Very Slow")); //4
			names.push_back(TEXT("Very Slow (multithread)")); //9
			return 7;
		case TexFmt_BC7:
			names.push_back(TEXT("Ultra Fast"));
			names.push_back(TEXT("Ultra Fast (multithread)"));
			names.push_back(TEXT("Very Fast"));
			names.push_back(TEXT("Very Fast (multithread)"));
			names.push_back(TEXT("Fast"));
			names.push_back(TEXT("Fast (multithread)"));
			names.push_back(TEXT("Basic"));
			names.push_back(TEXT("Basic (multithread)"));
			names.push_back(TEXT("Slow"));
			names.push_back(TEXT("Slow (multithread)"));
			return 7;
		default:
			return (size_t)-1;
	}
}
static ECompressorQuality GetQualityFromPresetListIdx(unsigned int textureFormat, size_t index)
{
	ECompressorQuality qualitySelection = (ECompressorQuality)index;
	switch (textureFormat)
	{
		case TexFmt_DXT1:
		case TexFmt_DXT5:
		case TexFmt_DXT1Crunched:
		case TexFmt_DXT5Crunched:
		case TexFmt_ETC_RGB4Crunched:
		case TexFmt_ETC2_RGBA8Crunched:
			switch (qualitySelection)
			{
				case 0:
					qualitySelection = eDXTnVeryFast;
					break;
				case 1:
					qualitySelection = eDXTnVeryFastMt;
					break;
				case 2:
					qualitySelection = eDXTnNormal;
					break;
				default:
				case 3:
					qualitySelection = eDXTnNormalMt;
					break;
				case 4:
					qualitySelection = eDXTnSlow;
					break;
				case 5:
					qualitySelection = eDXTnSlowMt;
					break;
			}
			break;
		case TexFmt_BC4:
		case TexFmt_BC5:
			switch (qualitySelection)
			{
				default:
				case 0:
					qualitySelection = eBCnVeryFast;
					break;
				case 1:
					qualitySelection = eBCnFast;
					break;
				case 2:
					qualitySelection = eBCnMedium;
					break;
				case 3:
					qualitySelection = eBCnSlow;
					break;
				case 4:
					qualitySelection = eBCnVerySlow;
					break;
				case 5:
					qualitySelection = eBCnPlacebo;
					break;
			}
			break;
		case TexFmt_BC6H:
		case TexFmt_BC7:
			if (qualitySelection < 0 || qualitySelection > eBC6HVerySlowMT)
				qualitySelection = eBC6HBasic;
			else if (qualitySelection & 1) //qualitySelection % 2, multithread
				qualitySelection = (ECompressorQuality)(eBC6HVeryFastMT + (qualitySelection >> 1));
			else
				qualitySelection = (ECompressorQuality)(qualitySelection >> 1);
			break;
		default:
			if (qualitySelection < 0 || qualitySelection >= eNumPVRTCModes)
				qualitySelection = ePVRTCFastest;
			break;
	}
	return qualitySelection;
}

struct TextureNameIDPair
{
	const TCHAR *name;
	TextureFormat textureType;
	int versionRangeMin; //-1 for no minimum (see SupportsTextureFormat)
	int versionRangeMax; //-1 for no maximum
	char sizeMul;
	bool showQualityDialog;
};

static const TextureNameIDPair SupportedTextureNames[] = {
	{TEXT("ARGB32"), TexFmt_ARGB32, -1, -1, 4, false},
	{TEXT("BGRA32"), TexFmt_BGRA32New, -1, -1, 4, false}, //TexFmt_BGRA32Old or TexFmt_BGRA32New
	{TEXT("RGBA32"), TexFmt_RGBA32, -1, -1, 4, false},
	{TEXT("RGB24"), TexFmt_RGB24, -1, -1, 3, false},
	{TEXT("ARGB4444"), TexFmt_ARGB4444, -1, -1, 2, false},
	{TEXT("RGBA4444"), TexFmt_RGBA4444, -1, -1, 2, false},
	{TEXT("RGB565"), TexFmt_RGB565, -1, -1, 2, false},
	{TEXT("Alpha8"), TexFmt_Alpha8, -1, -1, 1, false},
	{TEXT("R8"), TexFmt_R8, -1, -1, 1, false},
	{TEXT("R16"), TexFmt_R16, -1, -1, 2, false},
	{TEXT("RG16"), TexFmt_RG16, -1, -1, 2, false},
	{TEXT("RHalf"), TexFmt_RHalf, -1, -1, 2, false},
	{TEXT("RGHalf"), TexFmt_RGHalf, -1, -1, 4, false},
	{TEXT("RGBAHalf"), TexFmt_RGBAHalf, -1, -1, 8, false},
	{TEXT("RFloat"), TexFmt_RFloat, -1, -1, 4, false},
	{TEXT("RGFloat"), TexFmt_RGFloat, -1, -1, 8, false},
	{TEXT("RGBAFloat"), TexFmt_RGBAFloat, -1, -1, 16, false},
	{TEXT("RGB9e5Float"), TexFmt_RGB9e5Float, -1, -1, 4, false},
	{TEXT("RG32"), TexFmt_RG32, -1, -1, 4, false},
	{TEXT("RGB48"), TexFmt_RGB48, -1, -1, 6, false},
	{TEXT("RGBA64"), TexFmt_RGBA64, -1, -1, 8, false},
	{TEXT("YUV2"), TexFmt_YUV2, -1, -1, 2, false},
	{TEXT("DXT1"), TexFmt_DXT1, -1, -1, -1, true},
	{TEXT("DXT1Crunched (slow!)"), TexFmt_DXT1Crunched, -1, 0, -1, true},
	{TEXT("DXT1Crunched"), TexFmt_DXT1Crunched, 1, -1, -1, true},
	{TEXT("DXT5"), TexFmt_DXT5, -1, -1, -1, true},
	{TEXT("DXT5Crunched (slow!)"), TexFmt_DXT5Crunched, -1, 0, -1, true},
	{TEXT("DXT5Crunched"), TexFmt_DXT5Crunched, 1, -1, -1, true},
	{TEXT("EAC_R"), TexFmt_EAC_R, -1, -1, -1, true},
	{TEXT("EAC_R_SIGNED"), TexFmt_EAC_R_SIGNED, -1, -1, -1, true},
	{TEXT("EAC_RG"), TexFmt_EAC_RG, -1, -1, -1, true},
	{TEXT("EAC_RG_SIGNED"), TexFmt_EAC_RG_SIGNED, -1, -1, -1, true},
	{TEXT("ETC_RGB4"), TexFmt_ETC_RGB4, -1, -1, -1, true},
	{TEXT("ETC_RGB4Crunched"), TexFmt_ETC_RGB4Crunched, -1, -1, -1, true},
	{TEXT("ETC2_RGBA8Crunched"), TexFmt_ETC2_RGBA8Crunched, -1, -1, -1, true},
	{TEXT("ETC_RGB4_3DS"), TexFmt_ETC_RGB4_3DS, -1, -1, -1, true},
	{TEXT("ETC_RGBA8_3DS"), TexFmt_ETC_RGBA8_3DS, -1, -1, -1, true},
	{TEXT("ETC2_RGB4"), TexFmt_ETC2_RGB4, -1, -1, -1, true},
	{TEXT("ETC2_RGB_A1"), TexFmt_ETC2_RGBA1, -1, -1, -1, true},
	{TEXT("ETC2_RGBA8"), TexFmt_ETC2_RGBA8, -1, -1, -1, true},
	{TEXT("PVRTC_RGB2"), TexFmt_PVRTC_RGB2, -1, -1, -1, true},
	{TEXT("PVRTC_RGBA2"), TexFmt_PVRTC_RGBA2, -1, -1, -1, true},
	{TEXT("PVRTC_RGB4"), TexFmt_PVRTC_RGB4, -1, -1, -1, true},
	{TEXT("PVRTC_RGBA4"), TexFmt_PVRTC_RGBA4, -1, -1, -1, true},
	{TEXT("ASTC_4x4"), TexFmt_ASTC_4x4,     2, -1, -1, true},
	{TEXT("ASTC_5x5"), TexFmt_ASTC_5x5,     2, -1, -1, true},
	{TEXT("ASTC_6x6"), TexFmt_ASTC_6x6,     2, -1, -1, true},
	{TEXT("ASTC_8x8"), TexFmt_ASTC_8x8,     2, -1, -1, true},
	{TEXT("ASTC_10x10"), TexFmt_ASTC_10x10, 2, -1, -1, true},
	{TEXT("ASTC_12x12"), TexFmt_ASTC_12x12, 2, -1, -1, true},
	{TEXT("ASTC_RGB_4x4"), TexFmt_ASTC_RGB_4x4,     -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGB_5x5"), TexFmt_ASTC_RGB_5x5,     -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGB_6x6"), TexFmt_ASTC_RGB_6x6,     -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGB_8x8"), TexFmt_ASTC_RGB_8x8,     -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGB_10x10"), TexFmt_ASTC_RGB_10x10, -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGB_12x12"), TexFmt_ASTC_RGB_12x12, -1, 1, -1, true},  //Same ID as ASTC_*x*
	{TEXT("ASTC_RGBA_4x4"), TexFmt_ASTC_RGBA_4x4,     -1, 1, -1, true},
	{TEXT("ASTC_RGBA_5x5"), TexFmt_ASTC_RGBA_5x5,     -1, 1, -1, true},
	{TEXT("ASTC_RGBA_6x6"), TexFmt_ASTC_RGBA_6x6,     -1, 1, -1, true},
	{TEXT("ASTC_RGBA_8x8"), TexFmt_ASTC_RGBA_8x8,     -1, 1, -1, true},
	{TEXT("ASTC_RGBA_10x10"), TexFmt_ASTC_RGBA_10x10, -1, 1, -1, true},
	{TEXT("ASTC_RGBA_12x12"), TexFmt_ASTC_RGBA_12x12, -1, 1, -1, true},
	{TEXT("ASTC_HDR_4x4"), TexFmt_ASTC_HDR_4x4,     -1, -1, -1, true},
	{TEXT("ASTC_HDR_5x5"), TexFmt_ASTC_HDR_5x5,     -1, -1, -1, true},
	{TEXT("ASTC_HDR_6x6"), TexFmt_ASTC_HDR_6x6,     -1, -1, -1, true},
	{TEXT("ASTC_HDR_8x8"), TexFmt_ASTC_HDR_8x8,     -1, -1, -1, true},
	{TEXT("ASTC_HDR_10x10"), TexFmt_ASTC_HDR_10x10, -1, -1, -1, true},
	{TEXT("ASTC_HDR_12x12"), TexFmt_ASTC_HDR_12x12, -1, -1, -1, true},
	{TEXT("BC4"), TexFmt_BC4, -1, -1, -1, false},
	{TEXT("BC5"), TexFmt_BC5, -1, -1, -1, false},
	{TEXT("BC6H"), TexFmt_BC6H, -1, -1, -1, true},
	{TEXT("BC7"), TexFmt_BC7, -1, -1, -1, true},
};
#define SupportedTextureNames_size (sizeof(SupportedTextureNames) / sizeof(TextureNameIDPair))
static bool IsTextureNameIDPairInRange(const TextureNameIDPair *pPair, int version)
{
	return (pPair->versionRangeMin == -1 ? true : pPair->versionRangeMin <= version)
		&& (pPair->versionRangeMax == -1 ? true : pPair->versionRangeMax >= version);
}
static size_t GetTextureNameIDPair(unsigned int textureFormat, int version)
{
	if (textureFormat == TexFmt_BGRA32Old) textureFormat = TexFmt_BGRA32New;
	for (size_t i = 0; i < SupportedTextureNames_size; i++)
	{
		if (SupportedTextureNames[i].textureType == textureFormat 
			&& IsTextureNameIDPairInRange(&SupportedTextureNames[i], version))
			return i;
	}
	return (size_t)-1;
}
static size_t GetTextureDataSize(const TextureNameIDPair *pPair, unsigned int width, unsigned int height)
{
	size_t ret = 0;
	if (pPair->sizeMul <= 0)
	{
		TextureFormat textureType = pPair->textureType;
		if (textureType == TexFmt_DXT1Crunched)
		{
			textureType = TexFmt_DXT1;
			ret = 1024;
		}
		else if (textureType == TexFmt_DXT5Crunched)
		{
			textureType = TexFmt_DXT5;
			ret = 1024;
		}
		else if (textureType == TexFmt_ETC_RGB4Crunched)
		{
			textureType = TexFmt_ETC_RGB4;
			ret = 1024;
		}
		else if (textureType == TexFmt_ETC2_RGBA8Crunched)
		{
			textureType = TexFmt_ETC2_RGBA8;
			ret = 1024;
		}
		ret += GetCompressedTextureDataSize(width, height, textureType);
	}
	else
		ret = width * height * pPair->sizeMul;
	return ret;
}


struct DialogElementPair { int left; int right; short height; short leftOffset; short leftX; short rightX; };
static const DialogElementPair ImportDialogPairs[] = {
	{-1, -1, 13, 0, 0, 0},
	{IDC_SNAME, IDC_ENAME, 30, 3, 15, 124},
	{IDC_STEXFMT, IDC_CBTEXFMT, 27, 3, 15, 124},
	{IDC_SMIPMAP, IDC_CKMIPMAP, 20, 1, 15, 124},
	{IDC_SREADABLE, IDC_CKREADABLE, 20, 1, 15, 124},
	{IDC_SREADALLOWED, IDC_CKREADALLOWED, 21, 1, 15, 124},
	{IDC_SFILTERMODE, IDC_CBFILTERMODE, 28, 3, 15, 124},
	{IDC_SANISO, IDC_EANISO, 30, 3, 15, 124},
	{IDC_SMIPBIAS, IDC_EMIPBIAS, 30, 3, 15, 124},
	{IDC_SWRAPMODE, IDC_CBWRAPMODE, 28, 3, 15, 124},
	{IDC_SWRAPMODEU, IDC_CBWRAPMODEU, 27, 3, 15, 124},
	{IDC_SWRAPMODEV, IDC_CBWRAPMODEV, 28, 3, 15, 124},
	{IDC_SLIGHTMAPFMT, IDC_ELIGHTMAPFMT, 30, 3, 15, 124},
	{IDC_SCLSPACE, IDC_CBCLSPACE, 27, 3, 15, 124},
	{IDC_SLOAD, IDC_BLOAD, 30, 3, 15, 124},
	{IDOK, IDCANCEL, 35, 0, 15, 192}
};
#define ImportDialogPairs_size (sizeof(ImportDialogPairs) / sizeof(DialogElementPair))
static size_t GetImportDialogPairByID(int id)
{
	for (size_t i = 0; i < ImportDialogPairs_size; i++)
	{
		if (ImportDialogPairs[i].left == id || ImportDialogPairs[i].right == id) return i;
	}
	return (size_t)-1;
}
static void InactivateDialogPairsByIdx(HWND hDlg, std::vector<size_t> &indices)
{
	bool inactivate[ImportDialogPairs_size] = {};
	for (size_t i = 0; i < indices.size(); i++)
		if (indices[i] < ImportDialogPairs_size) inactivate[indices[i]] = true;

	int curY = 0;
	for (size_t i = 0; i < ImportDialogPairs_size; i++)
	{
		HWND hLeft = GetDlgItem(hDlg, ImportDialogPairs[i].left);
		HWND hRight = GetDlgItem(hDlg, ImportDialogPairs[i].right);

		if (hLeft)
			SetWindowPos(hLeft, NULL, ImportDialogPairs[i].leftX, curY + ImportDialogPairs[i].leftOffset, 0, 0, 
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
		if (hRight)
			SetWindowPos(hRight, NULL, ImportDialogPairs[i].rightX, curY, 0, 0, 
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);

		if (inactivate[i])
		{
			if (hLeft)
				ShowWindow(hLeft, SW_HIDE);
			if (hRight)
				ShowWindow(hRight, SW_HIDE);
		}
		else
		{
			curY += ImportDialogPairs[i].height;
		}
	}

	RECT dialogRect = {};
	GetWindowRect(hDlg, &dialogRect);
	RECT dialogClientRect = {};
	GetClientRect(hDlg, &dialogClientRect);
	SetWindowPos(hDlg, NULL, 0, 0, 
		dialogRect.right - dialogRect.left, 
		((dialogRect.bottom - dialogRect.top) - (dialogClientRect.bottom - dialogClientRect.top)) + curY, 
		SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOZORDER);
}