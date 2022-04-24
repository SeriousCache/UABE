#pragma once
#include "../UABE_Generic/IAssetBatchImportDesc.h"
#include "../UABE_Generic/AssetPluginUtil.h"
#include "api.h"
#include <stdint.h>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

//Class implemented by plugins that use the Win32AppContext::ShowAssetBatchImportDialog function specifically. 
//-> The plugin would pass a companion IAssetBatchImportDesc object (-> UABE_Generic) to the function.
//All const char* and std strings are UTF-8. const char* strings returned by the plugin must not be freed before the dialog has closed.
class IWin32AssetBatchImportDesc
{
public:
	//Shows a settings dialog to specify precise settings of a single asset. Returns whether such a dialog is supported if matchIndex==(size_t)-1.
	//matchIndex is defined as in the companion IAssetBatchImportDesc object.
	UABE_Win32_API virtual bool ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow) = 0;
};

class CWin32GenericBatchImportDialogDesc : public CGenericBatchImportDialogDesc, public IWin32AssetBatchImportDesc
{
	std::string fileTypeFilter;
public:
	//fileTypeFilter: File type filter as for the FileDialog.h functions, e.g. "*.*|Raw Unity asset:".
	inline CWin32GenericBatchImportDialogDesc(std::vector<AssetUtilDesc> _elements, const std::string& extensionRegex,
		std::string _fileTypeFilter)
		: CGenericBatchImportDialogDesc(std::move(_elements), extensionRegex),
		  fileTypeFilter(std::move(_fileTypeFilter))
	{}

	UABE_Win32_API bool ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow);
};
