#include "stdafx.h"
#include "resource.h"
#include "MainWindow2.h"
#include "AssetDependDialog.h"
#include "../libStringConverter/convert.h"
#include <WindowsX.h>

/*
TODO
- Add/Remove dependencies - should be possible with the existing AssetsFileContextInfo and replacers infrastructure.
*/

AssetDependDialog::~AssetDependDialog()
{
	pContext->getMainWindow().unregisterEventHandler(eventHandlerHandle);
}
AssetDependDialog::AssetDependDialog(class Win32AppContext *pContext, HWND hParentWnd)
	: pContext(pContext), hParentWnd(hParentWnd), hDialog(NULL),
	hCurEditPopup(NULL), iEditPopupItem(0), iEditPopupSubItem(0),
	pCurFileEntry(nullptr)
{
	eventHandlerHandle = pContext->getMainWindow().registerEventHandler(this);
}

void AssetDependDialog::addFileContext(const std::pair<FileEntryUIInfo*,uintptr_t> &fileContext)
{
	FileContextInfo *pContextInfo = fileContext.first->getContextInfoPtr();
	if (AssetsFileContextInfo *pAssetsInfo = dynamic_cast<AssetsFileContextInfo*>(pContextInfo))
	{
		unsigned int fileID = pAssetsInfo->getFileID();
		auto entryIt = fileEntries.find(fileID);
		assert(entryIt == fileEntries.end());
		if (entryIt == fileEntries.end())
		{
			fileEntries.insert(std::make_pair(fileID, fileContext.first));
			if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr
				|| pAssetsInfo->getFileID() < pCurFileEntry->getContextInfoPtr()->getFileID())
			{
				pCurFileEntry = fileContext.first;
				onUpdateCurrentFile();
			}
		}
	}

}
void AssetDependDialog::removeFileContext(FileEntryUIInfo *pContext)
{
	FileContextInfo *pContextInfo = pContext->getContextInfoPtr();
	if (AssetsFileContextInfo *pAssetsInfo = dynamic_cast<AssetsFileContextInfo*>(pContextInfo))
	{
		unsigned int fileID = pAssetsInfo->getFileID();
		auto entryIt = fileEntries.find(fileID);
		assert(entryIt != fileEntries.end());
		assert(entryIt->second == pContext);
		if (entryIt != fileEntries.end())
			fileEntries.erase(entryIt);
	}
	else
	{
		for (auto it = fileEntries.begin(); it != fileEntries.end(); ++it)
		{
			if (it->second == pContext)
			{
				fileEntries.erase(it);
				break;
			}
		}
	}
	if (pContext == pCurFileEntry)
	{
		if (!fileEntries.empty())
			pCurFileEntry = fileEntries.begin()->second;
		else
			pCurFileEntry = nullptr;
		onUpdateCurrentFile();
	}
}
EFileManipulateDialogType AssetDependDialog::getType()
{
	return FileManipulateDialog_AssetsDependencies;
}
HWND AssetDependDialog::getWindowHandle()
{
	return hDialog;
}
void AssetDependDialog::onHotkey(ULONG message, DWORD keyCode) //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
{

}
bool AssetDependDialog::onCommand(WPARAM wParam, LPARAM lParam) //Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
{
	return false;
}
void AssetDependDialog::onShow()
{
	if (!this->hDialog)
	{
		this->hDialog = CreateDialogParam(pContext->getMainWindow().getHInstance(), MAKEINTRESOURCE(IDD_ASSETSDEPEND2), hParentWnd, AssetDependProc, (LPARAM)this);
		onUpdateCurrentFile();
	}
}
void AssetDependDialog::onHide()
{
	if (this->hDialog)
	{
		if (this->hCurEditPopup != NULL)
			doCloseEditPopup();
		SendMessage(this->hDialog, WM_CLOSE, 0, 0);
	}
}
bool AssetDependDialog::hasUnappliedChanges(bool *applyable)
{
	return false;
}
bool AssetDependDialog::applyChanges()
{
	return true;
}
bool AssetDependDialog::doesPreferNoAutoclose()
{
	return false;
}

