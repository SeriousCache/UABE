#include "stdafx.h"
#include "FileDialog.h"
#include "../libStringConverter/convert.h"
#include <CommDlg.h>
#include <ShlObj.h>
#pragma comment(lib, "Shell32.lib")

//for Windows XP
bool testedFileDialog = false;
bool useLegacyFileDialog = false;

int GetFileTypeFilterCount(const wchar_t *filter)
{
	int ret = 1;
	size_t filterStringLen = wcslen(filter);
	for (size_t i = 1; i < filterStringLen; i++)
	{
		if (filter[i] == L':')
		{
			if (filter[i-1] == L'\\') //allow usage of ':' in text if a backslash prepends it
				continue;
			if ((i+1) < filterStringLen)
				ret++;
		}
	}
	return ret;
}
//also modifies the filter string for backslashes
void GetNextFileFilterIndices(
	wchar_t *filter,
	size_t *pFilterStringLen, //don't always run wcslen(filter)
	size_t curSpecStart, 
	bool &hasName, bool &hasNextSpec,
	size_t &curNameStart, size_t &nextSpecStart)
{
	size_t filterStringLen = *pFilterStringLen;
	hasName = false; hasNextSpec = false;
	nextSpecStart = filterStringLen;
	for (size_t _ch = curSpecStart; _ch < filterStringLen; _ch++)
	{
		if (!hasName && filter[_ch] == L'|')
		{
			curNameStart = _ch+1;
			hasName = true;
		}
		if (filter[_ch] == L':')
		{
			if (filter[_ch-1] != L'\\')
			{
				nextSpecStart = _ch;
				hasNextSpec = true;
				break;
			}
			else
			{
				for (size_t _tmpch = _ch; _tmpch <= filterStringLen; _tmpch++)
				{
					filter[_tmpch-1] = filter[_tmpch];
				}
				(*pFilterStringLen)--; //
				filterStringLen--; //
				nextSpecStart--; //
			}
		}
	}
}

#pragma region Fallback
struct LegacyFileFilter
{
	wchar_t *name;
	wchar_t *spec;
};
typedef BOOL(WINAPI *prot_GetOpenFileNameW)(LPOPENFILENAMEW lpofn);
prot_GetOpenFileNameW pGetOpenFileNameW = NULL;
typedef BOOL(WINAPI *prot_GetSaveFileNameW)(LPOPENFILENAMEW lpofn);
prot_GetSaveFileNameW pGetSaveFileNameW = NULL;

typedef PIDLIST_ABSOLUTE(__stdcall *prot_SHBrowseForFolderW)(LPBROWSEINFOW lpbi);
prot_SHBrowseForFolderW pSHBrowseForFolderW = NULL;
typedef BOOL(__stdcall *prot_SHGetPathFromIDListW)(PCIDLIST_ABSOLUTE pidl, LPWSTR pszPath);
prot_SHGetPathFromIDListW pSHGetPathFromIDListW = NULL;
void InitFileDialogFallback()
{
	HMODULE hComdlg = GetModuleHandle(TEXT("Comdlg32.dll"));
	HMODULE hShell32 = GetModuleHandle(TEXT("Shell32.dll"));
	if (hComdlg == nullptr || hShell32 == nullptr)
		return;
	pGetOpenFileNameW = (prot_GetOpenFileNameW)GetProcAddress(hComdlg, "GetOpenFileNameW");
	pGetSaveFileNameW = (prot_GetSaveFileNameW)GetProcAddress(hComdlg, "GetSaveFileNameW");
	pSHBrowseForFolderW = (prot_SHBrowseForFolderW)GetProcAddress(hShell32, "SHBrowseForFolderW");
	pSHGetPathFromIDListW = (prot_SHGetPathFromIDListW)GetProcAddress(hShell32, "SHGetPathFromIDListW");
	useLegacyFileDialog = true;
}

