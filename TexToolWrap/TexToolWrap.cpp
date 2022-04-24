#include <string.h>
#include <math.h>
#define PVRTEXLIB_IMPORT
#include <PVRTexLib.hpp>
#include "ETexFmts.h"

const PVRTuint64 pvr_ARGB8 = PVRTGENPIXELID4('r', 'g', 'b', 'a', 8, 8, 8, 8);
const PVRTuint64 pvr_RGB8 = PVRTGENPIXELID4('r', 'g', 'b', 0, 8, 8, 8, 0);
size_t Compress(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void *pInBuf, size_t inBufLen, void *pOutBuf, size_t outBufLen, int compressQuality);
unsigned int GetMaxCompressedSize(int width, int height, uint32_t texFmt);
size_t Decompress(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void *pInBuf, size_t inBufLen, void *pOutBuf, size_t outBufLen);

size_t Compress(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void *pInBuf, size_t inBufLen, void *pOutBuf, size_t outBufLen, int compressQuality)
{
	if ((texFmt >= TexFmt_MAX) || (inBufLen < (width * height * 4)) || (outBufLen < GetMaxCompressedSize(width, height, texFmt)))
		return 0;
	pvrtexlib::PVRTextureHeader header(pvr_ARGB8, width, height, 1, mipCount, 1, 1, PVRTLCS_Linear);

	pvrtexlib::PVRTexture tex(header, pInBuf);
	if (!tex.Transcode(PixelTypeByTextureFormat[texFmt], VariableTypeByTextureFormat[texFmt], PVRTLCS_Linear, (PVRTexLibCompressorQuality)compressQuality))
		return 0;
	memcpy(pOutBuf, tex.GetTextureDataPointer(), (outBufLen < tex.GetTextureDataSize()) ? outBufLen : tex.GetTextureDataSize());
	return tex.GetTextureDataSize();
}
unsigned int GetMaxCompressedSize(int width, int height, uint32_t texFmt)
{
	if (texFmt >= TexFmt_MAX)
		return 0;
	switch (texFmt)
	{
	case TexFmt_EAC_R:
	case TexFmt_EAC_RG:
	case TexFmt_EAC_R_SIGNED:
	case TexFmt_EAC_RG_SIGNED:
	case TexFmt_ETC_RGB4:
	case TexFmt_ETC_RGB4_3DS:
	case TexFmt_ETC_RGBA8_3DS:
	case TexFmt_ETC2_RGB4:
	case TexFmt_ETC2_RGBA1:
	case TexFmt_ETC2_RGBA8:
		//the compressions are limited to block sizes
		width = width + ((width & 3) ? 1 : 0);
		height = height + ((height & 3) ? 1 : 0);
		break;
	case TexFmt_ASTC_RGB_4x4:
	case TexFmt_ASTC_RGBA_4x4:
		return (unsigned int)(ceil((double)width / 4.0F) * ceil((double)height / 4.0F)) * 16;
	case TexFmt_ASTC_RGB_5x5:
	case TexFmt_ASTC_RGBA_5x5:
		return (unsigned int)(ceil((double)width / 5.0F) * ceil((double)height / 5.0F)) * 16;
	case TexFmt_ASTC_RGB_6x6:
	case TexFmt_ASTC_RGBA_6x6:
		return (unsigned int)(ceil((double)width / 6.0F) * ceil((double)height / 6.0F)) * 16;
	case TexFmt_ASTC_RGB_8x8:
	case TexFmt_ASTC_RGBA_8x8:
		return (unsigned int)(ceil((double)width / 8.0F) * ceil((double)height / 8.0F)) * 16;
	case TexFmt_ASTC_RGB_10x10:
	case TexFmt_ASTC_RGBA_10x10:
		return (unsigned int)(ceil((double)width / 10.0F) * ceil((double)height / 10.0F)) * 16;
	case TexFmt_ASTC_RGB_12x12:
	case TexFmt_ASTC_RGBA_12x12:
		return (unsigned int)(ceil((double)width / 12.0F) * ceil((double)height / 12.0F)) * 16;
	}
	pvrtexlib::PVRTextureHeader header(PixelTypeByTextureFormat[texFmt], width, height, 1, 1, 1, 1, PVRTLCS_Linear);
	return header.GetTextureDataSize();
}
size_t Decompress(uint32_t texFmt, unsigned int height, unsigned int width, unsigned int mipCount, void *pInBuf, size_t inBufLen, void *pOutBuf, size_t outBufLen)
{
	if (mipCount == (unsigned int)-1)
		mipCount = 1;
	if (texFmt >= TexFmt_MAX)
		return 0;
	pvrtexlib::PVRTextureHeader header(PixelTypeByTextureFormat[texFmt], width, height, 1, mipCount, 1, 1, PVRTLCS_Linear);
	unsigned int bpp = header.GetTextureBitsPerPixel();
	if (((bpp * width * height) > (inBufLen * 8)) || (4 * width * height) > outBufLen)
	{
		return 0;
	}
	else
	{
		pvrtexlib::PVRTexture tex(header, pInBuf);
		if (!tex.Transcode(pvr_ARGB8, PVRTLVT_UnsignedByteNorm, PVRTLCS_Linear, PVRTLCQ_PVRTCBest))
			return 0;

		memcpy(pOutBuf, tex.GetTextureDataPointer(), width * height * 4);
		return width * height * 4;
	}
}