void AssetDependDialog::list_updateAssetPath(HWND hList, int iItem, const AssetsFileDependency *pDependency)
{
	auto upText = unique_MultiByteToTCHAR(pDependency->assetPath);
	LVITEM item;
	item.mask = LVIF_TEXT;
	item.pszText = upText.get();
	item.cchTextMax = 256;
	item.iItem = iItem;
	item.iSubItem = 1;
	ListView_SetItem(hList, &item);
}
void AssetDependDialog::list_updateTargetAsset(HWND hList, int iItem, unsigned int reference)
{
	LVITEM item;
	item.mask = LVIF_TEXT;
	item.cchTextMax = 256;
	item.pszText = const_cast<TCHAR*>(TEXT("None"));
	TCHAR *resolvedToText = nullptr;
	if (reference != 0)
	{
		if (FileContextInfo_ptr pReferencedInfo = this->pContext->getContextInfo(reference))
		{
			char referenceFileIDTmp[32];
			sprintf_s(referenceFileIDTmp, "%u - ", reference);
			std::string resolvedToText8 = std::string(referenceFileIDTmp) + pReferencedInfo->getFileName();

			size_t tmp;
			resolvedToText = _MultiByteToTCHAR(resolvedToText8.c_str(), tmp);
			item.pszText = resolvedToText;
		}
	}
	item.iItem = iItem;
	item.iSubItem = 2;
	ListView_SetItem(hList, &item);
	if (resolvedToText)
		_FreeTCHAR(resolvedToText);
}

void AssetDependDialog::onUpdateCurrentFile()
{
	HWND hList = GetDlgItem(hDialog, IDC_DEPENDLIST);
	ListView_DeleteAllItems(hList);
	if (pCurFileEntry != nullptr && pCurFileEntry->getContextInfoPtr() != nullptr)
	{
		auto* pContextInfo = static_cast<AssetsFileContextInfo*>(pCurFileEntry->getContextInfoPtr());
		auto refLock = pContextInfo->lockReferencesRead();
		const std::vector<unsigned int> &references = pContextInfo->getReferencesRead(refLock);
		const std::vector<AssetsFileDependency> &dependencies = pContextInfo->getDependenciesRead(refLock);

		int listViewCount = 0;
		for (size_t i = 0; i < dependencies.size(); i++)
		{
			const AssetsFileDependency &dependency = dependencies[i];
			if (dependency.type != 0)
				continue;
			TCHAR fileIDTmp[32];
			_stprintf_s(fileIDTmp, TEXT("%u"), i + 1);
			LVITEM item;
			item.mask = LVIF_TEXT | LVIF_PARAM;
			item.lParam = (LPARAM)i;
			item.iItem = listViewCount;
			item.iSubItem = 0;
			item.pszText = fileIDTmp;
			ListView_InsertItem(hList, &item);
					
			list_updateAssetPath(hList, listViewCount, &dependency);
					
			list_updateTargetAsset(hList, listViewCount, references[i]);

			assert(listViewCount < INT_MAX);
			listViewCount++;
		}
	}
}

inline void doMoveWindow(HDWP &deferCtx, bool &retry, HWND hWnd, int x, int y, int w, int h)
{
	if (deferCtx)
	{
		deferCtx = DeferWindowPos(deferCtx, hWnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		if (!deferCtx)
			retry = true;
	}
	else
		SetWindowPos(hWnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
}
static void onResize(HWND hDlg, bool defer = true)
{
	//Add/Remove is not implemented
	ShowWindow(GetDlgItem(hDlg, IDC_BTNADD), SW_HIDE);
	ShowWindow(GetDlgItem(hDlg, IDC_BTNREMOVE), SW_HIDE);

	HDWP deferCtx = defer ? BeginDeferWindowPos(12) : NULL;
	bool retry = false;

	RECT client = {};
	GetClientRect(hDlg, &client);
	LONG clientWidth = client.right-client.left;
	LONG clientHeight = client.bottom-client.top;
	LONG x = 19;
	LONG w = clientWidth - 16;

	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_STATICTITLE),     x + 2,       10,                w - 4,  15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_DEPENDLIST), x,           30,                w - 15, clientHeight - 60 - 7);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNADD),     x + 5,       clientHeight - 30, 125,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNREMOVE),  x + 5 + 145, clientHeight - 30, 125,    25);

	if (defer)
	{
		if (retry || !EndDeferWindowPos(deferCtx))
			onResize(hDlg, false);
		else
			UpdateWindow(hDlg);
		deferCtx = NULL;
	}
	else
		UpdateWindow(hDlg);

}