bool MakeFileTypeFilterFallback(const wchar_t *_filter, wchar_t **_buf, int *_filterCount)
{
	size_t filterStringLen = wcslen(_filter);

	wchar_t *filter = (wchar_t*)malloc(sizeof(wchar_t) * (filterStringLen+1));
	if (filter == NULL)
		return false;
	wcscpy_s(filter, filterStringLen+1, _filter);

	if (filterStringLen == 0 || filter[0] == L':')
	{
		*_buf = NULL;
		*_filterCount = 0;
		return true;
	}
	int filterCount = GetFileTypeFilterCount(filter);

	wchar_t *stringMap = (wchar_t*)malloc(sizeof(wchar_t) * (filterStringLen + 2 + filterCount));
	if (stringMap == NULL)
	{
		free(filter);
		return false;
	}

	size_t curStrMapIndex = 0;

	size_t curSpecStart = 0;
	size_t curNameStart = 0;
	for (int i = 0; i < filterCount; i++)
	{
		bool hasName = false; bool hasNextSpec = false;
		size_t nextSpecStart = filterStringLen;

		GetNextFileFilterIndices(filter, &filterStringLen, curSpecStart, hasName, hasNextSpec, curNameStart, nextSpecStart);
		
		if (!hasName) curNameStart = curSpecStart;

		size_t specSize = (hasName ? (curNameStart-1) : nextSpecStart) - curSpecStart;
		size_t nameSize = nextSpecStart - curNameStart;
		
		if (hasName)
		{
			wcsncpy_s(&stringMap[curStrMapIndex], nameSize+1, &filter[curNameStart], nameSize);
			stringMap[curStrMapIndex+nameSize] = 0;
			curStrMapIndex += (nameSize + 1);
		}
		else
		{
			stringMap[curStrMapIndex] = L' ';
			stringMap[curStrMapIndex+1] = 0;
			curStrMapIndex += 2;
		}

		wcsncpy_s(&stringMap[curStrMapIndex], specSize+1, &filter[curSpecStart], specSize);
		stringMap[curStrMapIndex+specSize] = 0;
		curStrMapIndex += (specSize + 1);

		if ((i+1) < filterCount && !hasNextSpec)
			filterCount = i+1;
		if (!hasNextSpec)
			break;
		curSpecStart = nextSpecStart + 1;
	}
	stringMap[curStrMapIndex] = stringMap[curStrMapIndex+1] = 0;
	
	free(filter);
	*_buf = stringMap;
	*_filterCount = filterCount;
	return true;
}
void FreeFileTypeFilterFallback(wchar_t **buf)
{
	if (buf != NULL && *buf != NULL)
	{
		free(*buf);
		*buf = NULL;
	}
}

