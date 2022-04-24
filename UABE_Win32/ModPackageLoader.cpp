#include "stdafx.h"
#include "ModPackageLoader.h"
#include "FileDialog.h"
#include "resource.h"
#include "../libStringConverter/convert.h"
#include "../ModInstaller/ModInstaller.h"
#include <Shlwapi.h>
#include <WindowsX.h>

Win32ModPackageLoader::Win32ModPackageLoader(Win32AppContext &appContext)
	: Win32ModTreeDialogBase(appContext)
{}

void Win32ModPackageLoader::open()
{
	WCHAR *packageFilePath = NULL;
	if (FAILED(ShowFileOpenDialog(appContext.getMainWindow().getWindow(), &packageFilePath,
		L"*.emip|UABE Mod Installer Package:*.exe|UABE Installer:",
		nullptr, nullptr, nullptr, UABE_FILEDIALOG_FILE_GUID)))
		return;
	std::shared_ptr<IAssetsReader> pPackageFileReader(Create_AssetsReaderFromFile(packageFilePath, true, RWOpenFlags_Immediately), Free_AssetsReader);
	FreeCOMFilePathBuf(&packageFilePath);
	if (!pPackageFileReader)
	{
		MessageBox(appContext.getMainWindow().getWindow(), TEXT("Unable to open the package file!"), TEXT("ERROR"), 16);
		return;
	}
	bool closeReader = true;
	InstallerPackageFile packageFile;
	QWORD filePos = 0;
	bool success = packageFile.Read(filePos, pPackageFileReader/*, true*/);
	if (!success)
	{
		size_t overlayOffset = GetPEOverlayOffset(pPackageFileReader.get());
		if (overlayOffset != 0)
		{
			filePos = overlayOffset;
			success = packageFile.Read(filePos, pPackageFileReader);
		}
	}
	if (!success)
	{
		MessageBox(appContext.getMainWindow().getWindow(), TEXT("Unable to understand the package file!\n")\
			TEXT("Make sure the selected file actually is a valid package file."), TEXT("ERROR"), 16);
		return;
	}

	visibleFiles.clear();
	visibleFiles.reserve(packageFile.affectedAssets.size());
	for (size_t i = 0; i < packageFile.affectedAssets.size(); ++i)
	{
		visibleFiles.push_back(VisibleFileEntry(this->appContext, packageFile.affectedAssets[i]));
	}
	DialogBoxParam(appContext.getMainWindow().getHInstance(),
		MAKEINTRESOURCE(IDD_LOADFROMPACKAGE),
		appContext.getMainWindow().getWindow(),
		DialogProc, (LPARAM)this);
	
	for (size_t i = 0; i < visibleFiles.size(); ++i)
	{
		bool basedOnExistingFile = true;
		if (visibleFiles[i].fileType == FileContext_Resources)
		{
			//Allow new resource files to be created.
			//-> Check if the resource replacer is based on an existing file.
			//   If not, allow CreateFileOpenTask to open a 'zero length' in-memory reader
			//    instead of the actual file.
			assert(visibleFiles[i].replacers.size() == 1);
			if (visibleFiles[i].replacers.size() == 1
				&& !reinterpret_cast<BundleReplacer*>(visibleFiles[i].replacers[0].pReplacer.get())->RequiresEntryReader())
				basedOnExistingFile = false;
		}
		std::shared_ptr<ITask> pTask = appContext.CreateFileOpenTask(visibleFiles[i].pathOrName, basedOnExistingFile);
		if (pTask != nullptr)
		{
			appContext.OpenTask_SetModifications(pTask.get(), std::unique_ptr<VisibleFileEntry>(new VisibleFileEntry(std::move(visibleFiles[i]))));
			appContext.taskManager.enqueue(pTask);
		}
	}
	visibleFiles.clear();
}

void Win32ModPackageLoader::FillModifiedFilesTree()
{
	UpdateModsTree(false);

	TVITEM item;
	item.hItem = bundleBaseEntry;
	item.mask = TVIF_HANDLE | TVIF_STATE;
	item.stateMask = TVIS_STATEIMAGEMASK;
	item.state = 0 << 12;
	TreeView_SetItem(hTreeModifications, &item);

	item.hItem = assetsBaseEntry;
	item.mask = TVIF_HANDLE | TVIF_STATE;
	item.stateMask = TVIS_STATEIMAGEMASK;
	item.state = 0 << 12;
	TreeView_SetItem(hTreeModifications, &item);
}

