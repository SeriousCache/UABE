#pragma once
enum TextureFormat {
	TexFmt_Alpha8 = 1,
	TexFmt_ARGB4444,
	TexFmt_RGB24,
	TexFmt_RGBA32,
	TexFmt_ARGB32,
	TexFmt_UNUSED06,
	TexFmt_RGB565,
	TexFmt_UNUSED08,
	TexFmt_R16,
	TexFmt_DXT1,
	TexFmt_UNUSED11,
	TexFmt_DXT5,
	TexFmt_RGBA4444,
	TexFmt_BGRA32New, //Unity 5
	TexFmt_RHalf,
	TexFmt_RGHalf,
	TexFmt_RGBAHalf,
	TexFmt_RFloat,
	TexFmt_RGFloat,
	TexFmt_RGBAFloat,
	TexFmt_YUV2,
	TexFmt_UNUSED22,
	TexFmt_UNUSED23,
	TexFmt_UNUSED24,
	TexFmt_UNUSED25,
	TexFmt_UNUSED26,
	TexFmt_UNUSED27,
	TexFmt_DXT1Crunched,
	TexFmt_DXT5Crunched,
	TexFmt_PVRTC_RGB2,
	TexFmt_PVRTC_RGBA2,
	TexFmt_PVRTC_RGB4,
	TexFmt_PVRTC_RGBA4,
	TexFmt_ETC_RGB4,
	TexFmt_ATC_RGB4,
	TexFmt_ATC_RGBA8,
	TexFmt_BGRA32Old, //Unity 4
	TexFMT_UNUSED38, //TexFmt_ATF_RGB_DXT1,
	TexFMT_UNUSED39, //TexFmt_ATF_RGBA_JPG,
	TexFMT_UNUSED40, //TexFmt_ATF_RGB_JPG,
	TexFmt_EAC_R,
	TexFmt_EAC_R_SIGNED,
	TexFmt_EAC_RG,
	TexFmt_EAC_RG_SIGNED,
	TexFmt_ETC2_RGB4,
	TexFmt_ETC2_RGBA1,
	TexFmt_ETC2_RGBA8,
	TexFmt_ASTC_RGB_4x4,
	TexFmt_ASTC_RGB_5x5,
	TexFmt_ASTC_RGB_6x6,
	TexFmt_ASTC_RGB_8x8,
	TexFmt_ASTC_RGB_10x10,
	TexFmt_ASTC_RGB_12x12,
	TexFmt_ASTC_RGBA_4x4,
	TexFmt_ASTC_RGBA_5x5,
	TexFmt_ASTC_RGBA_6x6,
	TexFmt_ASTC_RGBA_8x8,
	TexFmt_ASTC_RGBA_10x10,
	TexFmt_ASTC_RGBA_12x12,
	TexFmt_ETC_RGB4_3DS,
	TexFmt_ETC_RGBA8_3DS,
	TexFmt_MAX
};