HRESULT MakeFileDialogStructureFallback(OPENFILENAMEW *out, 
	HWND hOwner, wchar_t **filePathBuf, wchar_t *pFilters, int filterCount, LPCWSTR defaultFile,
	size_t pathBufferLen = MAX_PATH+1)
{
	size_t defaultFileNameLen = (defaultFile != NULL) ? (wcslen(defaultFile)+1) : 1;
	if (pathBufferLen < defaultFileNameLen)
		pathBufferLen = defaultFileNameLen;
	*filePathBuf = (wchar_t*)malloc(sizeof(wchar_t) * pathBufferLen);
	if (*filePathBuf == NULL)
		return E_OUTOFMEMORY;

	if (defaultFile)
		memcpy(*filePathBuf, defaultFile, defaultFileNameLen * sizeof(wchar_t));
	else
		memset(*filePathBuf, 0, sizeof(wchar_t) * pathBufferLen);//(*filePathBuf)[0] = 0;

	ZeroMemory(out, sizeof(OPENFILENAMEW));
	out->lStructSize = sizeof(OPENFILENAMEW);
	out->hwndOwner = hOwner;
	out->lpstrFilter = pFilters;
	out->nFilterIndex = 1;
	out->lpstrFile = *filePathBuf;
	out->nMaxFile = (DWORD)pathBufferLen;
	out->Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
	//out->lpstrInitialDir = &fallback_nullChar;

	return NOERROR;
}
#define FNERR_BUFFERTOOSMALL 0x3003
HRESULT ShowFileOpenDialogFallback(HWND hOwner, wchar_t **filePathBuf, const wchar_t *fileTypeFilter, UINT *pOutSelFilter, LPCTSTR defaultFile, LPCTSTR windowTitle)
{
	if (pGetOpenFileNameW == NULL)
	{
		MessageBox(hOwner, TEXT("Unable to open a file dialog (fallback method failed)!"), TEXT("Error"), MB_ICONERROR);
		return E_NOTIMPL;
	}
	if (pOutSelFilter)
		*pOutSelFilter = (UINT)-1;
	wchar_t *pFilters; int filterCount;
	if (!MakeFileTypeFilterFallback(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	OPENFILENAMEW fileNameStruct;

	HRESULT ret = NOERROR;
	size_t pathBufferLen = MAX_PATH+1;
	while ((ret = MakeFileDialogStructureFallback(&fileNameStruct, hOwner, filePathBuf, 
		pFilters, filterCount, defaultFile, pathBufferLen)) != E_OUTOFMEMORY)
	{
		fileNameStruct.Flags |= OFN_FILEMUSTEXIST;
		if (windowTitle != NULL)
			fileNameStruct.lpstrTitle = windowTitle;
		if (!pGetOpenFileNameW(&fileNameStruct))
		{
			free(*filePathBuf);
			*filePathBuf = NULL;
			DWORD error = CommDlgExtendedError();
			if (error == FNERR_BUFFERTOOSMALL)
			{
				pathBufferLen = *(unsigned short*)fileNameStruct.lpstrFile;
			}
			else
			{
				ret = E_FAIL;
				break;
			}
		}
		else
		{
			if (pOutSelFilter)
				*pOutSelFilter = fileNameStruct.nFilterIndex;
			break;
		}
	}
	FreeFileTypeFilterFallback(&pFilters);
	return ret;
}

HRESULT ShowFileSaveDialogFallback(HWND hOwner, wchar_t **filePathBuf, const wchar_t *fileTypeFilter, UINT *pOutSelFilter, LPCWSTR defaultFile, LPCTSTR windowTitle)
{
	if (pGetSaveFileNameW == NULL)
	{
		MessageBox(hOwner, TEXT("Unable to open a file dialog (fallback method failed)!"), TEXT("Error"), MB_ICONERROR);
		return E_NOTIMPL;
	}
	if (pOutSelFilter)
		*pOutSelFilter = (UINT)-1;
	wchar_t *pFilters; int filterCount;
	if (!MakeFileTypeFilterFallback(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	OPENFILENAMEW fileNameStruct;

	HRESULT ret = NOERROR;
	size_t pathBufferLen = MAX_PATH+1;
	while ((ret = MakeFileDialogStructureFallback(&fileNameStruct, hOwner, filePathBuf,
		pFilters, filterCount, defaultFile, pathBufferLen)) != E_OUTOFMEMORY)
	{
		fileNameStruct.Flags |= OFN_CREATEPROMPT;
		if (windowTitle != NULL)
			fileNameStruct.lpstrTitle = windowTitle;
		if (!pGetSaveFileNameW(&fileNameStruct))
		{
			free(*filePathBuf);
			*filePathBuf = NULL;
			DWORD error = CommDlgExtendedError();
			if (error == FNERR_BUFFERTOOSMALL)
			{
				pathBufferLen = *(unsigned short*)fileNameStruct.lpstrFile;
			}
			else
			{
				ret = E_FAIL;
				break;
			}
		}
		else
		{
			if (pOutSelFilter)
				*pOutSelFilter = fileNameStruct.nFilterIndex;
			break;
		}
	}
	FreeFileTypeFilterFallback(&pFilters);
	return ret;
}
HRESULT ShowFileOpenDialogMultiSelectFallback(HWND hOwner, WCHAR **filePathsBuf, const wchar_t *fileTypeFilter, UINT *pOutSelFilter, LPCTSTR defaultFile, LPCTSTR windowTitle)
{
	if (pGetOpenFileNameW == NULL)
	{
		MessageBox(hOwner, TEXT("Unable to open a file dialog (fallback method failed)!"), TEXT("Error"), MB_ICONERROR);
		return E_NOTIMPL;
	}
	if (pOutSelFilter)
		*pOutSelFilter = (UINT)-1;
	wchar_t *pFilters; int filterCount;
	if (!MakeFileTypeFilterFallback(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	OPENFILENAMEW fileNameStruct;

	HRESULT ret = NOERROR;
	size_t pathBufferLen = (MAX_PATH+1)*16+1;
	while ((ret = MakeFileDialogStructureFallback(&fileNameStruct, hOwner, filePathsBuf, pFilters, 
		filterCount, defaultFile, pathBufferLen)) != E_OUTOFMEMORY)
	{
		fileNameStruct.Flags |= OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
		if (windowTitle != NULL)
			fileNameStruct.lpstrTitle = windowTitle;
		if (!pGetOpenFileNameW(&fileNameStruct))
		{
			free(*filePathsBuf);
			*filePathsBuf = NULL;
			DWORD error = CommDlgExtendedError();
			if (error == FNERR_BUFFERTOOSMALL)
			{
				if (pathBufferLen > *(unsigned short*)fileNameStruct.lpstrFile)
					pathBufferLen += (MAX_PATH+1)*16;
				else
					pathBufferLen = *(unsigned short*)fileNameStruct.lpstrFile;
			}
			else
			{
				ret = E_FAIL;
				break;
			}
		}
		else
		{
			if (pOutSelFilter)
				*pOutSelFilter = fileNameStruct.nFilterIndex;
			break;
		}
	}
	if (ret == NOERROR && *filePathsBuf)
	{
		size_t requiredLen = 0;
		size_t directoryLen = wcslen(*filePathsBuf);
		if (directoryLen)
		{
			size_t curStrIndex = 0; size_t curStrLen;
			while ((curStrLen = wcslen(&(*filePathsBuf)[directoryLen+1+curStrIndex])))
			{
				curStrIndex += curStrLen + 1;
				requiredLen += directoryLen + 1 + curStrLen + 1;
			}
			if (curStrIndex == 0)
			{
				FreeFileTypeFilterFallback(&pFilters);
				return NOERROR;
			}
		}
		WCHAR *wFilePathsList = (WCHAR*)malloc((requiredLen+1) * sizeof(WCHAR)); size_t curOutStrIndex = 0;
		if (!wFilePathsList)
		{
			free(*filePathsBuf);
			FreeFileTypeFilterFallback(&pFilters);
			return E_OUTOFMEMORY;
		}
		size_t curStrIndex = 0; size_t curStrLen;
		while ((curStrLen = wcslen(&(*filePathsBuf)[directoryLen+1+curStrIndex])))
		{
			memcpy(&wFilePathsList[curOutStrIndex], *filePathsBuf, directoryLen * sizeof(WCHAR));
			wFilePathsList[curOutStrIndex + directoryLen] = L'\\';
			memcpy(&wFilePathsList[curOutStrIndex + directoryLen + 1], 
				&(*filePathsBuf)[directoryLen+1+curStrIndex], curStrLen * sizeof(WCHAR));
			wFilePathsList[curOutStrIndex + directoryLen + 1 + curStrLen] = L';';
			curStrIndex += curStrLen + 1;
			curOutStrIndex += directoryLen + 1 + curStrLen + 1;
		}
		if (!curOutStrIndex)
			wFilePathsList[0] = 0;
		else
			wFilePathsList[curOutStrIndex - 1] = 0;
		free(*filePathsBuf);
		*filePathsBuf = wFilePathsList;
	}
	FreeFileTypeFilterFallback(&pFilters);
	return ret;
}

BOOL ShowFolderSelectDialogFallback(HWND hOwner, WCHAR **folderPathBuf, LPCWSTR windowTitle)
{
	if (pSHBrowseForFolderW == NULL
		|| pSHGetPathFromIDListW == NULL)
	{
		MessageBox(hOwner, TEXT("Unable to open a file dialog (fallback method failed)!"), TEXT("Error"), MB_ICONERROR);
		return FALSE;
	}
	LPITEMIDLIST pItemIDList = NULL;
	BROWSEINFOW bi; memset(&bi, 0, sizeof(BROWSEINFOW));
	BOOL ret = FALSE;

	WCHAR *folderPath = (WCHAR*)malloc(MAX_PATH * sizeof(WCHAR));
	if (folderPath == NULL)
		return FALSE;
	*folderPathBuf = folderPath;
	folderPath[0] = 0;

	bi.hwndOwner = hOwner;
	bi.pszDisplayName = folderPath;
	bi.pidlRoot = NULL;
	bi.lpszTitle = windowTitle;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
	
	if ((pItemIDList = pSHBrowseForFolderW(&bi)) != NULL)
	{
		ret = pSHGetPathFromIDListW(pItemIDList, folderPath);
		CoTaskMemFree(pItemIDList);
	}
	if (!ret)
	{
		*folderPathBuf = NULL;
		free(folderPath);
	}

	return ret;
}
#pragma endregion

bool MakeFileTypeFilter(const wchar_t *_filter, COMDLG_FILTERSPEC **_buf, int *_filterCount)
{
	size_t filterStringLen = wcslen(_filter);

	wchar_t *filter = (wchar_t*)malloc(sizeof(wchar_t) * (filterStringLen+1));
	if (filter == NULL)
		return false;
	wcscpy_s(filter, filterStringLen+1, _filter);

	if (filterStringLen == 0 || filter[0] == L':')
	{
		*_buf = NULL;
		*_filterCount = 0;
		return true;
	}
	int filterCount = GetFileTypeFilterCount(filter);

	//create a buffer that can at least hold all filterspecs + all strings
	uint8_t *filterMem = (uint8_t*)malloc(sizeof(COMDLG_FILTERSPEC) * (size_t)filterCount + 2 * (sizeof(wchar_t) * filterStringLen) + (size_t)filterCount);
	if (filterMem == NULL)
	{
		free(filter);
		return false;
	}

	COMDLG_FILTERSPEC *filterArray = (COMDLG_FILTERSPEC*)filterMem;
	wchar_t *stringMap = (wchar_t*)&filterMem[sizeof(COMDLG_FILTERSPEC) * (size_t)filterCount]; 

	size_t curStrMapIndex = 0;

	size_t curSpecStart = 0;
	size_t curNameStart = 0;
	for (int i = 0; i < filterCount; i++)
	{
		bool hasName = false; bool hasNextSpec = false;
		size_t nextSpecStart = filterStringLen;

		GetNextFileFilterIndices(filter, &filterStringLen, curSpecStart, hasName, hasNextSpec, curNameStart, nextSpecStart);
		
		if (!hasName) curNameStart = curSpecStart;

		size_t specSize = (hasName ? (curNameStart-1) : nextSpecStart) - curSpecStart;
		size_t nameSize = nextSpecStart - curNameStart;
		
		filterArray[i].pszSpec = &stringMap[curStrMapIndex];
		wcsncpy_s(&stringMap[curStrMapIndex], specSize+1, &filter[curSpecStart], specSize);
		stringMap[curStrMapIndex+specSize] = 0;
		curStrMapIndex += (specSize + 1);

		if (hasName)
		{
			filterArray[i].pszName = &stringMap[curStrMapIndex];
			wcsncpy_s(&stringMap[curStrMapIndex], nameSize+1, &filter[curNameStart], nameSize);
			stringMap[curStrMapIndex+nameSize] = 0;
			curStrMapIndex += (nameSize + 1);
		}
		else
			filterArray[i].pszName = filterArray[i].pszSpec;

		if ((i+1) < filterCount && !hasNextSpec)
			filterCount = i+1;
		if (!hasNextSpec)
			break;
		curSpecStart = nextSpecStart + 1;
	}
	
	free(filter);
	*_buf = filterArray;
	*_filterCount = filterCount;
	return true;
}
void FreeFileTypeFilter(COMDLG_FILTERSPEC **buf)
{
	if (buf != NULL && *buf != NULL)
	{
		free(*buf);
		*buf = NULL;
	}
}

bool TestFileDialogCompatible()
{
	if (!testedFileDialog)
	{
		IFileDialog *pfd = NULL;
		HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, 
			NULL, 
			CLSCTX_INPROC_SERVER, 
			IID_PPV_ARGS(&pfd));
		if (!SUCCEEDED(hr))
			InitFileDialogFallback();
		else
			pfd->Release();
		testedFileDialog = true;
	}
	return !useLegacyFileDialog;
}

HRESULT ShowFileOpenDialog(HWND hOwner, wchar_t **filePathBuf, const wchar_t *fileTypeFilter,
	UINT *pOutSelFilter, LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID& guid)
{
	if (!TestFileDialogCompatible())
		return ShowFileOpenDialogFallback(hOwner, filePathBuf, fileTypeFilter, pOutSelFilter, defaultFile, windowTitle);
	COMDLG_FILTERSPEC *pFilters; int filterCount;
	if (!MakeFileTypeFilter(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	HRESULT ret = ShowFileDialog(hOwner, filePathBuf, CLSID_FileOpenDialog, pFilters, filterCount, pOutSelFilter, defaultFile, windowTitle, guid);
	FreeFileTypeFilter(&pFilters);
	return ret;
}

HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner,
	WCHAR ***filePathsBuf, size_t *filePathCountBuf,
	const wchar_t *fileTypeFilter, UINT *pOutSelFilter,
	LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID& guid);
HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner, std::vector<char*> &filePathsBuf,
	const char *fileTypeFilter, UINT *pOutSelFilter,
	const char* defaultFile, const char* windowTitle,
	const GUID& guid)
{
	size_t strLenW;
	TCHAR *defaultFileT = _MultiByteToTCHAR(defaultFile, strLenW);
	TCHAR *windowTitleT = _MultiByteToTCHAR(windowTitle, strLenW);
	TCHAR *fileTypeFilterW = _MultiByteToWide(fileTypeFilter, strLenW);
	HRESULT result;
	if (!TestFileDialogCompatible())
	{
		WCHAR *filePathsW = nullptr;
		result = ShowFileOpenDialogMultiSelectFallback(hOwner, &filePathsW, fileTypeFilterW, pOutSelFilter, defaultFileT, windowTitleT);
		if (SUCCEEDED(result) && filePathsW[0] != 0)
		{
			size_t filePathsLenW = wcslen(filePathsW);
			size_t filePathsLen = (size_t)WideCharToMultiByte(CP_UTF8, 0, filePathsW, (int)filePathsLenW, NULL, 0, NULL, NULL);
			char *filePaths = new char[filePathsLen + 1];
			WideCharToMultiByte(CP_UTF8, 0, filePathsW, (int)filePathsLenW, filePaths, (int)filePathsLen, NULL, NULL);
			filePaths[filePathsLen] = 0;
			for (size_t i = 0, start = 0; i <= filePathsLen; i++)
			{
				if (filePaths[i] == L';' || filePaths[i] == 0)
				{
					filePaths[i] = 0;
					filePathsBuf.push_back(&filePaths[start]);
					start = i + 1;
				}
			}
			if (filePathsBuf.size() == 0)
				delete[] filePaths;
		}
	}
	else
	{
		WCHAR **filePathsArray; size_t filePathsCount;
		result = ShowFileOpenDialogMultiSelect(hOwner, &filePathsArray, &filePathsCount, fileTypeFilterW, pOutSelFilter, defaultFileT, windowTitleT, guid);
		if (SUCCEEDED(result) && filePathsCount > 0)
		{
			size_t totalLen = 0;
			filePathsBuf.resize(filePathsCount + 1);
			for (size_t i = 0; i < filePathsCount; i++)
			{
				size_t pathLenW = wcslen(filePathsArray[i]);
				if (pathLenW > INT_MAX) pathLenW = INT_MAX;
				size_t curBufferLen = (size_t)(WideCharToMultiByte(CP_UTF8, 0, filePathsArray[i], (int)pathLenW, NULL, 0, NULL, NULL) + 1) * sizeof(char);
				filePathsBuf[i] = (char*)totalLen;
				totalLen += curBufferLen;
			}
			filePathsBuf[filePathsCount] = (char*)totalLen;
			char *filePaths = new char[totalLen + 1];
			for (size_t i = 0; i < filePathsCount; i++)
			{
				size_t pathLenW = wcslen(filePathsArray[i]);
				if (pathLenW > INT_MAX) pathLenW = INT_MAX;

				int outLen = (int)(filePathsBuf[i+1] - filePathsBuf[i]);
				filePathsBuf[i] = &filePaths[(size_t)filePathsBuf[i]];
				WideCharToMultiByte(CP_UTF8, 0, filePathsArray[i], (int)pathLenW, filePathsBuf[i], outLen - 1, NULL, NULL);
				filePathsBuf[i][outLen - 1] = 0;
			}
			filePathsBuf.resize(filePathsCount);
			for (size_t i = 0; i < filePathsCount; i++)
				FreeCOMFilePathBuf(&filePathsArray[i]);
			delete[] filePathsArray;
		}
	}
	_FreeWCHAR(fileTypeFilterW);
	_FreeTCHAR(windowTitleT);
	_FreeTCHAR(defaultFileT);
	return result;
}
void FreeFilePathsMultiSelect(std::vector<char*> &filePathsBuf)
{
	if (filePathsBuf.size() > 0)
		delete[] filePathsBuf[0];
	filePathsBuf.clear();
}
HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner, WCHAR **filePathsBuf,
	const wchar_t *fileTypeFilter, UINT *pOutSelFilter,
	LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID &guid)
{
	if (!TestFileDialogCompatible())
		return ShowFileOpenDialogMultiSelectFallback(hOwner, filePathsBuf, fileTypeFilter, pOutSelFilter, defaultFile, windowTitle);
	WCHAR **filePathsArray; size_t filePathsCount;
	*filePathsBuf = NULL;
	HRESULT ret = ShowFileOpenDialogMultiSelect(hOwner, &filePathsArray, &filePathsCount, fileTypeFilter, pOutSelFilter, defaultFile, windowTitle, guid);
	if (!SUCCEEDED(ret))
		return ret;
	size_t resultSize = 0;
	for (size_t i = 0; i < filePathsCount; i++)
		resultSize += wcslen(filePathsArray[i]) + 1;
	if (!resultSize) resultSize = 1;
	*filePathsBuf = new WCHAR[resultSize];
	size_t resultOffset = 0;
	for (size_t i = 0; i < filePathsCount; i++)
	{
		size_t curSize = (filePathsArray[i] != NULL) ? wcslen(filePathsArray[i]) : 0;
		memcpy(&(*filePathsBuf)[resultOffset], filePathsArray[i], curSize * sizeof(WCHAR));
		(*filePathsBuf)[resultOffset + curSize] = L';';
		resultOffset += curSize + 1;
	}
	(*filePathsBuf)[resultSize - 1] = 0;
	for (size_t i = 0; i < filePathsCount; i++)
		FreeCOMFilePathBuf(&filePathsArray[i]);
	delete[] filePathsArray;
	return ret;
}
HRESULT ShowFileOpenDialogMultiSelect(HWND hOwner,
	WCHAR ***filePathsBuf, size_t *filePathCountBuf,
	const wchar_t *fileTypeFilter, UINT *pOutSelFilter,
	LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID& guid)
{
	if (pOutSelFilter)
		*pOutSelFilter = (UINT)-1;
	COMDLG_FILTERSPEC *pFilters; int filterCount;
	if (!MakeFileTypeFilter(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	*filePathsBuf = NULL; *filePathCountBuf = 0;
	IFileOpenDialog *pfd = NULL;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, 
		NULL, 
		CLSCTX_INPROC_SERVER, 
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr))
	{
		pfd->SetClientGuid(guid);
		DWORD dwOptions;
		hr = pfd->GetOptions(&dwOptions);
		if (SUCCEEDED(hr) && SUCCEEDED((hr = pfd->SetOptions(dwOptions | FOS_ALLOWMULTISELECT))))
		{
			if (windowTitle != NULL)
				pfd->SetTitle(windowTitle);
			hr = pfd->SetFileTypes(filterCount, pFilters);
			if (SUCCEEDED(hr))
			{
				hr = pfd->SetFileTypeIndex(0);
				if (defaultFile != NULL)
					pfd->SetFileName(defaultFile);
				if (SUCCEEDED(hr))
				{
					hr = pfd->Show(hOwner);
					if (SUCCEEDED(hr))
					{
						IShellItemArray *pFileItems;
						hr = pfd->GetResults(&pFileItems);
						if (SUCCEEDED(hr))
						{
							DWORD numItems;
							hr = pFileItems->GetCount(&numItems);
							if (SUCCEEDED(hr))
							{
								if (pOutSelFilter)
									pfd->GetFileTypeIndex(pOutSelFilter);
								*filePathsBuf = new WCHAR*[numItems](); *filePathCountBuf = numItems;
								for (DWORD i = 0; i < numItems; i++)
								{
									(*filePathsBuf)[i] = NULL;
									IShellItem *pFileItem = NULL;
									if (SUCCEEDED(pFileItems->GetItemAt(i, &pFileItem)))
									{
										WCHAR *filePath;
										hr = pFileItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
										if (SUCCEEDED(hr))
										{
											(*filePathsBuf)[i] = filePath;
										}
										pFileItem->Release();
									}
								}
								hr = 0;
							}
							pFileItems->Release();
						}
					}
				}
			}
		}
		pfd->Release();
	}
	FreeFileTypeFilter(&pFilters);
	return hr;
}

HRESULT ShowFileSaveDialog(HWND hOwner, wchar_t **filePathBuf,
	const wchar_t *fileTypeFilter, UINT *pOutSelFilter,
	LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID &guid)
{
	if (!TestFileDialogCompatible())
		return ShowFileSaveDialogFallback(hOwner, filePathBuf, fileTypeFilter, pOutSelFilter, defaultFile, windowTitle);
	COMDLG_FILTERSPEC *pFilters; int filterCount;
	if (!MakeFileTypeFilter(fileTypeFilter, &pFilters, &filterCount))
		return E_OUTOFMEMORY;
	HRESULT ret = ShowFileDialog(hOwner, filePathBuf, CLSID_FileSaveDialog, pFilters, filterCount, pOutSelFilter, defaultFile, windowTitle, guid);
	FreeFileTypeFilter(&pFilters);
	return ret;
}

HRESULT ShowFileDialog(HWND hOwner, wchar_t **filePathBuf,
	CLSID dialogClass,
	const COMDLG_FILTERSPEC *pFilters, int filtersCount, UINT *pOutSelFilter,
	LPCTSTR defaultFile, LPCTSTR windowTitle,
	const GUID& guid)
{
	if (pOutSelFilter)
		*pOutSelFilter = (UINT)-1;
	if (!TestFileDialogCompatible())
		return E_NOTIMPL;
	*filePathBuf = NULL;
	IFileDialog *pfd = NULL;
	HRESULT hr = CoCreateInstance(dialogClass, 
		NULL, 
		CLSCTX_INPROC_SERVER, 
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr))
	{
		pfd->SetClientGuid(guid);
		if (windowTitle != NULL)
			pfd->SetTitle(windowTitle);
		hr = pfd->SetFileTypes(filtersCount, pFilters);
		if (SUCCEEDED(hr))
		{
			hr = pfd->SetFileTypeIndex(0);
			if (defaultFile != NULL)
				pfd->SetFileName(defaultFile);
			if (SUCCEEDED(hr))
			{
				hr = pfd->Show(hOwner);
				if (SUCCEEDED(hr))
				{
					IShellItem *pFileItem;
					hr = pfd->GetResult(&pFileItem);
					if (SUCCEEDED(hr))
					{
						WCHAR *filePath;
						hr = pFileItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
						if (SUCCEEDED(hr))
						{
							if (pOutSelFilter)
								pfd->GetFileTypeIndex(pOutSelFilter);
							*filePathBuf = filePath;
						}
						pFileItem->Release();
					}
				}
			}
		}
		pfd->Release();
	}
	return hr;
}
HRESULT ShowPickFolderDialog(HWND hOwner, wchar_t **folderPathBuf, CLSID dialogClass, LPCWSTR windowTitle,
	const GUID& guid)
{
	if (!TestFileDialogCompatible())
		return E_NOTIMPL;
	*folderPathBuf = NULL;
	IFileDialog *pfd = NULL;
	HRESULT hr = CoCreateInstance(dialogClass, 
		NULL, 
		CLSCTX_INPROC_SERVER, 
		IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr))
	{
		pfd->SetClientGuid(guid);
		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
		{
			pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
		}
		if (windowTitle != NULL)
			pfd->SetTitle(windowTitle);
		hr = pfd->Show(hOwner);
		if (SUCCEEDED(hr))
		{
			IShellItem *pFileItem;
			hr = pfd->GetResult(&pFileItem);
			if (SUCCEEDED(hr))
			{
				WCHAR *filePath;
				hr = pFileItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
				if (SUCCEEDED(hr))
				{
					*folderPathBuf = filePath;
				}
				pFileItem->Release();
			}
		}
		pfd->Release();
	}
	return hr;
}

BOOL ShowFolderSelectDialog(HWND hOwner, WCHAR **folderPathBuf, LPCWSTR windowTitle, const GUID &guid)
{
	if (!TestFileDialogCompatible())
		return ShowFolderSelectDialogFallback(hOwner, folderPathBuf, windowTitle);
	return SUCCEEDED(ShowPickFolderDialog(hOwner, folderPathBuf, CLSID_FileOpenDialog, windowTitle, guid));
}

void FreeCOMFilePathBuf(WCHAR **filePath)
{
	if (useLegacyFileDialog)
	{
		if (filePath != NULL && *filePath != NULL)
		{
			free(*filePath);
			*filePath = NULL;
		}
	}
	else if (filePath != NULL && *filePath != NULL)
	{
		CoTaskMemFree(*filePath);
		*filePath = NULL;
	}
}