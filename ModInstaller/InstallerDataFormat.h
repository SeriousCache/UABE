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
#include <list>
#include <string>
#include <memory>
#include "../AssetsTools/AssetsFileFormat.h"
#include "../AssetsTools/AssetsFileReader.h"
#include "../AssetsTools/AssetBundleFileFormat.h"
#include "../AssetsTools/BundleReplacer.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/ClassDatabaseFile.h"

//Installer packages contain all instructions (add, modify, remove) 
class InstallerPackageFile
{
	bool Read(QWORD& pos, IAssetsReader *pReader, std::shared_ptr<IAssetsReader> ref_pReader, bool prefReplacersInMemory);
public:
	MODINSTALLER_API InstallerPackageFile();
	MODINSTALLER_API ~InstallerPackageFile();
	char magic[4] = {}; //EMIP = Extractor Mod Installer Package
	std::string modName;
	std::string modCreators; //comma-separated names list
	std::string modDescription;
	ClassDatabaseFile addedTypes;
	std::vector<class InstallerPackageAssetsDesc> affectedAssets;
	//DWORD opCount; //the amount of operations (on any of the .assets files)
	
	//prefReplacersInMemory indicates whether it should try to read all replacer data into memory to remove the reader dependency after Read
	MODINSTALLER_API bool Read(QWORD& pos, IAssetsReader *pReader, bool prefReplacersInMemory = false);
	MODINSTALLER_API bool Read(QWORD& pos, std::shared_ptr<IAssetsReader> pReader, bool prefReplacersInMemory = false);
	MODINSTALLER_API bool Write(QWORD& pos, IAssetsWriter *pWriter);
};

enum class InstallerPackageAssetsType
{
	Assets=0,
	Bundle=1,
	Resources=2
};
class InstallerPackageAssetsDesc
{
public:
	MODINSTALLER_API InstallerPackageAssetsDesc();
	MODINSTALLER_API InstallerPackageAssetsDesc(const InstallerPackageAssetsDesc &src);
	MODINSTALLER_API ~InstallerPackageAssetsDesc();
	InstallerPackageAssetsType type;
	//relative to the main path
	std::string path;
	//Assets: 0..N AssetsReplacer entries
	//Bundle: 0..N BundleReplacer entries
	//Resources: 1 BundleEntryModifierByResources entry
	std::vector<std::shared_ptr<GenericReplacer>> replacers; 
};
