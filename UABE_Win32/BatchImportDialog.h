#pragma once
#include "api.h"
#include <vector>
#include <string>
#include <regex>
#include <functional>
#include "../UABE_Generic/IAssetBatchImportDesc.h"
#include "Win32BatchImportDesc.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

class CBatchImportDialog
{
	class AssetInfo
	{
	public:
		class FileListEntry
		{
		public:
			std::string path;
			bool isRelative; //Path is relative to wcBasePath.
		};
		std::string description;
		std::string assetsFileName;
		long long int pathId;
		std::vector<FileListEntry> fileList;
	};
	HWND hParentWnd;
	HWND hWnd; bool modeless;
	HINSTANCE hInstance;
	int dialogSortColumnIdx;
	bool dialogSortDirReverse;
	bool updatingAssetList;

	IAssetBatchImportDesc *pDesc;
	IWin32AssetBatchImportDesc* pDescWin32;
	std::string basePath;

	std::vector<AssetInfo> assetInfo;

	std::function<void(bool)> closeCallback;

	static int CALLBACK AssetlistSortCallback(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
	static INT_PTR CALLBACK WindowHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	//Returns -1 if no valid asset is selected.
	size_t GetCurAssetInfoIndex(unsigned int selection = -1);
	//Updates the file list.
	void UpdateDialogFileList(size_t assetInfoIndex);
	//Returns whether at least one file has been found.
	bool SearchDirectory(const std::wstring &path, const std::string &relativePath, std::vector<std::regex> &regexs, bool searchSubDirs);
	bool GenerateFileLists();
public:
	//The pointer to wcBasePath must not be freed before Show() has returned!
	UABE_Win32_API CBatchImportDialog(HINSTANCE hInstance,
		IAssetBatchImportDesc* pDesc, IWin32AssetBatchImportDesc* pDescWin32,
		std::string basePath);
	UABE_Win32_API ~CBatchImportDialog();
	UABE_Win32_API void Hide();
	UABE_Win32_API bool ShowModal(HWND hParentWnd);
	UABE_Win32_API bool ShowModeless(HWND hParentWnd);
	inline void SetCloseCallback(std::function<void(bool)> callback)
	{
		this->closeCallback = callback;
	}
	inline HWND getWindowHandle()
	{
		return hWnd;
	}
};