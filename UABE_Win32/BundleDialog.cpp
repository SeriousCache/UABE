#include "stdafx.h"
#include "BundleDialog.h"
#include "MainWindow2.h"
#include "resource.h"
#include "../libStringConverter/convert.h"
#include "FileDialog.h"
#include <WindowsX.h>

BundleDialog::~BundleDialog()
{
	pContext->getMainWindow().unregisterEventHandler(eventHandlerHandle);
}
BundleDialog::BundleDialog(class Win32AppContext *pContext, HWND hParentWnd)
	: pContext(pContext), hParentWnd(hParentWnd), hDialog(NULL),
	hCurEditPopup(NULL), iEditPopupItem(0), iEditPopupSubItem(0), nSelected(0),
	pCurFileEntry(nullptr)
{
	eventHandlerHandle = pContext->getMainWindow().registerEventHandler(this);
}

void BundleDialog::addFileContext(const std::pair<FileEntryUIInfo*,uintptr_t> &fileContext)
{
	FileContextInfo *pContextInfo = fileContext.first->getContextInfoPtr();
	if (BundleFileContextInfo *pBundleInfo = dynamic_cast<BundleFileContextInfo*>(pContextInfo))
	{
		unsigned int fileID = pBundleInfo->getFileID();
		auto entryIt = fileEntries.find(fileID);
		assert(entryIt == fileEntries.end());
		if (entryIt == fileEntries.end())
		{
			fileEntries.insert(std::make_pair(fileID, fileContext.first));
			if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr
				|| pBundleInfo->getFileID() < pCurFileEntry->getContextInfoPtr()->getFileID())
			{
				pCurFileEntry = fileContext.first;
				onUpdateCurrentFile();
			}
		}
	}

}
void BundleDialog::removeFileContext(FileEntryUIInfo *pContext)
{
	FileContextInfo *pContextInfo = pContext->getContextInfoPtr();
	if (BundleFileContextInfo *pBundleInfo = dynamic_cast<BundleFileContextInfo*>(pContextInfo))
	{
		unsigned int fileID = pBundleInfo->getFileID();
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
EFileManipulateDialogType BundleDialog::getType()
{
	return FileManipulateDialog_Bundle;
}
HWND BundleDialog::getWindowHandle()
{
	return hDialog;
}
void BundleDialog::onHotkey(ULONG message, DWORD keyCode) //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
{

}
bool BundleDialog::onCommand(WPARAM wParam, LPARAM lParam) //Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
{
	return false;
}
void BundleDialog::onShow()
{
	if (!this->hDialog)
	{
		this->hDialog = CreateDialogParam(pContext->getMainWindow().getHInstance(), MAKEINTRESOURCE(IDD_BUNDLEEDIT), hParentWnd, BundleDlgProc, (LPARAM)this);
		onUpdateCurrentFile();
	}
}
void BundleDialog::onHide()
{
	if (this->hDialog)
	{
		if (this->hCurEditPopup != NULL)
			doCloseEditPopup();
		SendMessage(this->hDialog, WM_CLOSE, 0, 0);
	}
}
bool BundleDialog::hasUnappliedChanges(bool *applyable)
{
	return false;
}
bool BundleDialog::applyChanges()
{
	return true;
}
bool BundleDialog::doesPreferNoAutoclose()
{
	return false;
}

static size_t getSelectedEntryIdx(HWND hList)
{
	int listItem = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
	if (listItem == -1)
		return (size_t)-1;
		
	LVITEM item;
	item.mask = LVIF_PARAM;
	item.lParam = (LPARAM)-1;
	item.iItem = listItem;
	item.iSubItem = 0;
	ListView_GetItem(hList, &item);

	return (size_t)item.lParam;
}
void BundleDialog::importItem(bool addNew)
{
	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
		return;
	std::shared_ptr<BundleFileContextInfo> pBundleInfo(
		pCurFileEntry->getContextInfo(),
		static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr()));

	size_t entryIdx = (size_t)-1;
	if (!addNew)
	{
		entryIdx = getSelectedEntryIdx(hList);
		assert(entryIdx == (size_t)-1 || entryIdx < pBundleInfo->getEntryCount());
		if (entryIdx >= pBundleInfo->getEntryCount() || entryIdx > UINT_MAX)
			return;
	}

	WCHAR *filePathBuf = nullptr;
	if (FAILED(ShowFileOpenDialog(this->pContext->getMainWindow().getWindow(), 
		&filePathBuf, L"*.*|All types:", nullptr, nullptr, TEXT("Import a bundle entry"),
		UABE_FILEDIALOG_FILE_GUID)))
		return;

	if (!addNew)
	{
		std::vector<unsigned int> fileIDByEntry;
		pBundleInfo->getChildFileIDs(fileIDByEntry);
		assert(fileIDByEntry.size() > entryIdx);
		if (fileIDByEntry[entryIdx] != 0 && !pContext->getMainWindow().CloseFile(fileIDByEntry[entryIdx]))
			return;
	}

	std::string fileName;
	for (size_t _i = wcslen(filePathBuf); _i > 0; --_i)
	{
		size_t i = _i - 1;
		if (filePathBuf[i] == L'/' || filePathBuf[i] == L'\\')
		{
			auto pFileName8 = unique_TCHARToMultiByte(&filePathBuf[i+1]);
			fileName.assign(pFileName8.get());
			break;
		}
	}

	IAssetsReader *pReader = Create_AssetsReaderFromFile(filePathBuf, true, RWOpenFlags_Immediately);
	FreeCOMFilePathBuf(&filePathBuf);
	if (!pReader)
	{
		MessageBox(this->pContext->getMainWindow().getWindow(),
			TEXT("Unable to open the file to import!"),
			TEXT("Asset Bundle Extractor"),
			MB_ICONERROR);
		return;
	}
	std::shared_ptr<IAssetsReader> pReader_shared(pReader, Free_AssetsReader);
	bool isSerializedData = false;
	{
		std::unique_ptr<AssetsFile> pAssetsFile(new AssetsFile(pReader_shared.get()));
		isSerializedData = pAssetsFile->VerifyAssetsFile();
	}

	if (addNew)
	{
		if (fileName.empty())
			fileName = std::string("New entry (") + std::to_string(pBundleInfo->getEntryCount()) + ")";
		entryIdx = pBundleInfo->addEntry(*this->pContext, pReader_shared, isSerializedData, fileName);
	}
	else
		pBundleInfo->overrideEntryReader(*this->pContext, entryIdx, pReader_shared, isSerializedData);

	pContext->getMainWindow().loadBundleEntry(pBundleInfo, (unsigned int)entryIdx);
}
void BundleDialog::exportItem()
{
	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
		return;
	BundleFileContextInfo *pBundleInfo = static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr());

	size_t entryIdx = getSelectedEntryIdx(hList);
	assert(entryIdx == (size_t)-1 || entryIdx < pBundleInfo->getEntryCount());
	if (entryIdx >= pBundleInfo->getEntryCount())
		return;

	std::string newName = pBundleInfo->getNewEntryName(entryIdx);
	auto pNewNameT = unique_MultiByteToTCHAR(newName.c_str());
	WCHAR *filePathBuf = nullptr;
	if (FAILED(ShowFileSaveDialog(this->pContext->getMainWindow().getWindow(),
		&filePathBuf, L"*.*|All types:", nullptr, pNewNameT.get(), TEXT("Export a bundle entry"),
		UABE_FILEDIALOG_FILE_GUID)))
		return;

	//Note: Race condition possible (entries could be changed by some other thread).
	//-> Reader could change after having retrieved the name.
	//  (doesn't appear to be relevant, and will not cause too bad behavior).

	bool isModified;
	std::shared_ptr<IAssetsReader> pReader = pBundleInfo->makeEntryReader(entryIdx, isModified);
	QWORD size = 0;
	if (pReader == nullptr || !pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0))
	{
		MessageBox(this->pContext->getMainWindow().getWindow(),
			TEXT("Unable to read the entry to export!"),
			TEXT("Asset Bundle Extractor"),
			MB_ICONERROR);
		return;
	}

	//Slight abuse of BundleReplacers to copy from a reader to a writer. Should be fine, though.
	BundleReplacer *pReplacer = MakeBundleEntryModifier("", "", false, pReader, size, 0);
	bool result = pReplacer->Init(nullptr, nullptr, 0, 0, nullptr);
	if (!result)
	{
		MessageBox(this->pContext->getMainWindow().getWindow(),
			TEXT("An internal error occured."),
			TEXT("Asset Bundle Extractor"),
			MB_ICONERROR);
		return;
	}

	IAssetsWriter *pWriter = Create_AssetsWriterToFile(filePathBuf, true, true, RWOpenFlags_Immediately);
	if (pWriter == nullptr)
	{
		FreeBundleReplacer(pReplacer);
		MessageBox(this->pContext->getMainWindow().getWindow(),
			TEXT("Unable to open the output file!"),
			TEXT("Asset Bundle Extractor"),
			MB_ICONERROR);
		return;
	}
	
	QWORD written = pReplacer->Write(0, pWriter);
	FreeBundleReplacer(pReplacer);
	Free_AssetsWriter(pWriter);
	assert(written <= size);
	if (written < size)
	{
		MessageBox(this->pContext->getMainWindow().getWindow(),
			TEXT("Failed writing the output file!"),
			TEXT("Asset Bundle Extractor"),
			MB_ICONERROR);
		return;
	}
}
void BundleDialog::removeItem()
{
	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
		return;
	std::shared_ptr<BundleFileContextInfo> pBundleInfo(
		pCurFileEntry->getContextInfo(),
		static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr()));

	size_t entryIdx = getSelectedEntryIdx(hList);
	assert(entryIdx == (size_t)-1 || entryIdx < pBundleInfo->getEntryCount());
	if (entryIdx >= pBundleInfo->getEntryCount())
		return;

	std::vector<unsigned int> fileIDByEntry;
	pBundleInfo->getChildFileIDs(fileIDByEntry);
	if (fileIDByEntry[entryIdx] != 0 && !pContext->getMainWindow().CloseFile(fileIDByEntry[entryIdx]))
		return;

	pBundleInfo->removeEntry(*this->pContext, entryIdx);
}

