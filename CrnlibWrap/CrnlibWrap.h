#pragma once
#include "../AssetsTools/TextureFileFormat.h"
#include <vector>
#ifndef CRNLIBWRAP_API
#define CRNLIBWRAP_API __declspec(dllimport)
#endif

CRNLIBWRAP_API bool CrunchTextureData_RGBA32_Unity(TextureFile* pTex, const void* pRGBA32Buf,
	void* pOutBuf, QWORD& outputSize, int compressQuality, unsigned int curWidth, unsigned int curHeight);
CRNLIBWRAP_API bool CrunchTextureData_RGBA32_Legacy(TextureFile* pTex, const void* pRGBA32Buf,
	void* pOutBuf, QWORD& outputSize, int compressQuality, unsigned int curWidth, unsigned int curHeight);

CRNLIBWRAP_API bool DecrunchTextureData_Unity(TextureFile* pTex, std::vector<uint8_t>& decrunchBuf, TextureFormat& decrunchFormat);
CRNLIBWRAP_API bool DecrunchTextureData_Legacy(TextureFile* pTex, std::vector<uint8_t>& decrunchBuf, TextureFormat& decrunchFormat);