void Win32ModPackageLoader::OnChangeBaseFolderEdit()
{
	HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
	if (!hEditBaseFolder)
		return;
	size_t textLen = (size_t)Edit_GetTextLength(hEditBaseFolder);
	std::vector<wchar_t> newBasePath(textLen+1);
	Edit_GetText(hEditBaseFolder, newBasePath.data(), (int)(textLen + 1));
	newBasePath[textLen] = 0;

	size_t wBasePathLen = wcslen(newBasePath.data());
	for (size_t i = 0; i < this->visibleFiles.size(); i++)
	{
		size_t wPathLen = 0;
		auto wPath = unique_MultiByteToWide(this->visibleFiles[i].pathOrName.c_str(), wPathLen);
		std::vector<wchar_t> combinedPathBuf(std::max<size_t>(wBasePathLen + wPathLen + 16, MAX_PATH+1));
		//TODO: Use a long path compatible function.
		bool changePath = PathCombine(combinedPathBuf.data(), newBasePath.data(), wPath.get()) != NULL;
		if (changePath)
		{
			TVITEMEX item = {};
			item.hItem = (HTREEITEM)this->visibleFiles[i].treeViewEntry;
			item.mask = TVIF_HANDLE | TVIF_TEXT;
			item.pszText = combinedPathBuf.data();
			TreeView_SetItem(hTreeModifications, &item);
		}
	}
}