void AssetDependDialog::doCloseEditPopup(bool applyChanges)
{
	if (applyChanges)
	{
		if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
		{
			doCloseEditPopup(false);
			return;
		}
		AssetsFileContextInfo *pAssetsFileInfo = static_cast<AssetsFileContextInfo*>(pCurFileEntry->getContextInfoPtr());

		HWND hList = GetDlgItem(hDialog, IDC_DEPENDLIST);
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)-1;
		item.iItem = iEditPopupItem;
		item.iSubItem = 0;
		ListView_GetItem(hList, &item);

		size_t dependencyIdx = item.lParam;
		auto refLock = pAssetsFileInfo->lockReferencesWrite();
		std::vector<unsigned int> &references = pAssetsFileInfo->getReferencesWrite(refLock);
		std::vector<AssetsFileDependency>& dependencies = pAssetsFileInfo->getDependenciesWrite(refLock);

		assert(dependencyIdx < dependencies.size());
		assert(dependencies.size() == references.size());
		if (dependencyIdx >= dependencies.size()
			|| dependencyIdx >= references.size())
		{
			refLock.unlock();
			doCloseEditPopup(false);
			return;
		}
		
		switch (iEditPopupSubItem)
		{
			case 1: //Dependency name/text
				{
					int nameLen = Edit_GetTextLength(hCurEditPopup);
					if (nameLen > 0 && nameLen < INT_MAX - 2)
					{
						std::unique_ptr<TCHAR[]> nameT(new TCHAR[nameLen + 2]);
						nameT[0] = 0;
						nameT[nameLen + 1] = 0;
						Edit_GetText(hCurEditPopup, nameT.get(), nameLen + 1);
						size_t name8Len = 0;
						auto name8 = unique_TCHARToMultiByte(nameT.get(), name8Len);
						AssetsFileDependency *pDependencyEntry = &dependencies[dependencyIdx];
						if (name8Len < sizeof(pDependencyEntry->assetPath) / sizeof(char))
						{
							memcpy_s(pDependencyEntry->assetPath, sizeof(pDependencyEntry->assetPath),
								name8.get(), (name8Len + 1) * sizeof(char));
							pAssetsFileInfo->setDependenciesChanged();
							list_updateAssetPath(hList, iEditPopupItem, pDependencyEntry);
						}
					}
					refLock.unlock();
					pContext->OnUpdateDependencies(pAssetsFileInfo, dependencyIdx, dependencyIdx);
				}
				break;
			case 2: //Dependency target
				{
					int iSelection = ComboBox_GetCurSel(hCurEditPopup);
					if (iSelection < 0)
						break;
					unsigned int newReferenceTarget = 0;
					if (iSelection == 0)
						newReferenceTarget = 0;
					else
					{
						size_t iComboBoxItem = 1;
						auto &fileEntries = pContext->getMainWindow().getFileEntries();
						//Slightly janky. A mapping from combo box index to file ID would be better.
						for (auto fileIt = fileEntries.begin(); fileIt != fileEntries.end(); ++fileIt)
						{
							if (fileIt->pContextInfo != pCurFileEntry->pContextInfo &&
								fileIt->pContextInfo && 
								fileIt->pContextInfo->getFileContext() && 
								fileIt->pContextInfo->getFileContext()->getType() == FileContext_Assets)
							{
								if (iComboBoxItem == iSelection)
								{
									newReferenceTarget = fileIt->pContextInfo->getFileID();
									break;
								}
								iComboBoxItem++;
							}
						}
					}
					unsigned int prevReferenceTarget = references[dependencyIdx];
					references[dependencyIdx] = newReferenceTarget;

					refLock.unlock();

					if (prevReferenceTarget != 0 && newReferenceTarget != prevReferenceTarget)
					{
						//Remove the containerSources entry in the previously referenced AssetsFileContextInfo, if it exists.
						FileContextInfo_ptr pOldTargetFileInfo = pContext->getContextInfo(prevReferenceTarget);
						//May be null (if the referenced file was closed).
						if (pOldTargetFileInfo &&
							pOldTargetFileInfo->getFileContext() &&
							pOldTargetFileInfo->getFileContext()->getType() == FileContext_Assets)
						{
							AssetsFileContextInfo *pTargetAssetsInfo = static_cast<AssetsFileContextInfo*>(pOldTargetFileInfo.get());
							std::vector<unsigned int> &containerSources = pTargetAssetsInfo->getContainerSources();
							for (size_t i = 0; i < containerSources.size(); i++)
							{
								if (containerSources[i] == pAssetsFileInfo->getFileID())
								{
									containerSources.erase(containerSources.begin() + i);
									pContext->OnUpdateContainers(pTargetAssetsInfo);
									break;
								}
							}
						}
					}
					bool hasContainers;
					{
						AssetContainerList &containers = pAssetsFileInfo->lockContainersRead();
						hasContainers = (containers.getContainerCount() > 0);
						pAssetsFileInfo->unlockContainersRead();
					}
					if (newReferenceTarget != 0 && hasContainers)
					{
						//Add a containerSources entry in the newly referenced AssetsFileContextInfo, if there is a need for it.
						FileContextInfo_ptr pNewTargetFileInfo = pContext->getContextInfo(newReferenceTarget);
						assert(pNewTargetFileInfo &&
							pNewTargetFileInfo->getFileContext() &&
							pNewTargetFileInfo->getFileContext()->getType() == FileContext_Assets);
						if (pNewTargetFileInfo &&
							pNewTargetFileInfo->getFileContext() &&
							pNewTargetFileInfo->getFileContext()->getType() == FileContext_Assets)
						{
							AssetsFileContextInfo *pTargetAssetsInfo = static_cast<AssetsFileContextInfo*>(pNewTargetFileInfo.get());
							std::vector<unsigned int> &containerSources = pTargetAssetsInfo->getContainerSources();
							bool alreadyExists = false;
							for (size_t i = 0; i < containerSources.size(); i++)
							{
								if (containerSources[i] == pAssetsFileInfo->getFileID())
								{
									alreadyExists = true;
									break;
								}
							}
							if (!alreadyExists)
							{
								containerSources.push_back(pAssetsFileInfo->getFileID());
								pContext->OnUpdateContainers(pTargetAssetsInfo);
							}
						}
					}
					list_updateTargetAsset(hList, iEditPopupItem, newReferenceTarget);
					pContext->OnUpdateDependencies(pAssetsFileInfo, dependencyIdx, dependencyIdx);
				}
				break;
			default:
				assert(false);
		}
	}
	DestroyWindow(hCurEditPopup);
	hCurEditPopup = NULL;
	iEditPopupItem = iEditPopupSubItem = 0;
}