PVRTuint64 PixelTypeByTextureFormat[] =
{
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('a', '\0', '\0', '\0', 8, 0, 0, 0), //alpha8
	PVRTGENPIXELID4('a', 'r', 'g', 'b', 4, 4, 4, 4), //argb4444
	PVRTGENPIXELID4('r', 'g', 'b', '\0', 8, 8, 8, 0), //rgb24
	PVRTGENPIXELID4('r', 'g', 'b', 'a', 8, 8, 8, 8), //rgba32
	PVRTGENPIXELID4('a', 'r', 'g', 'b', 8, 8, 8, 8), //argb32
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('r', 'g', 'b', '\0', 5, 6, 5, 0), //rgb565, possibly wrong due to endianess
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('r', '\0', '\0', '\0', 16, 0, 0, 0), //r16
	PVRTLPF_DXT1, //dxt1
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTLPF_DXT5, //dxt5
	PVRTGENPIXELID4('b', 'a', 'r', 'g', 4, 4, 4, 4), //rgba4444, actually barg4444
	PVRTGENPIXELID4('b', 'g', 'r', 'a', 8, 8, 8, 8), //bgra32 (Unity 5)
	PVRTGENPIXELID4('r', '\0', '\0', '\0', 16, 0, 0, 0), //RHalf
	PVRTGENPIXELID4('r', 'g', '\0', '\0', 16, 16, 0, 0), //RGHalf
	PVRTGENPIXELID4('r', 'g', 'b', 'a', 16, 16, 16, 16), //RGBAHalf
	PVRTGENPIXELID4('r', '\0', '\0', '\0', 32, 0, 0, 0), //RFloat
	PVRTGENPIXELID4('r', 'g', '\0', '\0', 32, 32, 0, 0), //RGFloat
	PVRTGENPIXELID4('r', 'g', 'b', 'a', 32, 32, 32, 32), //RGBAFloat
	PVRTLPF_YUY2_422, //YUV2
	PVRTLPF_SharedExponentR9G9B9E5, //RGB9e5
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //BC6H
	PVRTLPF_BC7, //BC7
	PVRTLPF_BC4, //BC4
	PVRTLPF_BC5, //BC5
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //DXT1Crunched
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //DXT5Crunched
	PVRTLPF_PVRTCI_2bpp_RGB, //TexFmt_PVRTC_RGB2
	PVRTLPF_PVRTCI_2bpp_RGBA, //TexFmt_PVRTC_RGBA2
	PVRTLPF_PVRTCI_4bpp_RGB, //TexFmt_PVRTC_RGB4
	PVRTLPF_PVRTCI_4bpp_RGBA, //TexFmt_PVRTC_RGBA4
	PVRTLPF_ETC1, //TexFmt_ETC_RGB4
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ATC_RGB4 (obsolete)
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ATC_RGBA8 (obsolete)
	PVRTGENPIXELID4('b', 'g', 'r', 'a', 8, 8, 8, 8), //bgra32 (Unity 4)
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //none
	PVRTLPF_EAC_R11, //eac_r
	PVRTLPF_EAC_R11, //eac_r_signed
	PVRTLPF_EAC_RG11, //eac_rg
	PVRTLPF_EAC_RG11, //eac_rg_signed
	PVRTLPF_ETC2_RGB, //etc2_rgb4
	PVRTLPF_ETC2_RGB_A1, //etc2_rgba1
	PVRTLPF_ETC2_RGBA, //etc2_rgba8
	PVRTLPF_ASTC_4x4, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_4x4 (only rgba supported)
	PVRTLPF_ASTC_5x5, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_5x5 (only rgba supported)
	PVRTLPF_ASTC_6x6, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_6x6 (only rgba supported)
	PVRTLPF_ASTC_8x8, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_8x8 (only rgba supported)
	PVRTLPF_ASTC_10x10, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_10x10 (only rgba supported)
	PVRTLPF_ASTC_12x12, //pvrtexture::PixelType('\0', '\0', '\0', '\0', 0, 0, 0, 0).PixelTypeID, //astc_rgb_12x12 (only rgba supported)
	PVRTLPF_ASTC_4x4, //astc_rgba_4x4
	PVRTLPF_ASTC_5x5, //astc_rgba_5x5
	PVRTLPF_ASTC_6x6, //astc_rgba_6x6
	PVRTLPF_ASTC_8x8, //astc_rgba_8x8
	PVRTLPF_ASTC_10x10, //astc_rgba_10x10
	PVRTLPF_ASTC_12x12, //astc_rgba_12x12
	PVRTLPF_ETC2_RGB, //etc_rgb4_3ds
	PVRTLPF_ETC2_RGBA, //etc_rgba8_3ds
	PVRTGENPIXELID4('r', 'g', '\0', '\0', 8, 8, 0, 0), //RG16
	PVRTGENPIXELID4('r', '\0', '\0', '\0', 8, 0, 0, 0), //R8
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 8, 0, 0, 0), //ETC_RGB4Crunched
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 8, 0, 0, 0), //ETC2_RGBA8Crunched
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_4x4
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_5x5
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_6x6
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_8x8
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_10x10
	PVRTGENPIXELID4('\0', '\0', '\0', '\0', 0, 0, 0, 0), //ASTC_HDR_12x12
	PVRTGENPIXELID4('r', 'g', '\0', '\0', 16, 16, 0, 0), //RG32
	PVRTGENPIXELID4('r', 'g', 'b', '\0', 16, 16, 16, 0), //RGB48
	PVRTGENPIXELID4('r', 'g', 'b', 'a', 16, 16, 16, 16), //RGBA64
};
PVRTexLibVariableType VariableTypeByTextureFormat[] = {
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_SignedFloat,
	PVRTLVT_SignedFloat,
	PVRTLVT_SignedFloat,
	PVRTLVT_SignedFloat,
	PVRTLVT_SignedFloat,
	PVRTLVT_SignedFloat,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm, //ETC_RGB4
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,//BGRA32Old
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedIntegerNorm, //EAC_R
	PVRTLVT_SignedIntegerNorm, //EAC_R_SIGNED
	PVRTLVT_UnsignedIntegerNorm, //EAC_RG
	PVRTLVT_SignedIntegerNorm, //EAC_RG_SIGNED
	PVRTLVT_UnsignedByteNorm, //ETC2_RGB4
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm, //ASTC_RGB_4x4
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm, //ASTC_RGBA_4x4
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm, //ETC2_RGB4_3DS
	PVRTLVT_UnsignedByteNorm,
	PVRTLVT_UnsignedByteNorm, //RG16
	PVRTLVT_UnsignedByteNorm, //R8
	PVRTLVT_UnsignedByteNorm, //ETC_RGB4Crunched
	PVRTLVT_UnsignedByteNorm, //ETC2_RGBA8Crunched
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_4x4
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_5x5
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_6x6
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_8x8
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_10x10
	PVRTLVT_UnsignedByteNorm, //ASTC_HDR_12x12
	PVRTLVT_UnsignedByteNorm, //RG32
	PVRTLVT_UnsignedByteNorm, //RGB48
	PVRTLVT_UnsignedByteNorm, //RGBA64
};