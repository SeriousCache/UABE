#pragma once
#include "api.h"
#include "../UABE_Generic/PluginManager.h"
#include "Win32AppContext.h"
#include "AssetListDialog.h"
#include <functional>

//Provider class intended for plugins that create a AssetModifyDialog tab in an AssetListDialog
class IAssetListTabOptionProvider : public IOptionProvider
{
public:
	UABE_Win32_API IAssetListTabOptionProvider();
	UABE_Win32_API virtual EAssetOptionType getType() = 0;
	//Determines whether this option provider applies to a given selection.
	//Returns a runner object for the selection, or a null pointer otherwise.
	//No operation should be applied unless the runner object's operator() is invoked.
	//The plugin should set "optionName" to a short description on success.
	UABE_Win32_API virtual std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetListDialog& listDialog,
		std::vector<struct AssetUtilDesc> selection,
		std::string& optionName) = 0;
};

//Function provided by plugins. Called by the plugin loader to determine the supported actions.
//The sizeof_* parameters are set by the loader to sizeof(AppContext) and sizeof(BundleFileContextInfo), respectively.
// The plugin should check the sizes and return nullptr on a mismatch.
//The returned pointer must be created via the new operator. If nullptr is returned, the plugin will be unloaded again.
//The function should be exported with a .def file as GetUABEPluginDesc1.
typedef IPluginDesc* (_cdecl* UABEGetPluginDescCallback1)(size_t sizeof_AppContext, size_t sizeof_BundleFileContextInfo);


void loadAllPlugins(class Win32AppContext& appContext, PluginMapping& outMapping, const std::string& pluginsDir);

#include "AssetViewModifyDialog.h"
//Provider class for context menu options in the "View Asset" dialog.
class IAssetViewEntryOptionProvider : public IOptionProvider
{
public:
	UABE_Win32_API IAssetViewEntryOptionProvider();
	UABE_Win32_API virtual EAssetOptionType getType() = 0;
	//Determines whether this option provider applies to a given selection.
	//Returns a runner object for the selection, or a null pointer otherwise.
	//No operation should be applied unless the runner object's operator() is invoked.
	//The plugin should set "optionName" to a short description on success.
	//The runner object must not use the fieldInfo elements concurrently,
	// and the validity of fieldInfo elements is only guaranteed up until the end of the runner call.
	UABE_Win32_API virtual std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetViewModifyDialog& assetViewDialog,
		AssetViewModifyDialog::FieldInfo fieldInfo,
		std::string& optionName) = 0;
};

//Shows a context menu with an arbitrary amount of entries (of at least one).
//hCurPopupMenu: Reference to a variable that holds the current popup menu.
// -> If hCurPopupMenu is set prior to calling ShowContextMenu, it will be destroyed first.
// -> Could be used by the caller in window handlers while the popup menu window loop is running.
// -> Closes the menu before returning.
//Returns the selected index, or (size_t)-1 if no proper selection was made.
size_t ShowContextMenu(size_t numEntries, std::function<const char* (size_t)> entryNameGetter,
	UINT popupMenuFlags, LONG x, LONG y, HWND hParent,
	HMENU& hCurPopupMenu, size_t nMaxEntriesOnScreen = 30);
