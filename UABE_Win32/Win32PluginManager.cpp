#include "stdafx.h"
#include "Win32PluginManager.h"
#include <filesystem>
#include "../UABE_Generic/AppContext.h"
#include "../UABE_Generic/FileContextInfo.h"
#include <algorithm>

IAssetListTabOptionProvider::IAssetListTabOptionProvider() {}
IAssetViewEntryOptionProvider::IAssetViewEntryOptionProvider() {}

void loadPlugin(Win32AppContext& appContext, PluginMapping& outMapping, const std::filesystem::path& path)
{
	HMODULE hModule = LoadLibrary(path.c_str());
	if (hModule == NULL)
		return;
	auto pGetDescCallback = reinterpret_cast<UABEGetPluginDescCallback1>(GetProcAddress(hModule, "GetUABEPluginDesc1"));
	IPluginDesc* pPluginDesc = nullptr;
	if (pGetDescCallback == nullptr || (pPluginDesc = pGetDescCallback(sizeof(AppContext), sizeof(BundleFileContextInfo))) == nullptr)
	{
		FreeLibrary(hModule);
		return;
	}
	outMapping.descriptions.emplace_back(pPluginDesc);
	for (std::shared_ptr<IOptionProvider> &pProvider : pPluginDesc->getPluginOptions(appContext))
		outMapping.options.emplace_back(pProvider);
	//TODO: Keep the HMODULE somewhere, so it can be unloaded during runtime.
}

void loadAllPlugins(Win32AppContext& appContext, PluginMapping& outMapping, const std::string& pluginsDir)
{
	//Create a path object, treating pluginsDir as UTF-8 (C++20; older equivalent: std::filesystem::u8path(pluginsDir)).
	std::filesystem::path pluginsDirPath(
		reinterpret_cast<const char8_t*>(&pluginsDir.data()[0]),
		reinterpret_cast<const char8_t*>(&pluginsDir.data()[pluginsDir.size()]));
	//Ignore errors accessing the directory.
	std::error_code errc;
	for (const auto& dirEntry : std::filesystem::directory_iterator(pluginsDirPath.make_preferred(), errc))
	{
		auto extension = dirEntry.path().extension().string();
		const std::string expectedExtension(".bep");
		if (dirEntry.is_regular_file()
			&& std::equal(extension.begin(), extension.end(),
				expectedExtension.begin(), expectedExtension.end(),
				[](unsigned char is, unsigned char exp) { return std::tolower(is) == exp; }))
		{
			loadPlugin(appContext, outMapping, dirEntry.path());
		}
	}
}

size_t ShowContextMenu(size_t numEntries, std::function<const char* (size_t)> entryNameGetter,
	UINT popupMenuFlags, LONG x, LONG y, HWND hParent,
	HMENU& hCurPopupMenu, size_t nMaxEntriesOnScreen)
{
	auto displayPopupMenu = [numEntries, &entryNameGetter, popupMenuFlags, x, y, hParent, &hCurPopupMenu](size_t rangeMin, size_t rangeMax) {
		assert(rangeMax <= numEntries);
		if (rangeMax <= rangeMin || rangeMax > numEntries)
			return (uintptr_t)0;
		if (hCurPopupMenu != NULL)
			DestroyMenu(hCurPopupMenu);
		hCurPopupMenu = CreatePopupMenu();
		if (hCurPopupMenu == NULL)
			return (uintptr_t)0;
		if (rangeMin > 0)
			AppendMenu(hCurPopupMenu, MF_STRING, 100, TEXT("..."));
		for (size_t i = 0; i < rangeMax - rangeMin; ++i)
		{
			auto pOptionNameT = unique_MultiByteToTCHAR(entryNameGetter(i + rangeMin));
			AppendMenu(hCurPopupMenu, MF_STRING, 9000 + i, pOptionNameT.get());
		}
		if (rangeMax < numEntries)
			AppendMenu(hCurPopupMenu, MF_STRING, 101, TEXT("..."));
		return static_cast<uintptr_t>(TrackPopupMenuEx(hCurPopupMenu, popupMenuFlags, x, y, hParent, NULL));
	};
	size_t rangeMin = 0;
	size_t rangeMax = std::min<size_t>(numEntries, nMaxEntriesOnScreen);
	size_t ret = (size_t)-1;
	while (uintptr_t selectedId = displayPopupMenu(rangeMin, rangeMax))
	{
		if (selectedId == 100) //Up
		{
			rangeMin = std::max<size_t>(rangeMin, nMaxEntriesOnScreen) - nMaxEntriesOnScreen;
			rangeMax = std::min<size_t>(numEntries, rangeMin + nMaxEntriesOnScreen);
		}
		else if (selectedId == 101) //Down
		{
			rangeMax = std::min<size_t>(numEntries, rangeMax + nMaxEntriesOnScreen);
			rangeMin = std::max<size_t>(rangeMax, nMaxEntriesOnScreen) - nMaxEntriesOnScreen;
		}
		else if (selectedId >= 9000 && selectedId < (9000 + (rangeMax - rangeMin)))
		{
			size_t iOption = static_cast<size_t>(selectedId - 9000) + rangeMin;
			assert(iOption < numEntries);
			if (iOption < numEntries)
				ret = iOption;
			break;
		}
	}
	if (hCurPopupMenu != NULL)
	{
		DestroyMenu(hCurPopupMenu);
		hCurPopupMenu = NULL;
	}
	return ret;
}
