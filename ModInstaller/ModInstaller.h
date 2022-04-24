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
#ifdef MODINSTALLER_EXPORTS
#include "InstallDialog.h"
int Install(InstallerPackageFile &packageFile, LogCallback log = NULL, 
	DialogController_Progress *pProgressController = NULL, DialogController_Complete *pCompleteController = NULL);
#endif
MODINSTALLER_API int Install(InstallerPackageFile &packageFile, LogCallback log = NULL);

MODINSTALLER_API size_t GetPEOverlayOffset(IAssetsReader *pReader);
