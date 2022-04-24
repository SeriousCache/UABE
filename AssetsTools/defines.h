#pragma once
#include <string>
#include <cstdint>

typedef uint64_t QWORD;

#ifdef ASSETSTOOLS_EXPORTS
#if (ASSETSTOOLS_EXPORTS == 1)
#define ASSETSTOOLS_API __declspec(dllexport) 
#else
#define ASSETSTOOLS_API
#endif
#elif defined(ASSETSTOOLS_IMPORTSTATIC)
#define ASSETSTOOLS_API
#else
#define ASSETSTOOLS_API __declspec(dllimport)
#endif

#ifndef __AssetsTools_AssetsFileFunctions_Read
#define __AssetsTools_AssetsFileFunctions_Read
typedef void(_cdecl *AssetsFileVerifyLogger)(const char *message);
#endif

#ifndef __AssetsTools_AssetsReplacerFunctions_FreeCallback
#define __AssetsTools_AssetsReplacerFunctions_FreeCallback
typedef void(_cdecl *cbFreeMemoryResource)(void *pResource);
typedef void(_cdecl *cbFreeReaderResource)(class IAssetsReader *pReader);
#endif
#ifndef __AssetsTools_Hash128
#define __AssetsTools_Hash128
union Hash128
{
	uint8_t bValue[16];
	uint16_t wValue[8];
	uint32_t dValue[4];
	QWORD qValue[2];
};
#endif

#include "AssetsFileReader.h"