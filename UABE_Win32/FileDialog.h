#pragma once
#include "api.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shobjidl.h>
#include <ShTypes.h>
#include <vector>
#include <string>

static const GUID UABE_FILEDIALOG_DEFAULT_GUID = {0x832dbb4b, 0xf1bf, 0x8e37, 0x69, 0xc5, 0x41, 0x6e, 0x5f, 0x72, 0x67, 0xc0 };

UABE_Win32_API HRESULT ShowFileOpenDialog(HWND hOwner, wchar_t **filePathBuf, const wchar_t *fileTypeFilter,
	UINT *pOutSelFilter = nullptr, LPCTSTR defaultFile = nullptr, LPCTSTR windowTitle = nullptr,
	const GUID &guid = UABE_FILEDIALOG_DEFAULT_GUID);

//free with delete[] filePath
UABE_Win32_API HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner, WCHAR **filePathsBuf,
	const wchar_t *fileTypeFilter, UINT *pOutSelFilter = nullptr,
	LPCTSTR defaultFile = nullptr, LPCTSTR windowTitle = nullptr,
	const GUID &guid = UABE_FILEDIALOG_DEFAULT_GUID);
//free with FreeFilePathsMultiSelect
UABE_Win32_API HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner, std::vector<char*> &filePathsBuf,
	const char *fileTypeFilter, UINT *pOutSelFilter = nullptr,
	const char *defaultFile = nullptr, const char *windowTitle = nullptr,
	const GUID& guid = UABE_FILEDIALOG_DEFAULT_GUID);
UABE_Win32_API void FreeFilePathsMultiSelect(std::vector<char*> &filePathsBuf);

//free with FreeCOMFilePathBuf for each filePath, then delete[] filePaths
UABE_Win32_API HRESULT ShowFileSaveDialog(HWND hOwner, wchar_t **filePathBuf, const wchar_t *fileTypeFilter,
	UINT *pOutSelFilter = nullptr, LPCTSTR defaultFile = nullptr, LPCTSTR windowTitle = nullptr,
	const GUID &guid = UABE_FILEDIALOG_DEFAULT_GUID);
UABE_Win32_API HRESULT ShowFileDialog(HWND hOwner, wchar_t **filePathBuf,
	CLSID dialogClass,
	const COMDLG_FILTERSPEC *pFilters, int filtersCount,
	UINT *pOutSelFilter = nullptr, LPCTSTR defaultFile = nullptr, LPCTSTR windowTitle = nullptr,
	const GUID &guid = UABE_FILEDIALOG_DEFAULT_GUID);
UABE_Win32_API BOOL ShowFolderSelectDialog(HWND hOwner, WCHAR **folderPathBuf, LPCWSTR windowTitle = nullptr,
	const GUID &guid = UABE_FILEDIALOG_DEFAULT_GUID);
UABE_Win32_API void FreeCOMFilePathBuf(WCHAR **filePath);
