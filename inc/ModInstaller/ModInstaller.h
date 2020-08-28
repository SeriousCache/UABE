#pragma once
#include "InstallerDataFormat.h"

#ifndef MODINSTALLER_API
#ifdef MODINSTALLER_EXPORTS
#define MODINSTALLER_API __declspec(dllexport)
#else
#define MODINSTALLER_API __declspec(dllimport)
#endif
#endif

#define MI_CANCEL -1
#define MI_INCOMPLETE -2
#define MI_MOVEFILEFAIL -3
typedef void(_cdecl *LogCallback)(const char *message);

MODINSTALLER_API void SanitizePackageFile(InstallerPackageFile &packageFile);
MODINSTALLER_API int Install(InstallerPackageFile &packageFile, LogCallback log = NULL);