#pragma once
//#include "AssetBundleExtractor.h"
//#include "AssetListManager.h"
#include "Win32AppContext.h"
#include "FileContextInfo.h"
#include "../AssetsTools/ClassDatabaseFile.h"
#include "../AssetsTools/EngineVersion.h"
#include <vector>
#include <memory>
#include <Windows.h> //HWND

bool GetAllScriptInformation(Win32AppContext &appContext, std::vector<std::shared_ptr<AssetsFileContextInfo>> &assetsInfo);
std::shared_ptr<ClassDatabaseFile> CreateMonoBehaviourClassDb(Win32AppContext &appContext,
	std::vector<std::pair<EngineVersion,WCHAR*>> &assemblyFullNames,
	bool useLongPathID, bool allowUserDialog);