void Win32ModPackageLoader::OnCheck(HTREEITEM item, bool isChecked)
{
	struct _Lambda_RecursiveCheck{
		HWND hTreeModifications;
		bool isChecked;
		_Lambda_RecursiveCheck(HWND hTreeModifications, bool isChecked)
			: hTreeModifications(hTreeModifications), isChecked(isChecked)
		{}
		void operator()(VisibleFileEntry &file)
		{
			if (!file.treeViewEntry)
				return;
			TreeView_SetCheckState(hTreeModifications, (HTREEITEM)file.treeViewEntry, isChecked);
			for (size_t i = 0; i < file.subFiles.size(); ++i)
				(*this)(file.subFiles[i]);
		}
		bool operator()(HTREEITEM base, VisibleFileEntry &file)
		{
			if (!file.treeViewEntry)
				return false;
			if (file.treeViewEntry == (uintptr_t)base)
			{
				for (size_t i = 0; i < file.subFiles.size(); ++i)
					(*this)(file.subFiles[i]);
				return true;
			}
			else
			{
				for (size_t i = 0; i < file.subFiles.size(); ++i)
				{
					if ((*this)(base, file.subFiles[i]))
						return true;
				}
			}
			return false;
		}
	} recursiveCheck(hTreeModifications, isChecked);
	if (item == assetsBaseEntry || item == NULL)
	{
		for (size_t i = 0; i < visibleFiles.size(); i++)
		{
			if (visibleFiles[i].fileType == FileContext_Assets)
				recursiveCheck(visibleFiles[i]);
		}
	}
	if (item == bundleBaseEntry || item == NULL)
	{
		for (size_t i = 0; i < visibleFiles.size(); i++)
		{
			if (visibleFiles[i].fileType == FileContext_Bundle)
				recursiveCheck(visibleFiles[i]);
		}
	}
	else
	{
		for (size_t i = 0; i < visibleFiles.size(); i++)
		{
			if (recursiveCheck(item, visibleFiles[i]))
				break;
		}
	}
}
void Win32ModPackageLoader::RemoveUnchecked()
{
	struct _Lambda_RecursiveRemoveIfUnchecked {
		HWND hTreeModifications;
		_Lambda_RecursiveRemoveIfUnchecked(HWND hTreeModifications)
			: hTreeModifications(hTreeModifications)
		{}
		bool operator()(VisibleFileEntry &file)
		{
			if (file.treeViewEntry == NULL || !TreeView_GetCheckState(hTreeModifications, file.treeViewEntry))
				return true;
			for (size_t _i = file.subFiles.size(); _i > 0; --_i)
			{
				size_t i = _i - 1;
				if ((*this)(file.subFiles[i]))
				{
					if (file.subFiles[i].treeViewEntry != NULL)
						TreeView_DeleteItem(hTreeModifications, file.subFiles[i].treeViewEntry);
					file.subFiles.erase(file.subFiles.begin() + i);
				}
			}
			return false;
		}
	} recursiveRemoveIfUnchecked(hTreeModifications);
	for (size_t _i = visibleFiles.size(); _i > 0; --_i)
	{
		size_t i = _i - 1;
		if (recursiveRemoveIfUnchecked(visibleFiles[i]))
		{
			if (visibleFiles[i].treeViewEntry != NULL)
				TreeView_DeleteItem(hTreeModifications, visibleFiles[i].treeViewEntry);
			visibleFiles.erase(visibleFiles.begin() + i);
		}
	}
}
void Win32ModPackageLoader::OnClose()
{
	if (this->hTreeModifications == NULL)
		return;
	this->RemoveUnchecked();
	
	HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
	size_t baseDirLen = (size_t)Edit_GetTextLength(hEditBaseFolder);
	std::vector<TCHAR> tBaseDir(baseDirLen + 1);
	Edit_GetText(hEditBaseFolder, tBaseDir.data(), (int)(baseDirLen + 1));
	tBaseDir[baseDirLen] = 0;

	std::vector<TCHAR> pathBuffer;
	for (size_t i = 0; i < this->visibleFiles.size(); i++)
	{
		TVITEM item;
		item.mask = TVIF_HANDLE | TVIF_STATE;
		item.hItem = (HTREEITEM)this->visibleFiles[i].treeViewEntry;
		item.state = 0;

		size_t newPathLen = baseDirLen + this->visibleFiles[i].pathOrName.size() + 1;
		if (newPathLen <= MAX_PATH) newPathLen = MAX_PATH + 1;
			
		//Retrieve the absolute paths from the TreeView.
		TVITEMEX itemex;
		do {
			if (newPathLen >= INT_MAX-1) newPathLen = INT_MAX-2;
			if (pathBuffer.size() < newPathLen) pathBuffer.resize(newPathLen);
			itemex.hItem = (HTREEITEM)this->visibleFiles[i].treeViewEntry;
			itemex.mask = TVIF_HANDLE | TVIF_TEXT;
			itemex.pszText = pathBuffer.data();
			itemex.cchTextMax = (int)pathBuffer.size();
			TreeView_GetItem(hTreeModifications, &itemex);
			pathBuffer[pathBuffer.size() - 1] = 0;
			newPathLen += MAX_PATH;
		} while (newPathLen < INT_MAX-2 && _tcslen(itemex.pszText) >= pathBuffer.size() - 1);
		
		if (pathBuffer[0] != 0)
		{
			//Put the absolute path in the file entries.
			auto newPath8 = unique_TCHARToMultiByte(pathBuffer.data());
			this->visibleFiles[i].pathOrName = newPath8.get();
		}
	}

	TreeView_DeleteAllItems(hTreeModifications);
	HIMAGELIST hCBImageList = TreeView_GetImageList(hTreeModifications, TVSIL_STATE);
	if (hCBImageList)
		ImageList_Destroy(hCBImageList);
}