void AssetDependDialog::onOpenEditPopup()
{
	if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
	{
		doCloseEditPopup(false);
		return;
	}
	auto pContextInfo = static_cast<AssetsFileContextInfo*>(pCurFileEntry->getContextInfoPtr());
	auto refLock = pContextInfo->lockReferencesRead();
	const std::vector<unsigned int>& references = pContextInfo->getReferencesRead(refLock);
	const std::vector<AssetsFileDependency>& dependencies = pContextInfo->getDependenciesRead(refLock);

	HWND hList = GetDlgItem(hDialog, IDC_DEPENDLIST);
	LVITEM item;
	item.mask = LVIF_PARAM;
	item.lParam = (LPARAM)-1;
	item.iItem = iEditPopupItem;
	item.iSubItem = 0;
	ListView_GetItem(hList, &item);

	size_t dependencyIdx = item.lParam;
	assert(dependencyIdx < dependencies.size());
	assert(dependencies.size() == references.size());
	if (dependencyIdx >= dependencies.size()
		|| dependencyIdx >= references.size())
	{
		doCloseEditPopup(false);
		return;
	}

	switch (iEditPopupSubItem)
	{
		case 1: //Dependency name/text
			{
				auto pText = unique_MultiByteToTCHAR(dependencies[dependencyIdx].assetPath);
				Edit_SetText(hCurEditPopup, pText.get());
				return;
			}
			break;
		case 2: //Dependency target
			{
				ComboBox_AddString(hCurEditPopup, TEXT("None"));
				size_t iComboBoxItem = 1;
				size_t iSelComboBoxItem = 0;
				auto &fileEntries = pContext->getMainWindow().getFileEntries();
				for (auto fileIt = fileEntries.begin(); fileIt != fileEntries.end(); ++fileIt)
				{
					//Do not list the current .assets file in the combo box. However, circular references are allowed.
					if (fileIt->pContextInfo != pCurFileEntry->pContextInfo &&
						fileIt->pContextInfo && 
						fileIt->pContextInfo->getFileContext() && 
						fileIt->pContextInfo->getFileContext()->getType() == FileContext_Assets)
					{
						if (fileIt->pContextInfo->getFileID() == references[dependencyIdx])
							iSelComboBoxItem = iComboBoxItem; //Mark the currently set dependency as the selection of the combo box.
						char referenceFileIDTmp[32];
						sprintf_s(referenceFileIDTmp, "%u - ", fileIt->pContextInfo->getFileID());
						std::string targetName8 = std::string(referenceFileIDTmp) + fileIt->pContextInfo->getFileName();
						auto upTargetNameT = unique_MultiByteToTCHAR(targetName8.c_str());
						ComboBox_AddString(hCurEditPopup, upTargetNameT.get());
						iComboBoxItem++;
					}
				}
				ComboBox_SetCurSel(hCurEditPopup, iSelComboBoxItem);
				return;
			}
			break;
		default:
			assert(false);
	}
	//Failure
	doCloseEditPopup(false);
}