static void list_updateBundleName(HWND hList, int iItem, const char *name)
{
	auto upText = unique_MultiByteToTCHAR(name);
	LVITEM item;
	item.mask = LVIF_TEXT;
	item.pszText = upText.get();
	item.iItem = iItem;
	item.iSubItem = 0;
	ListView_SetItem(hList, &item);
}
static void list_updateBundleModified(HWND hList, int iItem, bool modified)
{
	LVITEM item;
	item.mask = LVIF_TEXT;
	item.pszText = const_cast<TCHAR*>(modified ? TEXT("*") : TEXT(""));
	item.iItem = iItem;
	item.iSubItem = 1;
	ListView_SetItem(hList, &item);
}

void BundleDialog::onUpdateCurrentFile()
{
	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	ListView_DeleteAllItems(hList);
	if (pCurFileEntry != nullptr && pCurFileEntry->getContextInfoPtr() != nullptr && pCurFileEntry->getContextInfoPtr()->getFileContext() != nullptr)
	{
		BundleFileContextInfo *pBundleInfo = static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr());
		//Note: Race condition possible (entries could be changed by some other thread).
		//-> Index bounds are checked in the functions, and can only increase.
		size_t numEntries = pBundleInfo->getEntryCount();
		int listViewCount = 0;
		for (size_t i = 0; i < numEntries && i < INT_MAX; ++i)
		{
			bool hasChanged = pBundleInfo->entryHasChanged(i);
			std::string newName = pBundleInfo->getNewEntryName(i);
			if (!pBundleInfo->entryIsRemoved(i))
			{
				LVITEM item;
				item.mask = LVIF_PARAM;
				item.lParam = (LPARAM)i;
				item.iItem = listViewCount;
				item.iSubItem = 0;
				ListView_InsertItem(hList, &item);

				list_updateBundleName(hList, listViewCount, newName.c_str());
				list_updateBundleModified(hList, listViewCount, hasChanged);

				assert(listViewCount < INT_MAX);
				listViewCount++;
			}
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
	HDWP deferCtx = defer ? BeginDeferWindowPos(12) : NULL;
	bool retry = false;

	RECT client = {};
	GetClientRect(hDlg, &client);
	int clientWidth = (int)(client.right-client.left);
	int clientHeight = (int)(client.bottom-client.top);
	int x = 19;
	int w = clientWidth - 16;

	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_STATICTITLE),     x + 2,       10,                w - 4,    15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_ENTRYLIST),  x,           30,                w - 15,   clientHeight - 60 - 7);
	int btnDistance = std::max<int>(4, w - 25) / 4;
	int btnWidth = std::max<int>(1, btnDistance - 10);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNADD),     x + 5 + 0*btnDistance, clientHeight - 30, btnWidth, 25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNIMPORT),  x + 5 + 1*btnDistance, clientHeight - 30, btnWidth, 25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNEXPORT),  x + 5 + 2*btnDistance, clientHeight - 30, btnWidth, 25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_BTNREMOVE),  x + 5 + 3*btnDistance, clientHeight - 30, btnWidth, 25);

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

