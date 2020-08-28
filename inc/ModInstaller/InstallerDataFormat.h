#pragma once
typedef unsigned __int64 QWORD;
#ifndef MODINSTALLER_API
#ifdef MODINSTALLER_EXPORTS
#define MODINSTALLER_API __declspec(dllexport)
#else
#define MODINSTALLER_API __declspec(dllimport)
#endif
#endif
#include <stdint.h>
#include <vector>
#include "..\AssetsTools\AssetsFileFormat.h"
#include "..\AssetsTools\AssetsFileReader.h"
#include "..\AssetsTools\AssetBundleFileFormat.h"
#include "..\AssetsTools\BundleReplacer.h"
#include "..\AssetsTools\AssetsReplacer.h"
#include "..\AssetsTools\ClassDatabaseFile.h"

class InstallerPackageFile
{
	bool ownStrings;
public:
	MODINSTALLER_API InstallerPackageFile();
	MODINSTALLER_API ~InstallerPackageFile();
	char magic[4]; //EMIP = Extractor Mod Installer Package
	char *modName;
	char *modCreators; //comma-separated names list
	char *modDescription;
	ClassDatabaseFile addedTypes;
	std::vector<class InstallerPackageAssetsDesc> affectedAssets;
	
	//prefReplacersInMemory indicates whether it should try to read all replacer data into memory to remove the reader dependency after Read
	MODINSTALLER_API bool Read(QWORD& pos, IAssetsReader *pReader, bool prefReplacersInMemory = false);
	MODINSTALLER_API bool Write(QWORD& pos, IAssetsWriter *pWriter);
};

class InstallerPackageAssetsDesc
{
	bool ownStrings;
public:
	MODINSTALLER_API InstallerPackageAssetsDesc();
	MODINSTALLER_API InstallerPackageAssetsDesc(const InstallerPackageAssetsDesc &src);
	MODINSTALLER_API ~InstallerPackageAssetsDesc();
	bool isBundle;
	char *path; //relative to the main path
	std::vector<void*> replacers; //BundleReplacer if isBundle, else AssetsReplacer
};