INT_PTR CALLBACK Win32ModPackageLoader::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	Win32ModPackageLoader *pThis = reinterpret_cast<Win32ModPackageLoader*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
		case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = reinterpret_cast<Win32ModPackageLoader*>(lParam);
			pThis->hDlg = hDlg;
			pThis->hTreeModifications = GetDlgItem(hDlg, IDC_TREECHANGES);

			DWORD dwStyle = GetWindowLong(pThis->hTreeModifications, GWL_STYLE);
			dwStyle |= TVS_CHECKBOXES;
			SetWindowLong(pThis->hTreeModifications, GWL_STYLE, dwStyle);

			ShowWindow(pThis->hTreeModifications, SW_HIDE);
			pThis->FillModifiedFilesTree();
			ShowWindow(pThis->hTreeModifications, SW_SHOW);
			SetWindowPos(hDlg, NULL, 0, 0, 255, 360, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
		//Also see http://stackoverflow.com/questions/22441747/creating-treeview-with-nodes-and-checkboxes
		case WM_NOTIFY:
		if (pThis != nullptr)
		{
			NMHDR *pNotifHdr = (NMHDR*)lParam;

			if (pNotifHdr->idFrom != IDC_TREECHANGES)
				break;
			switch (pNotifHdr->code)
			{
				case TVN_KEYDOWN:
					if (((NMTVKEYDOWN*)lParam)->wVKey == VK_SPACE)
					{
						HTREEITEM checkedItem = TreeView_GetSelection(pNotifHdr->hwndFrom);
						bool isChecked = TreeView_GetCheckState(pNotifHdr->hwndFrom, checkedItem) == 0; //will be checked, isn't at the moment
						pThis->OnCheck(checkedItem, isChecked);
						return (INT_PTR)TRUE;
					}
					break;
				case NM_CLICK:
				{
					TVHITTESTINFO hitTest = {};
					DWORD posval = GetMessagePos();
					hitTest.pt.x = GET_X_LPARAM(posval);
					hitTest.pt.y = GET_Y_LPARAM(posval);
					MapWindowPoints(NULL, pNotifHdr->hwndFrom, &hitTest.pt, 1);
					TreeView_HitTest(pNotifHdr->hwndFrom, &hitTest);
					if (hitTest.flags & TVHT_ONITEMSTATEICON)
					{
						bool isChecked = TreeView_GetCheckState(pNotifHdr->hwndFrom, hitTest.hItem) == 0; //will be checked, isn't at the moment
						pThis->OnCheck(hitTest.hItem, isChecked);
						return (INT_PTR)TRUE;
					}
				}
				break;
			default:
				break;
			}
			break;
		}
		return (INT_PTR)FALSE;
		case WM_SIZE:
			{
				int width = LOWORD(lParam); int height = HIWORD(lParam);
				SetWindowPos(GetDlgItem(hDlg, IDC_SDESCRIPTION), NULL, 9, 11, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_TREECHANGES), NULL, 9, 26, width - 18, height - 108, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_SBASEFOLDER), NULL, 9, height - 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_EBASEFOLDER), NULL, 9, height - 63, (int)((float)(width - 23) * 0.69F), 23, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_BTNBASEFOLDER), NULL, (int)((float)width * 0.69F - 1.87F), height - 62, (int)((float)(width - 23) * 0.31F), 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDOK), NULL, 9, height - 33, width / 3, 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDCANCEL), NULL, width - 9 - width / 3, height - 33, width / 3, 21, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			return (INT_PTR)TRUE;
		case WM_CLOSE:
		case WM_DESTROY:
			if (pThis) pThis->OnClose();
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case WM_COMMAND:
			wmId    = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			switch (wmId)
			{
				case IDC_EBASEFOLDER:
					if (!pThis)
						break;
					pThis->OnChangeBaseFolderEdit();
					return (INT_PTR)TRUE;
				case IDC_BTNBASEFOLDER:
					if (!pThis)
						break;
					{
						HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
						WCHAR *folderPath = NULL;
						if (hEditBaseFolder && ShowFolderSelectDialog(hDlg, &folderPath, L"Select a base directory", UABE_FILEDIALOG_FILE_GUID))
						{
							Edit_SetText(hEditBaseFolder, folderPath);
							FreeCOMFilePathBuf(&folderPath);
						}
						return (INT_PTR)TRUE;
					}
				case IDOK:
					if (pThis) pThis->OnClose();
					EndDialog(hDlg, (INT_PTR)0);
					return (INT_PTR)TRUE;
				case IDCANCEL:
					{
						if (pThis)
							pThis->OnCheck(NULL, false);
						if (pThis) pThis->OnClose();
						EndDialog(hDlg, (INT_PTR)0);
						return (INT_PTR)TRUE;
					}
			}
	}
	return (INT_PTR)FALSE;
}