void BundleDialog::doCloseEditPopup(bool applyChanges)
{
	if (applyChanges)
	{
		if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
		{
			doCloseEditPopup(false);
			return;
		}
		BundleFileContextInfo *pBundleFileInfo = static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr());

		HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)-1;
		item.iItem = iEditPopupItem;
		item.iSubItem = 0;
		ListView_GetItem(hList, &item);

		size_t entryIdx = (size_t)item.lParam;
		assert(entryIdx < pBundleFileInfo->getEntryCount());
		if (entryIdx >= pBundleFileInfo->getEntryCount())
		{
			doCloseEditPopup(false);
			return;
		}
		
		switch (iEditPopupSubItem)
		{
			case 0: //Entry name
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
						pBundleFileInfo->renameEntry(*this->pContext, entryIdx, std::string(name8.get()));
					}
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

void BundleDialog::onOpenEditPopup()
{
	if (pCurFileEntry == nullptr || pCurFileEntry->getContextInfoPtr() == nullptr)
	{
		doCloseEditPopup(false);
		return;
	}
	BundleFileContextInfo *pBundleInfo = static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr());

	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	LVITEM item;
	item.mask = LVIF_PARAM;
	item.lParam = (LPARAM)-1;
	item.iItem = iEditPopupItem;
	item.iSubItem = 0;
	ListView_GetItem(hList, &item);
	
	size_t entryIdx = (size_t)item.lParam;
	assert(entryIdx < pBundleInfo->getEntryCount());
	if (entryIdx >= pBundleInfo->getEntryCount())
	{
		doCloseEditPopup(false);
		return;
	}

	switch (iEditPopupSubItem)
	{
		case 0: //Dependency name/text
			{
				std::string name = pBundleInfo->getNewEntryName(entryIdx);
				auto pText = unique_MultiByteToTCHAR(name.c_str());
				Edit_SetText(hCurEditPopup, pText.get());
				return;
			}
			break;
		default:
			assert(false);
	}
	//Failure
	doCloseEditPopup(false);
}