INT_PTR CALLBACK AssetDependDialog::AssetDependProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret = (INT_PTR)FALSE;
	AssetDependDialog *pThis = (AssetDependDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	switch (message)
	{
	case WM_DESTROY:
		break;
	case WM_NCDESTROY:
		break;
	case WM_CLOSE:
		if (pThis)
			pThis->hDialog = NULL;
		DestroyWindow(hDlg);
		ret = (INT_PTR)TRUE;
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (AssetDependDialog*)lParam;

			//List control columns : Rel File ID; Dependency path; Chosen target file ID (for UABE)
			HWND hList = GetDlgItem(hDlg, IDC_DEPENDLIST);
			//Subclass the ListView to support item edit popups (via double click).
			SetWindowSubclass(hList, AssetDependListViewProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
			ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
			LVCOLUMN column;
			ZeroMemory(&column, sizeof(LVCOLUMN));
			column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			column.cx = 70;
			column.pszText = const_cast<TCHAR*>(TEXT("Rel. File ID"));
			column.iSubItem = 0;
			ListView_InsertColumn(hList, 0, &column);
			column.cx = 250;
			column.pszText = const_cast<TCHAR*>(TEXT("File Path"));
			//column.iSubItem = 1;
			ListView_InsertColumn(hList, 1, &column);
			column.cx = 250;
			column.pszText = const_cast<TCHAR*>(TEXT("Resolved to"));
			//column.iSubItem = 2;
			ListView_InsertColumn(hList, 2, &column);

			ShowWindow(hDlg, SW_SHOW);
			PostMessage(hDlg, WM_SIZE, 0, 0);
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_SIZE:
		onResize(hDlg);
		break;
	}
	return ret;
}
LRESULT CALLBACK AssetDependDialog::AssetDependListViewProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	AssetDependDialog *pThis = (AssetDependDialog*)dwRefData;
	switch (message)
	{
	case WM_LBUTTONDBLCLK:
		{
			LVHITTESTINFO hitTestInfo = {};
			hitTestInfo.pt.x = GET_X_LPARAM(lParam);
			hitTestInfo.pt.y = GET_Y_LPARAM(lParam);
			if (ListView_SubItemHitTest(hWnd, &hitTestInfo) != -1 && hitTestInfo.iSubItem >= 1)
			{
				if (pThis->hCurEditPopup != NULL)
					pThis->doCloseEditPopup();
				pThis->iEditPopupItem = hitTestInfo.iItem;
				pThis->iEditPopupSubItem = hitTestInfo.iSubItem;
				
				RECT targetRect = {};
				ListView_GetSubItemRect(hWnd, hitTestInfo.iItem, hitTestInfo.iSubItem, LVIR_BOUNDS, &targetRect);
				if (hitTestInfo.iSubItem == 2)
				{
					pThis->hCurEditPopup = 
						CreateWindow(WC_COMBOBOX, TEXT(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_CHILD | WS_VISIBLE, 
							targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
							hWnd, GetMenu(hWnd), pThis->pContext->getMainWindow().getHInstance(), NULL);
				}
				else
				{
					pThis->hCurEditPopup = 
						CreateWindow(WC_EDIT, TEXT(""), ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE, 
							targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
							hWnd, GetMenu(hWnd), pThis->pContext->getMainWindow().getHInstance(), NULL);
					SendMessage(pThis->hCurEditPopup, EM_SETLIMITTEXT, sizeof(((AssetsFileDependency*)nullptr)->assetPath) / sizeof(char) - 1, 0);
				}
				SetWindowSubclass(pThis->hCurEditPopup, EditPopupProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
				SendMessage(pThis->hCurEditPopup, WM_SETFONT, (WPARAM)(HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0), FALSE);
				SetFocus(pThis->hCurEditPopup);
				pThis->onOpenEditPopup();
			}
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, AssetDependListViewProc, uIdSubclass);
		break;
	}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}
LRESULT CALLBACK AssetDependDialog::EditPopupProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	AssetDependDialog *pThis = (AssetDependDialog*)dwRefData;
	switch (message)
	{
	case WM_KILLFOCUS:
		//if (wParam == WA_INACTIVE)
		{
			if (pThis->hCurEditPopup != NULL)
				pThis->doCloseEditPopup();
		}
		break;
	case WM_KEYDOWN:
		if (LOWORD(wParam) == VK_ESCAPE || (pThis->iEditPopupSubItem != 2 && LOWORD(wParam) == VK_RETURN))
		{
			if (pThis->hCurEditPopup != NULL)
				pThis->doCloseEditPopup();
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, EditPopupProc, uIdSubclass);
		break;
	}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

void AssetDependDialog::onUpdateDependencies(AssetsFileContextInfo * pContextInfo, size_t from, size_t to)
{
	if (this->hDialog == NULL || this->pCurFileEntry == nullptr || pContextInfo != this->pCurFileEntry->getContextInfoPtr()
		|| pContextInfo == nullptr || from > UINT32_MAX)
		return;
	HWND hList = GetDlgItem(hDialog, IDC_DEPENDLIST);
	auto refLock = pContextInfo->lockReferencesRead();
	const std::vector<unsigned int> &references = pContextInfo->getReferencesRead(refLock);
	const std::vector<AssetsFileDependency> &dependencies = pContextInfo->getDependenciesRead(refLock);

	int nListItems = ListView_GetItemCount(hList);
	
	//Retrieve a list of all dependencies in range that are already in the list.
	int iFirstAffectedItem = -1;
	std::vector<size_t> dependenciesInRange;
	for (int i = 0; i < nListItems; i++)
	{
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)-1;
		item.iItem = i;
		item.iSubItem = 0;
		ListView_GetItem(hList, &item);
		if ((size_t)item.lParam > to)
			break;
		else if ((size_t)item.lParam >= from)
		{
			dependenciesInRange.push_back(item.lParam);
			if (iFirstAffectedItem == -1)
				iFirstAffectedItem = i;
		}
	}
	bool requiresListRebuild = false;
	{
		//Check whether the dependencies that would now be put into the list already existed before.
		//If not, a rebuild of the list will be required.
		size_t iChangedItem = 0;
		for (size_t i = from; i <= to && i < dependencies.size(); i++)
		{
			const AssetsFileDependency &dependency = dependencies[i];
			if (dependency.type != 0)
				continue;
			if (iChangedItem >= dependenciesInRange.size()
				|| dependenciesInRange[iChangedItem++] != i)
			{
				requiresListRebuild = true;
				break;
			}
		}
		if (iChangedItem != dependenciesInRange.size())
			requiresListRebuild = true;
	}
	if (this->hCurEditPopup != NULL)
	{
		//Retrieve the dependency index for the edit popup.
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)-1;
		item.iItem = iEditPopupItem;
		item.iSubItem = 0;
		ListView_GetItem(hList, &item);

		size_t dependencyIdx = item.lParam;
		if (dependencyIdx >= from && (requiresListRebuild || dependencyIdx <= to))
		{
			//Reset the popup since the contents to be edited may have changed (if !requiresListRebuild)
			// or the list indices have shuffled (if requiresListRebuild).
			doCloseEditPopup(false);
		}
	}
	if (requiresListRebuild)
	{
		//Regenerate the whole list for simplicity.
		onUpdateCurrentFile();
	}
	else
	{
		//Update the asset paths and selected targets only, since no dependencies were added/removed.
		//This is essential so the UI keeps being usable while dependencies are loading.
		int iItem = iFirstAffectedItem;
		for (DWORD i = static_cast<DWORD>(from); i <= to && i < dependencies.size(); i++)
		{
			const AssetsFileDependency &dependency = dependencies[i];
			if (dependency.type != 0)
				continue;

			list_updateAssetPath(hList, iItem, &dependency);
			
			list_updateTargetAsset(hList, iItem, references[i]);

			assert(iItem < INT_MAX);
			iItem++;
		}			
	}
}

void AssetDependDialog::onUpdateContainers(AssetsFileContextInfo *pFile) {}
void AssetDependDialog::onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved) {}