void BundleDialog::onChangeSelection()
{
	EnableWindow(GetDlgItem(hDialog, IDC_BTNIMPORT), (this->nSelected > 0) ? TRUE : FALSE);
	EnableWindow(GetDlgItem(hDialog, IDC_BTNEXPORT), (this->nSelected > 0) ? TRUE : FALSE);
	EnableWindow(GetDlgItem(hDialog, IDC_BTNREMOVE), (this->nSelected > 0) ? TRUE : FALSE);
}

INT_PTR CALLBACK BundleDialog::BundleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret = (INT_PTR)FALSE;
	BundleDialog *pThis = (BundleDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
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
			pThis = (BundleDialog*)lParam;

			pThis->nSelected = 0;
			pThis->onChangeSelection();

			HWND hList = GetDlgItem(hDlg, IDC_ENTRYLIST);
			//Subclass the ListView to support item edit popups (via double click).
			SetWindowSubclass(hList, BundleListViewProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
			ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
			LVCOLUMN column;
			ZeroMemory(&column, sizeof(LVCOLUMN));
			column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			column.cx = 250;
			column.pszText = const_cast<TCHAR*>(TEXT("File Name"));
			column.iSubItem = 0;
			ListView_InsertColumn(hList, 0, &column);
			column.cx = 85;
			column.pszText = const_cast<TCHAR*>(TEXT("Has changed"));
			//column.iSubItem = 1;
			ListView_InsertColumn(hList, 1, &column);

			ShowWindow(hDlg, SW_SHOW);
			PostMessage(hDlg, WM_SIZE, 0, 0);
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_SIZE:
		onResize(hDlg);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BTNADD:
			if (pThis) pThis->importItem(true);
			ret = (INT_PTR)TRUE;
			break;
		case IDC_BTNIMPORT:
			if (pThis) pThis->importItem(false);
			ret = (INT_PTR)TRUE;
			break;
		case IDC_BTNEXPORT:
			if (pThis) pThis->exportItem();
			ret = (INT_PTR)TRUE;
			break;
		case IDC_BTNREMOVE:
			if (pThis) pThis->removeItem();
			ret = (INT_PTR)TRUE;
			break;
		}
		break;
	case WM_NOTIFY:
		{
			HWND hList = GetDlgItem(hDlg, IDC_ENTRYLIST);
			NMLISTVIEW *pNotifyLV = (NMLISTVIEW*)lParam;
			if (pNotifyLV->hdr.hwndFrom != hList)
				break;
			switch (pNotifyLV->hdr.code)
			{
				case LVN_ITEMCHANGED:
					{
						NMLISTVIEW *pInfo = (NMLISTVIEW*)lParam;
						if (pThis)
						{
							if ((pInfo->uOldState ^ pInfo->uNewState) & LVIS_SELECTED)
							{
								bool isSelected = (pInfo->uNewState & LVIS_SELECTED) ? true : false;
								int iItem = pInfo->iItem;
								if (iItem == -1)
								{
									pThis->nSelected = (isSelected) ? ListView_GetItemCount(hList) : 0;
								}
								else
								{
									if (isSelected)
										pThis->nSelected++;
									else
										pThis->nSelected--;
								}
							}
						}
					}
					pThis->onChangeSelection();
					break;
			}
		}
		break;
	}
	return ret;
}
LRESULT CALLBACK BundleDialog::BundleListViewProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	BundleDialog *pThis = (BundleDialog*)dwRefData;
	switch (message)
	{
	case WM_LBUTTONDBLCLK:
		{
			LVHITTESTINFO hitTestInfo = {};
			hitTestInfo.pt.x = GET_X_LPARAM(lParam);
			hitTestInfo.pt.y = GET_Y_LPARAM(lParam);
			if (ListView_SubItemHitTest(hWnd, &hitTestInfo) != -1 && hitTestInfo.iSubItem == 0)
			{
				if (pThis->hCurEditPopup != NULL)
					pThis->doCloseEditPopup();
				pThis->iEditPopupItem = hitTestInfo.iItem;
				pThis->iEditPopupSubItem = hitTestInfo.iSubItem;
				
				RECT targetRect = {};
				ListView_GetSubItemRect(hWnd, hitTestInfo.iItem, hitTestInfo.iSubItem, LVIR_BOUNDS, &targetRect);
				if (hitTestInfo.iSubItem == 0)
				{
					pThis->hCurEditPopup = 
						CreateWindow(WC_EDIT, TEXT(""), ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE, 
							targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
							hWnd, GetMenu(hWnd), pThis->pContext->getMainWindow().getHInstance(), NULL);
				}
				SetWindowSubclass(pThis->hCurEditPopup, EditPopupProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
				SendMessage(pThis->hCurEditPopup, WM_SETFONT, (WPARAM)(HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0), FALSE);
				SetFocus(pThis->hCurEditPopup);
				pThis->onOpenEditPopup();
			}
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, BundleListViewProc, uIdSubclass);
		break;
	}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}
LRESULT CALLBACK BundleDialog::EditPopupProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	BundleDialog *pThis = (BundleDialog*)dwRefData;
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
		if (LOWORD(wParam) == VK_ESCAPE || (pThis->iEditPopupSubItem != 0 && LOWORD(wParam) == VK_RETURN))
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

void BundleDialog::onUpdateBundleEntry(BundleFileContextInfo *info, size_t index)
{
	if (this->hDialog == NULL || this->pCurFileEntry == nullptr || info != this->pCurFileEntry->getContextInfoPtr()
		|| info == nullptr || info->getFileContext() == nullptr)
		return;
	HWND hList = GetDlgItem(hDialog, IDC_ENTRYLIST);
	BundleFileContextInfo *pBundleInfo = static_cast<BundleFileContextInfo*>(pCurFileEntry->getContextInfoPtr());

	int nListItems = ListView_GetItemCount(hList);
	
	//Retrieve a list of all dependencies in range that are already in the list.
	int iAffectedItem = -1;
	for (int i = 0; i < nListItems; i++)
	{
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)-1;
		item.iItem = i;
		item.iSubItem = 0;
		ListView_GetItem(hList, &item);
		if ((size_t)item.lParam == index)
		{
			iAffectedItem = i;
			break;
		}
	}
	bool hasChanged = pBundleInfo->entryHasChanged(index);
	std::string newName = pBundleInfo->getNewEntryName(index);
	if (pBundleInfo->entryIsRemoved(index))
	{
		if (iAffectedItem != -1)
		{
			if (hCurEditPopup != NULL && iEditPopupItem == iAffectedItem)
				doCloseEditPopup(false);
			ListView_DeleteItem(hList, iAffectedItem);
		}
	}
	else if (iAffectedItem == -1)
	{
		//Add new element
		LVITEM item;
		item.mask = LVIF_PARAM;
		item.lParam = (LPARAM)index;
		item.iItem = nListItems;
		item.iSubItem = 0;
		ListView_InsertItem(hList, &item);

		list_updateBundleName(hList, nListItems, newName.c_str());
		list_updateBundleModified(hList, nListItems, hasChanged);
	}
	else
	{
		//Change existing element
		if (hCurEditPopup != NULL && iEditPopupItem == iAffectedItem)
			doCloseEditPopup(false);
		list_updateBundleName(hList, iAffectedItem, newName.c_str());
		list_updateBundleModified(hList, iAffectedItem, hasChanged);
	}
}
