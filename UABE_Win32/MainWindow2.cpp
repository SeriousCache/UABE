#include "stdafx.h"
#include "resource.h"
#include "MainWindow2.h"
#include "Win32AppContext.h"
#include "AssetListDialog.h"
#include "AssetDependDialog.h"
#include "BundleDialog.h"
#include "FileDialog.h"
#include "TypeDatabaseEditor.h"
#include "TypeDbPackageEditor.h"
#include "MonoBehaviourManager.h"
#include "ModInstallerEditor2.h"
#include "ModPackageLoader.h"
#include "AddAssetDialog.h"
#include "Win32TaskStatusTracker.h"
#include "../libStringConverter/convert.h"

#include <string>
#include <assert.h>
#include <Psapi.h>
#include <mctrl.h>
#include <WindowsX.h>

IFileManipulateDialogFactory::IFileManipulateDialogFactory()
{}
IFileManipulateDialogFactory::~IFileManipulateDialogFactory()
{}

DefaultFileDialogFactory::DefaultFileDialogFactory(class Win32AppContext *pContext)
	: pContext(pContext)//, pAssetListDialog(nullptr)
{}

DefaultFileDialogFactory::~DefaultFileDialogFactory()
{
}
std::shared_ptr<IFileManipulateDialog> DefaultFileDialogFactory::construct(EFileManipulateDialogType type, HWND hParent)
{
	switch (type)
	{
	case FileManipulateDialog_AssetList:
		return std::make_shared<AssetListDialog>(pContext, hParent);
	case FileManipulateDialog_AssetsDependencies:
		return std::make_shared<AssetDependDialog>(pContext, hParent);
	case FileManipulateDialog_Bundle:
		return std::make_shared<BundleDialog>(pContext, hParent);
	default:
		return nullptr;
	}
}


FileManipulateDialogInfo::FileManipulateDialogInfo()
	: pEntry(nullptr), hTreeItem(NULL), param(0)
{
	b_isFileManipulateDialogInfo = true;
}
FileManipulateDialogInfo::~FileManipulateDialogInfo()
{
}

FileEntryUIInfo::FileEntryUIInfo(MC_HTREELISTITEM hTreeItem, const std::string &fullName, bool isFilePath) :
		failed(false), pending(true), pContextInfo(nullptr), shortNameIndex(0),
		hTreeItem(hTreeItem), standardDialogsCount(0)
{
	b_isFileManipulateDialogInfo = false;
	this->fullName.assign(fullName);
	if (isFilePath)
	{
		const char *fullNameC = this->fullName.c_str();
		for (shortNameIndex = fullName.size(); shortNameIndex > 0; shortNameIndex--)
		{
			if (fullNameC[shortNameIndex-1] == '/' || fullNameC[shortNameIndex-1] == '\\')
				break;
		}
	}
}
FileEntryUIInfo::~FileEntryUIInfo()
{
	//Note that the destructor is also called during emplace_back, i.e. after a move constructor call.
	if (!pending)
		setContextInfo(std::shared_ptr<FileContextInfo>(nullptr));
}

UIDisposableCache::UIDisposableCache()
	: nRefs(0)
{}
UIDisposableCache::~UIDisposableCache()
{}
void UIDisposableCache::incRef()
{
	assert(nRefs != UINT_MAX);
	++nRefs;
}
void UIDisposableCache::decRef()
{
	assert(nRefs != 0);
	if (--nRefs == 0)
		delete this;
}


void MainWindowEventHandler::onUpdateContainers(AssetsFileContextInfo *pFile)
{}
void MainWindowEventHandler::onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved)
{}
void MainWindowEventHandler::onUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to)
{}
void MainWindowEventHandler::onUpdateBundleEntry(BundleFileContextInfo *pFile, size_t index)
{}

static const HANDLE uabeDlgProp = (HANDLE)(uintptr_t)(GetCurrentProcessId() | 0x80000000);

MainWindow2::MainWindow2(HINSTANCE hInstance) :
	pContext(nullptr), hDlg(NULL), hMenu(NULL),
	hInstance(hInstance),
	hContainersDlg(NULL), activeManipDlgTab(0), hHotkeyHook(NULL),
	mainPanelSplitter(0.3f, 0.15f, 0.8f),
	fileTreeColumnRatio(0.8f), fileEntryCountersByType(),
	ignoreTreeSelChanges(false), skipDeselectOnTabChange(false),
	decompressTargetDir_cancel(false)
{
}

bool MainWindow2::Initialize()
{
	if (!pDialogFactory)
		pDialogFactory.reset(new DefaultFileDialogFactory(this->pContext));
	WNDCLASSEX windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	if (!GetClassInfoEx(hInstance, TEXT("msctls_progress32_dblclk"), &windowClass)
		&& GetClassInfoEx(NULL, TEXT("msctls_progress32"), &windowClass))
	{
		windowClass.hInstance = hInstance;
		windowClass.lpszMenuName = nullptr;
		windowClass.lpszClassName = TEXT("msctls_progress32_dblclk");
		windowClass.style |= CS_DBLCLKS;
		RegisterClassEx(&windowClass);
	}
	this->hDlg = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc, (LPARAM)this);
	return (this->hDlg != NULL);
}
int MainWindow2::HandleMessages()
{
	MSG msg = {}; 
	//HACCEL hAccelTable;
	//hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ASSETBUNDLEEXTRACTOR));

	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (IsDialogMessage(this->hDlg, &msg))// || TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			continue;
		IFileManipulateDialog *pActiveManipDlg = this->getActiveManipDlg();
		if (pActiveManipDlg && IsDialogMessage(pActiveManipDlg->getWindowHandle(), &msg))
			continue;
		Win32TaskStatusTracker* pStatusTracker = dynamic_cast<Win32TaskStatusTracker*>(this->pStatusTracker.get());
		if (pStatusTracker && pStatusTracker->getDialog() != NULL && IsDialogMessage(pStatusTracker->getDialog(), &msg))
			continue;
		//TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	pStatusTracker.reset();
	return (int)msg.wParam;
}

static MC_HTREELISTITEM insertEntry(HWND hTree, MC_HTREELISTITEM parent, const std::string &name)
{
    MC_TLINSERTSTRUCT insertStruct;
	insertStruct.hParent = parent;
	insertStruct.hInsertAfter = MC_TLI_LAST;
	insertStruct.item.fMask = MC_TLIF_TEXT;
	size_t nameLenT;
	TCHAR *tcText = _MultiByteToTCHAR(name.c_str(), nameLenT);
	insertStruct.item.pszText = tcText;
	insertStruct.item.cchTextMax = (int)(nameLenT + 1);
	
    MC_HTREELISTITEM hItem = (MC_HTREELISTITEM)SendMessage(hTree, MC_TLM_INSERTITEM, 0, (LPARAM)&insertStruct);
	_FreeTCHAR(tcText);
	return hItem;
}
static MC_HTREELISTITEM insertPendingEntry(HWND hTree, MC_HTREELISTITEM parent, const std::string &fileName)
{
	return insertEntry(hTree, parent, "Pending : " + fileName);
}
static void updateEntryInfoRef(HWND hTree, MC_HTREELISTITEM hTreeItem, ITreeParameter &info)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_PARAM;
	item.lParam = (LPARAM)&info;
	
	SendMessage(hTree, MC_TLM_SETITEM, (WPARAM)hTreeItem, (LPARAM)&item);
}
static void updateEntryName(HWND hTree, MC_HTREELISTITEM hTreeItem, const std::string &newName)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_TEXT;
	size_t nameLenT;
	TCHAR *tcText = _MultiByteToTCHAR(newName.c_str(), nameLenT);
	item.pszText = tcText;
	item.cchTextMax = (int)(nameLenT + 1);
	
	SendMessage(hTree, MC_TLM_SETITEM, (WPARAM)hTreeItem, (LPARAM)&item);

	_FreeTCHAR(tcText);
}
static void setEntryFileID(HWND hTree, MC_HTREELISTITEM hTreeItem, unsigned int fileID)
{
	TCHAR fileIDBuf[20]; fileIDBuf[19] = 0;
	_stprintf_s(fileIDBuf, TEXT("%u"), fileID);
	MC_TLSUBITEMW subitemInfo;
	subitemInfo.fMask = MC_TLSIF_TEXT;
	subitemInfo.iSubItem = 1;
	subitemInfo.pszText = fileIDBuf;
	subitemInfo.cchTextMax = sizeof(fileIDBuf) / sizeof(TCHAR);
	SendMessage(hTree, MC_TLM_SETSUBITEM, (WPARAM)hTreeItem, (LPARAM)&subitemInfo);
}
static ITreeParameter *getEntryParam(HWND hTree, MC_HTREELISTITEM hTreeItem)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_PARAM;
	item.lParam = 0;
	if (!SendMessage(hTree, MC_TLM_GETITEM, (WPARAM)hTreeItem, (LPARAM)&item))
		item.lParam = 0;
	return (ITreeParameter*)item.lParam;
}
static bool getEntryParam_TreeItem(ITreeParameter *pTreeParameter, MC_HTREELISTITEM &hTreeItem)
{
	hTreeItem = nullptr;
	if (pTreeParameter)
	{
		if (pTreeParameter->isFileEntryInfo())
			hTreeItem = pTreeParameter->asFileEntryInfo()->hTreeItem;
		else if (pTreeParameter->isFileManipulateDialogInfo())
			hTreeItem = pTreeParameter->asFileManipulateDialogInfo()->hTreeItem;
		else
			return false;
	}
	else
		return false;
	return true;
}
static FileEntryUIInfo *getEntryParam_FileEntryInfo(ITreeParameter *pTreeParameter, bool &isPrimaryDialog)
{
	if (pTreeParameter)
	{
		isPrimaryDialog = false;
		if (pTreeParameter->isFileEntryInfo())
		{
			isPrimaryDialog = true;
			return pTreeParameter->asFileEntryInfo();
		}
		else if (pTreeParameter->isFileManipulateDialogInfo())
		{
			FileManipulateDialogInfo *pInfo = pTreeParameter->asFileManipulateDialogInfo();
			FileEntryUIInfo *pEntry = pInfo->pEntry;
			if (pInfo == &pEntry->standardDialogs[0])
				isPrimaryDialog = true;
			return pEntry;
		}
	}
	else
		isPrimaryDialog = true;
	return nullptr;
}
static void setHasChildren(HWND hTree, MC_HTREELISTITEM hTreeItem, bool hasChildren)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_CHILDREN;
	item.cChildren = hasChildren ? 1 : 0;
	
	SendMessage(hTree, MC_TLM_SETITEM, (WPARAM)hTreeItem, (LPARAM)&item);
}
bool getSelectItem(HWND hTree, MC_HTREELISTITEM hTreeItem)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_STATE;
	item.stateMask = TVIS_SELECTED;
	item.state = 0;
	if (SendMessage(hTree, MC_TLM_GETITEM, (WPARAM)hTreeItem, (LPARAM)&item))
		return ((item.state & TVIS_SELECTED) != 0);
	return false;
}
static void setSelectItem(HWND hTree, MC_HTREELISTITEM hTreeItem, bool select)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_STATE;
	item.stateMask = TVIS_SELECTED;
	item.state = select ? TVIS_SELECTED : 0;
	
	SendMessage(hTree, MC_TLM_SETITEM, (WPARAM)hTreeItem, (LPARAM)&item);
}
inline MC_HTREELISTITEM MCTreeList_GetNextSelection(HWND hTree, MC_HTREELISTITEM curItem = NULL)
{
	return (MC_HTREELISTITEM)SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CARET, (LPARAM)curItem);
}
inline MC_HTREELISTITEM MCTreeList_DeleteItem(HWND hTree, MC_HTREELISTITEM item)
{
	return (MC_HTREELISTITEM)SendMessage(hTree, MC_TLM_DELETEITEM, 0, (LPARAM)item);
}
static void DeleteFileEntry_TreeItems(HWND hTree, FileEntryUIInfo *pEntryInfo)
{
	//Deleting the parent item also deletes the children.
	MCTreeList_DeleteItem(hTree, pEntryInfo->hTreeItem);
}

LRESULT CALLBACK MainWindow2::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    LRESULT nResult = 1;
    if(nCode == HC_ACTION && wParam == PM_REMOVE)
    {
        MSG *p = (MSG*) lParam;
		HWND hForeground = GetForegroundWindow();
		DWORD wndProcessId = 0;
		GetWindowThreadProcessId(hForeground, &wndProcessId);
		if(p->message == WM_KEYDOWN && hForeground != NULL
			&& GetProp(hForeground, TEXT("UABE")) == uabeDlgProp && wndProcessId == GetCurrentProcessId())
		{
			MainWindow2 *pThis = (MainWindow2*)GetWindowLongPtr(hForeground, GWLP_USERDATA);
			if (pThis->getActiveManipDlg() != nullptr)
			{
				PostMessage(pThis->hDlg, WM_APP+1, (DWORD)p->wParam, (LPARAM)p->message);
			}
		}
    }
    if(nCode < 0 || nResult)
        return CallNextHookEx(NULL,nCode,wParam,lParam);
    return nResult;
}
void MainWindow2::OnSelectClassDbDialogFinished()
{
	if (!fileEntriesPendingForDbSelection.empty())
	{
		//Apply the user choice for the first pending file entry.
		DbSelectionQueueEntry entry = this->fileEntriesPendingForDbSelection.front();
		this->fileEntriesPendingForDbSelection.pop_front();
		auto pClassDatabase = this->pSelectClassDbDialog->getClassDatabaseResult_Move();
		if (entry.pEntry != nullptr && pClassDatabase != nullptr)
		{
			AssetsFileContextInfo *pTargetEntry = dynamic_cast<AssetsFileContextInfo*>(entry.pEntry->pContextInfo.get());
			ClassDatabaseFile_sharedptr pClassDatabaseShared(std::move(pClassDatabase));
			if (this->pSelectClassDbDialog->isRememberForVersion())
				this->databaseFilesByEngineVersion[this->pSelectClassDbDialog->getEngineVersion()] = pClassDatabaseShared;
			else
			{
				//Remove old engine version defaults.
				auto databaseIt = this->databaseFilesByEngineVersion.find(this->pSelectClassDbDialog->getEngineVersion());
				if (databaseIt != this->databaseFilesByEngineVersion.end())
					this->databaseFilesByEngineVersion.erase(databaseIt);
			}
			if (this->pSelectClassDbDialog->isRememberForAll())
				this->defaultDatabaseFile = pClassDatabaseShared;
			else
				this->defaultDatabaseFile.reset(); //Remove the general default.
			pTargetEntry->SetClassDatabase(std::move(pClassDatabaseShared));
		}
	}
	this->pSelectClassDbDialog.reset();
	//Take care of the remaining file entries waiting for database selection.
	while (!this->fileEntriesPendingForDbSelection.empty())
	{
		DbSelectionQueueEntry &entry = this->fileEntriesPendingForDbSelection.front();
		if (entry.pEntry == nullptr)
		{
			this->fileEntriesPendingForDbSelection.pop_front();
			continue;
		}
		AssetsFileContextInfo *pContextInfo = dynamic_cast<AssetsFileContextInfo*>(entry.pEntry->pContextInfo.get());
		if (this->TryFindClassDatabase(pContextInfo)) //If new defaults can solve this entry, there is no need to open a dialog.
			this->fileEntriesPendingForDbSelection.pop_front();
		else
		{
			this->OpenClassDatabaseSelection(pContextInfo, entry.reason_DatabaseNotFound);
			break;
		}
	}
}

void MainWindow2::onGCTick()
{
	PROCESS_MEMORY_COUNTERS memCounters = {};
	memCounters.cb = sizeof(PROCESS_MEMORY_COUNTERS);
	size_t memoryToFree = this->pContext->getGCMemoryLimit() / 2;
	assert(memoryToFree > 0);
	if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounters, memCounters.cb)
		&& memCounters.PagefileUsage >= this->pContext->getGCMemoryLimit()
		&& memoryToFree > 0)
	{
		struct ErasableElementDesc
		{
			std::list<UIDisposableCacheRef<>> *pList;
			std::list<UIDisposableCacheRef<>>::iterator it;
			time_t lastUseTime;
		};
		std::vector<ErasableElementDesc> erasableElements;
		unsigned int minAge = this->pContext->getGCMinAge();
		time_t now = time(nullptr);
		for (auto cacheListIt = this->disposableCacheElements.begin(); 
			cacheListIt != this->disposableCacheElements.end(); 
			++cacheListIt)
		{
			for (auto cacheElemIt = cacheListIt->second.begin();
				cacheElemIt != cacheListIt->second.end();
				++cacheElemIt)
			{
				if (!cacheElemIt->get()->isInUse())
				{
					time_t lastUseTime = cacheElemIt->get()->getLastUseTime();
					//Assuming time_t is in seconds since some reference time (which is the case for POSIX and Win32)
					if (lastUseTime < now && now - lastUseTime >= minAge)
					{
						ErasableElementDesc desc = {&cacheListIt->second, cacheElemIt, lastUseTime};
						erasableElements.push_back(std::move(desc));
					}
				}
			}
		}
		struct
		{
			bool operator()(ErasableElementDesc &a, ErasableElementDesc &b) const
			{   
				return a.lastUseTime < b.lastUseTime;
			}   
		} useTimeComparator_Ascending;
		//Sort the erasable elements by their use time, so that the least recently used element is first.
		std::sort(erasableElements.begin(), erasableElements.end(), useTimeComparator_Ascending);
		size_t estimatedFreedMemory = 0;
		for (size_t i = 0; i < erasableElements.size() && estimatedFreedMemory < memoryToFree; i++)
		{
			estimatedFreedMemory += erasableElements[i].it->get()->approxMemory();
			erasableElements[i].pList->erase(erasableElements[i].it);
		}
	}
}
bool askUserApplyChangeBeforeInstaller(HWND hParent);
static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK MainWindow2::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	INT_PTR ret = (INT_PTR)FALSE;
	MainWindow2 *pThis = (MainWindow2*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	if (pThis && pThis->mainPanelSplitter.handleWin32Message(hDlg, message, wParam, lParam))
	{
		if (pThis->mainPanelSplitter.shouldResize())
			pThis->onResize();
		return (message == WM_SETCURSOR) ? (INT_PTR)TRUE : (INT_PTR)0;
	}
	switch (message)
	{
	case WM_APP+0: //New messages available.
		if (pThis && pThis->pContext)
		{
			pThis->pContext->handleMessages();
		}
		ret = (INT_PTR)TRUE;
		break;
	case WM_APP+1:
		if (pThis)
		{
			IFileManipulateDialog *pManipDlg = pThis->getActiveManipDlg();
			if (pManipDlg)
				pManipDlg->onHotkey((ULONG)lParam, (DWORD)wParam);
		}
		ret = (INT_PTR)TRUE;
		break;
	case WM_APP+2: //SelectClassDbDialog finished
		if (pThis && pThis->pSelectClassDbDialog)
		{
			pThis->OnSelectClassDbDialogFinished();
		}
		ret = (INT_PTR)TRUE;
		break;
	case WM_CLOSE:
		{
			if (pThis->onCloseProgramCommand())
			{
				DestroyWindow(hDlg);
				ret = (INT_PTR)TRUE;
			}
		}
		break;
	case WM_DESTROY:
		{
			if (pThis)
			{
				SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
				DestroyMenu(pThis->hMenu);
				KillTimer(hDlg, (uintptr_t)0);
				pThis->hDlg = NULL;
				pThis->hMenu = NULL;

				if (pThis->hContainersDlg != NULL)
				{
					CloseWindow(pThis->hContainersDlg);
					pThis->hContainersDlg = NULL;
				}
				//FreeAssetsInfo(pThis);
				//FreeMonoBehaviourClassDbs();
			}

			PostQuitMessage(0);
		}
		break;
	case WM_NCDESTROY:
		{
			RemoveProp(hDlg, TEXT("UABE"));
			if (pThis)
				UnhookWindowsHookEx(pThis->hHotkeyHook);
		}
		break;
	case WM_TIMER:
		{
			if (wParam == (uintptr_t)0 && pThis)
			{
				pThis->onGCTick();
			}
		}
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			SetProp(hDlg, TEXT("UABE"), uabeDlgProp);
			pThis = (MainWindow2*)lParam;
			pThis->pStatusTracker.reset(new Win32TaskStatusTracker(*pThis->pContext, GetDlgItem(hDlg, IDC_PROGMAIN), GetDlgItem(hDlg, IDC_SPROGDESC)));

			pThis->mainPanelSplitter.setSplitterWindow(GetDlgItem(hDlg, IDC_CONTENTSEPARATE));
			pThis->mainPanelSplitter.handleWin32Message(hDlg, message, wParam, lParam);

			pThis->hHotkeyHook = SetWindowsHookEx(WH_GETMESSAGE, KeyboardHookProc, NULL, GetCurrentThreadId());
			SetWindowSubclass(GetDlgItem(hDlg, IDC_PROGMAIN), ProgSubclassProc, 0, (DWORD_PTR)pThis);
			

			pThis->hMenu = LoadMenu(pThis->hInstance, MAKEINTRESOURCE(IDC_MAINMENU));
			SetMenu(hDlg, pThis->hMenu);
			EnableMenuItem(pThis->hMenu, IDM_FILE_APPLY, MF_GRAYED);
			EnableMenuItem(pThis->hMenu, IDM_FILE_SAVE, MF_GRAYED);
			EnableMenuItem(pThis->hMenu, IDM_FILE_SAVEALL, MF_GRAYED);

			{
				HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
				MC_TLCOLUMN col;
				col.fMask = MC_TLCF_TEXT;
				col.pszText = const_cast<TCHAR*>(_T("Files and Components"));
				col.cchTextMax = (int)(_tcslen(col.pszText) + 1);
				SendMessage(hTree, MC_TLM_INSERTCOLUMN, 0, (LPARAM)&col);
				col.pszText = const_cast<TCHAR*>(_T("File ID"));
				col.cchTextMax = (int)(_tcslen(col.pszText) + 1);
				SendMessage(hTree, MC_TLM_INSERTCOLUMN, 1, (LPARAM)&col);
				//Enable child multiselect
				SendMessage(hTree, MC_TLM_SETCUSTOMSTYLE, (1 << 0), 0);
			}
			{
				HWND hTabsControl = GetDlgItem(hDlg, IDC_MANIPDLGTABS);
				MC_MTITEMWIDTH widths;
				widths.dwDefWidth = 0;
				widths.dwMinWidth = 90;
				SendMessage(hTabsControl, MC_MTM_SETITEMWIDTH, 0, (LPARAM) &widths);
				SendMessage(hTabsControl, MC_MTM_SETCUSTOMSTYLE, (WPARAM) MC_MTCS_OPENBTN, 0);
			}

			pThis->ignoreTreeSelChanges = false;
			pThis->skipDeselectOnTabChange = false;
			
			ShowWindow(hDlg, SW_SHOW);
			PostMessage(hDlg, WM_SIZE, 0, 0);
			//Cache GC timer
			SetTimer(hDlg, (uintptr_t)0, 2000, NULL);
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_SIZE:
		if (pThis)
		{
			pThis->onResize();
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO pMinMax = (LPMINMAXINFO)lParam;
			pMinMax->ptMinTrackSize.x = 400;
			pMinMax->ptMinTrackSize.y = 400;
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_NOTIFY:
		if (pThis && pThis->pContext)
		{
			switch (((NMHDR*)lParam)->code)
			{
			case MC_TLN_SELCHANGED:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_TREEFILES))
				{
					//Style MC_TLS_MULTISELECT makes the hItemOld/hItemNew fields meaningless, 
					//   i.e. we need to iterate over the selection and compare with the old one manually.
					if (!pThis->ignoreTreeSelChanges)
						pThis->onChangeFileSelection();
				}
				break;
			case MC_MTN_OPENITEM:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					pThis->doOpenTab();
					SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
					return (INT_PTR)TRUE; //Prevent tab deletion.
				}
				break;
			case MC_MTN_CLOSEITEM:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					MC_NMMTCLOSEITEM *pNotification = (MC_NMMTCLOSEITEM*)lParam;
					if (!pThis->preDeleteTab(pNotification))
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
					return (INT_PTR)TRUE; //Prevent tab deletion.
				}
				break;
			case MC_MTN_DELETEITEM:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					MC_NMMTDELETEITEM *pNotification = (MC_NMMTDELETEITEM*)lParam;
					pThis->onDeleteTab(pNotification);
				}
				break;
			case MC_MTN_SELCHANGE:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					MC_NMMTSELCHANGE *pNotification = (MC_NMMTSELCHANGE*)lParam;
					pThis->onSwitchTabs(pNotification);
				}
				break;
			case NM_RCLICK:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					//TODO: Handle tab right click.
					//Use MC_MTM_HITTEST to find out which tab is being clicked.
					// -> MC_MTM_HITTEST expects the control client position of the mouse position (=> GetCursorPos(&pos) and ScreenToClient(((NMHDR*)lParam)->hwndFrom, &pos)).
				}
				break;
			case MC_MTN_DELETEALLITEMS:
				if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_MANIPDLGTABS))
				{
					SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
					return (INT_PTR)TRUE;
				}
				break;
			}
		}
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		if (pThis != nullptr && pThis->pContext != nullptr)
		{
			switch (wmId)
			{
			case IDM_VIEW_ADDASSET:
				{
					AddAssetDialog dlg(*pThis->pContext);
					dlg.open();
				}
				break;
			case IDM_FILE_OPEN:
				pThis->onOpenFileCommand();
				break;
			case IDM_FILE_CLOSE:
				pThis->onCloseFileCommand();
				break;
			case IDM_EXIT:
				PostMessage(hDlg, WM_CLOSE, 0, 0);
				break;
			case IDM_FILE_SAVE:
				if (IFileManipulateDialog *pActiveManipDlg = pThis->getActiveManipDlg())
					pActiveManipDlg->onCommand(MAKEWPARAM(IDM_FILE_APPLY,wmEvent), lParam);
				if (ManipDlgDesc *pCurTab = pThis->getActiveManipDlgDesc())
				{
					for (size_t i = 0; i < pCurTab->selection.size(); i++)
					{
						if (pCurTab->selection[i] == nullptr) continue;
						FileEntryUIInfo *pUIInfo = nullptr;
						if (pCurTab->selection[i]->isFileManipulateDialogInfo())
						{
							FileManipulateDialogInfo *pDlgInfo = pCurTab->selection[i]->asFileManipulateDialogInfo();
							pUIInfo = pDlgInfo->pEntry;
						}
						else if (pCurTab->selection[i]->isFileEntryInfo())
							pUIInfo = pCurTab->selection[i]->asFileEntryInfo();
						if (pUIInfo != nullptr)
							pThis->onSaveFileRequest(pUIInfo);
					}
				}
				break;
			case IDM_FILE_SAVEALL:
				{
					for (size_t iTab = 0; iTab < pThis->manipDlgTabs.size(); iTab++)
					{
						if (pThis->manipDlgTabs[iTab].pCurManipDlg)
							pThis->manipDlgTabs[iTab].pCurManipDlg->onCommand(wParam, lParam);
					}
					auto &fileEntryUIList = pThis->getFileEntries();
					for (auto curIt = fileEntryUIList.begin(); curIt != fileEntryUIList.end(); ++curIt)
					{
						pThis->onSaveFileRequest(&*curIt);
					}
				}
				break;
			case IDM_FILE_OPENUABESAVEFILE:
				{
					Win32ModPackageLoader loader(*pThis->pContext);
					loader.open();
				}
				break;
			case IDM_VIEW_PROGRESS:
				if (auto pStatusTracker = dynamic_cast<Win32TaskStatusTracker*>(pThis->pStatusTracker.get()))
					pStatusTracker->open();
				break;
			case IDM_MODMAKER_CREATESTANDALONE:
			case IDM_MODMAKER_CREATEPACKAGE:
				{
					//First ask the user to apply (or not apply) changes to be visible in the installer data.
					bool choseApplyAllChanges = false;
					for (size_t iTab = 0; iTab < pThis->manipDlgTabs.size(); iTab++)
					{
						ManipDlgDesc& desc = pThis->manipDlgTabs[iTab];
						if (desc.pCurManipDlg && desc.pCurManipDlg->hasUnappliedChanges())
						{
							if (!choseApplyAllChanges && !askUserApplyChangeBeforeInstaller(hDlg))
								break;
							choseApplyAllChanges = true;
							desc.pCurManipDlg->applyChanges();
						}
					}
					Win32ModInstallerEditor editor(*pThis->pContext, pThis->pContext->contextInfo, 
						(wmId == IDM_MODMAKER_CREATESTANDALONE) ? ModDataSaveType_Installer : ModDataSaveType_PackageFile);
					editor.open();
				}
				break;
			case IDM_TOOLS_EDITTYPEDATABASE:
				OpenTypeDatabaseEditor(pThis->hInstance, pThis->hDlg);
				break;
			case IDM_TOOLS_EDITTYPEPACKAGE:
				OpenTypeDbPackageEditor(pThis->hInstance, pThis->hDlg);
				break;
			case IDM_TOOLS_GETSCRIPTINFORMATION:
				{
					std::unordered_set<unsigned int> addedFileIDs;
					std::vector<std::shared_ptr<AssetsFileContextInfo>> assetsInfo;
					//Add all selected .assets files and their direct dependencies.
					//-> If a selected .assets file has a MonoBehavior,
					//   it may have a reference to another .assets file with the corresponding MonoScript.
					if (ManipDlgDesc *pCurTab = pThis->getActiveManipDlgDesc())
					{
						for (size_t i = 0; i < pCurTab->selection.size(); i++)
						{
							if (pCurTab->selection[i] == nullptr) continue;
							FileEntryUIInfo *pUIInfo = nullptr;
							if (pCurTab->selection[i]->isFileManipulateDialogInfo())
							{
								FileManipulateDialogInfo *pDlgInfo = pCurTab->selection[i]->asFileManipulateDialogInfo();
								pUIInfo = pDlgInfo->pEntry;
							}
							else if (pCurTab->selection[i]->isFileEntryInfo())
								pUIInfo = pCurTab->selection[i]->asFileEntryInfo();
							//Retrieve the context info, and try to convert it to an AssetsFileContextInfo.
							auto pFile = std::dynamic_pointer_cast<AssetsFileContextInfo>(pUIInfo->getContextInfo());
							//If this is an AssetsFileContextInfo and not inserted yet, proceed.
							if (pFile == nullptr || !addedFileIDs.insert(pFile->getFileID()).second) continue;

							const std::vector<unsigned int> references = pFile->getReferences();
							for (size_t i = 0; i < references.size(); ++i)
							{
								//Same as above, but for the direct references.
								auto pFile = std::dynamic_pointer_cast<AssetsFileContextInfo>(pThis->pContext->getContextInfo(references[i]));
								if (pFile == nullptr || !addedFileIDs.insert(pFile->getFileID()).second) continue;
								assetsInfo.push_back(std::move(pFile));
							}
							assetsInfo.push_back(std::move(pFile));
						}
					}
					GetAllScriptInformation(*pThis->pContext, assetsInfo);
				}
				break;
			case IDC_CKBUNDLES:
			case IDC_CKASSETS:
			case IDC_CKRESOURCES:
			case IDC_CKGENERICS:
			case IDC_CKSELALL:
				if (wmEvent == BN_CLICKED)
					pThis->onClickSelectionCheckbox(wmId, Button_GetCheck((HWND)lParam));
				break;
			case IDM_HELP_ABOUT:
				DialogBox(pThis->hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, AboutDlgProc);
				break;
			default:
				//Let the manipulate dialog handle the command first.
				//(NOTE: This should be done for any menu item that can be overridden)
				if (IFileManipulateDialog *pActiveManipDlg = pThis->getActiveManipDlg())
					ret = (pActiveManipDlg->onCommand(wParam, lParam) ? 1 : 0);
				break;
			}
		}
		break;
	}
	return ret;
}
LRESULT CALLBACK MainWindow2::ProgSubclassProc(HWND hWnd, UINT message,
	WPARAM wParam, LPARAM lParam,
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	MainWindow2* pThis = (MainWindow2*)dwRefData;
	switch (message)
	{
	case WM_LBUTTONDBLCLK:
		if (auto pStatusTracker = dynamic_cast<Win32TaskStatusTracker*>(pThis->pStatusTracker.get()))
			pStatusTracker->open();
		return (LRESULT)0;
	}
	return DefSubclassProc(hWnd, message, wParam, lParam);
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
void MainWindow2::doOpenTab()
{
	HWND hTabsControl = GetDlgItem(hDlg, IDC_MANIPDLGTABS);
	size_t newTabIdx = this->manipDlgTabs.size();
	this->manipDlgTabs.emplace_back(ManipDlgDesc());

	MC_MTITEM newItem = {};
	newItem.dwMask = MC_MTIF_TEXT | MC_MTIF_PARAM;
	TCHAR sprntTmp[36];
	_stprintf_s(sprntTmp, TEXT("Tab %u"), (unsigned int)this->manipDlgTabs.size()); //TODO: Proper names
	newItem.pszText = sprntTmp;
	newItem.lParam = (LPARAM)newTabIdx;
	SendMessage(hTabsControl, MC_MTM_INSERTITEM, (WPARAM)newTabIdx, (LPARAM)&newItem);
	SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)newTabIdx, 0); //Also sends a SELCHANGE notification.
}
bool MainWindow2::preDeleteTab(MC_NMMTCLOSEITEM *pNotification)
{
	HWND hTabsControl = pNotification->hdr.hwndFrom;
	if (pNotification->iItem >= 0 && pNotification->iItem < this->manipDlgTabs.size())
	{
		MC_MTITEM itemInfo = {};
		itemInfo.dwMask = MC_MTIF_PARAM;
		//Retrieve the index of the tab in pThis->manipDlgTabs.
		SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)pNotification->iItem, (LPARAM)&itemInfo);
		size_t internalItemIdx = (size_t)itemInfo.lParam;
		assert(internalItemIdx < this->manipDlgTabs.size());

		if (internalItemIdx < this->manipDlgTabs.size() && this->manipDlgTabs[internalItemIdx].pCurManipDlg != nullptr)
		{
			IFileManipulateDialog *pDialog = this->manipDlgTabs[internalItemIdx].pCurManipDlg.get();
			bool changesApplyable = false;
			if (pDialog->hasUnappliedChanges(&changesApplyable))
			{
				SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)pNotification->iItem, 0);
				if (changesApplyable)
				{
					switch (MessageBox(this->hDlg, 
						TEXT("This tab has unsaved changes.\nDo you want to apply the changes before closing the tab?"), 
						TEXT("Asset Bundle Extractor"), 
						MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3))
					{
					case IDYES:
						if (pDialog->applyChanges())
							return true; //Close tab (changes applied).
						return false; //Don't close tab (changes not applied).
					case IDNO:
						return true; //Close tab without saving.
					case IDCANCEL:
						return false; //Don't close tab.
					}
				}
				else if (IDYES == MessageBox(this->hDlg, 
					TEXT("This tab has unsaved changes.\nDo you want to close the tab anyway and discard any unsaved changes?"), 
					TEXT("Asset Bundle Extractor"), 
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2))
				{
					return true;
				}
				return false;
			}
		}
	}
	else
		assert(false);
	return true;
}
void MainWindow2::onDeleteTab(MC_NMMTDELETEITEM *pNotification)
{
	HWND hTabsControl = pNotification->hdr.hwndFrom;
	if (pNotification->iItem >= 0 && pNotification->iItem < this->manipDlgTabs.size())
	{
		MC_MTITEM itemInfo = {};
		itemInfo.dwMask = MC_MTIF_PARAM;
		//Retrieve the index of the tab in pThis->manipDlgTabs.
		SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)pNotification->iItem, (LPARAM)&itemInfo);
		size_t internalItemIdx = (size_t)itemInfo.lParam;
		assert(internalItemIdx < this->manipDlgTabs.size());
		//Fix the pThis->manipDlgTabs indices in the tab control user data to account for pThis->manipDlgTabs.erase(...).
		for (size_t i = 0; i < this->manipDlgTabs.size(); i++)
		{
			if (i == pNotification->iItem)
				continue;
			itemInfo.dwMask = MC_MTIF_PARAM;
			if (SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)i, (LPARAM)&itemInfo)
				&& ((size_t)itemInfo.lParam) > internalItemIdx)
			{
				itemInfo.dwMask = MC_MTIF_PARAM;
				itemInfo.lParam--;
				SendMessage(hTabsControl, MC_MTM_SETITEM, (WPARAM)i, (LPARAM)&itemInfo);
			}
		}
		if (internalItemIdx == this->activeManipDlgTab)
		{
			//If there are at least two tabs, another tab should be selected by mCtrl at this point.
			assert(this->manipDlgTabs.size() == 1);
			if (this->activeManipDlgTab == internalItemIdx
				&& this->manipDlgTabs[internalItemIdx].pCurManipDlg != nullptr)
			{
				this->manipDlgTabs[internalItemIdx].pCurManipDlg->onHide(); //Hide the tab if necessary.
				InvalidateRect(hDlg, NULL, TRUE); //TODO: Redraw only the manipulate dialog area.
			}
		}
		this->manipDlgTabs.erase(this->manipDlgTabs.begin() + internalItemIdx);
		if (this->activeManipDlgTab > internalItemIdx)
		{
			this->activeManipDlgTab--;
			//The active tab must still be in range if there is at least one item.
			assert(this->activeManipDlgTab >= 0);
			assert(this->manipDlgTabs.empty() || this->activeManipDlgTab < this->manipDlgTabs.size());
		}
	}
	else
		assert(false);
}
void MainWindow2::onSwitchTabs(MC_NMMTSELCHANGE *pNotification)
{
	//Handle tab selection change.
	HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
	HWND hTabsControl = pNotification->hdr.hwndFrom;
	assert(pNotification->iItemNew != pNotification->iItemOld);
	bool redraw = false;
	//Deselect the old tab. 
	if (pNotification->iItemOld >= 0 && pNotification->iItemOld < this->manipDlgTabs.size())
	{
		MC_MTITEM itemInfo = {};
		itemInfo.dwMask = MC_MTIF_PARAM;
		//Retrieve the index of the tab in pThis->manipDlgTabs.
		SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)pNotification->iItemOld, (LPARAM)&itemInfo);
		size_t internalOldItemIdx = (size_t)itemInfo.lParam;
		assert(internalOldItemIdx < this->manipDlgTabs.size());

		ManipDlgDesc *pOldTabDesc = &this->manipDlgTabs[internalOldItemIdx]; //this->getActiveManipDlgDesc();
		if (pOldTabDesc)
		{
			if (pOldTabDesc->pCurManipDlg)
			{
				pOldTabDesc->pCurManipDlg->onHide();
				InvalidateRect(hDlg, NULL, TRUE); //TODO: Redraw only the manipulate dialog area.
				redraw = true;
			}
			if (!this->skipDeselectOnTabChange)
			{
				assert(internalOldItemIdx == this->activeManipDlgTab);
				//Undo the file selections in the tree list.
				this->ignoreTreeSelChanges = true;
				for (size_t i = 0; i < pOldTabDesc->selection.size(); i++)
				{
					MC_HTREELISTITEM hTreeItem;
					if (getEntryParam_TreeItem(pOldTabDesc->selection[i], hTreeItem))
						setSelectItem(hTree, hTreeItem, false);
				}
				this->ignoreTreeSelChanges = false;
				this->activeManipDlgTab = SIZE_MAX; //Deselect
				std::vector<ITreeParameter*> emptySelection;
				this->doUpdateSelectionCheckboxes(emptySelection);
			}
		}
	}
	//Select the new tab.
	if (pNotification->iItemNew >= 0 && pNotification->iItemNew < this->manipDlgTabs.size())
	{
		MC_MTITEM itemInfo = {};
		itemInfo.dwMask = MC_MTIF_PARAM;
		//Retrieve the index of the tab in pThis->manipDlgTabs.
		SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)pNotification->iItemNew, (LPARAM)&itemInfo);
		size_t internalNewItemIdx = (size_t)itemInfo.lParam;
		assert(internalNewItemIdx < this->manipDlgTabs.size());

		this->activeManipDlgTab = internalNewItemIdx; //Select
		ManipDlgDesc *pNewTabDesc = this->getActiveManipDlgDesc();
		if (!this->skipDeselectOnTabChange)
		{
			//Redo the file selections in the tree list.
			this->ignoreTreeSelChanges = true;
			for (size_t i = 0; i < pNewTabDesc->selection.size(); i++)
			{
				MC_HTREELISTITEM hTreeItem;
				if (getEntryParam_TreeItem(pNewTabDesc->selection[i], hTreeItem))
					setSelectItem(hTree, hTreeItem, true);
			}
			this->ignoreTreeSelChanges = false;
		}
		if (pNewTabDesc->pCurManipDlg)
		{
			pNewTabDesc->pCurManipDlg->onShow();
			this->onResize();
			redraw = true;
		}
		if (!this->skipDeselectOnTabChange)
			this->doUpdateSelectionCheckboxes(pNewTabDesc->selection);
	}
	if (redraw)
		InvalidateRect(hDlg, NULL, TRUE); //TODO: Redraw only the manipulate dialog area.
}
void MainWindow2::onResize(bool defer)
{
	RECT client = {};
	GetClientRect(hDlg, &client);
	long width = client.right-client.left;
	long height = client.bottom-client.top;

	HDWP deferCtx = defer ? BeginDeferWindowPos(11) : NULL;
	bool retry = false;

	long fontHeight = 16;
	long bottomPanelHeight = 25;
	long bottomPanelTop = height - bottomPanelHeight;
	long bottomLeftPanelLeft = 7;
	long bottomLeftPanelWidth = std::min<long>((width - bottomLeftPanelLeft) / 3, 125);
	long bottomRightPanelLeft = bottomLeftPanelWidth + 7;
	long bottomRightPanelWidth = width - bottomRightPanelLeft - 7;
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_PROGSEPARATE), -2, bottomPanelTop, 2 + bottomLeftPanelLeft + bottomLeftPanelWidth, bottomPanelHeight);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_PROGMAIN), bottomLeftPanelLeft, bottomPanelTop + 3, bottomLeftPanelWidth - 7, bottomPanelHeight - 6);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_PROGSEPARATE2), bottomRightPanelLeft, bottomPanelTop, (width + 2) - bottomRightPanelLeft, bottomPanelHeight);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_SPROGDESC), bottomRightPanelLeft + 3, bottomPanelTop + 4, bottomRightPanelWidth - 3, fontHeight);
	
	long leftPanelTop = 4;
	long leftPanelLeft = 7;
	long leftPanelWidth = (long)(width * this->mainPanelSplitter.getLeftOrTopPanelRatio() - leftPanelLeft);
	long leftPanelClientWidth = leftPanelWidth - 5;
	long panelHeight = height - bottomPanelHeight;
	{
		long curCheckboxLeft = leftPanelLeft;
		static const long checkboxHeight = 16;
		static const long ckBundlesWidth = 60;
		static const long ckAssetsWidth = 60;
		static const long ckResourcesWidth = 76;
		static const long ckGenericsWidth = 60;

		static const long ckSelAllWidth = 60;

		doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CKBUNDLES), curCheckboxLeft, leftPanelTop, std::min<long>(ckBundlesWidth, leftPanelClientWidth - curCheckboxLeft), checkboxHeight);
		curCheckboxLeft += ckBundlesWidth;
		if ((curCheckboxLeft + ckAssetsWidth) > leftPanelClientWidth && curCheckboxLeft > leftPanelLeft)
		{
			leftPanelTop += checkboxHeight + 4;
			curCheckboxLeft = leftPanelLeft;
		}
		doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CKASSETS), curCheckboxLeft, leftPanelTop, std::min<long>(ckAssetsWidth, leftPanelClientWidth - curCheckboxLeft), checkboxHeight);
		curCheckboxLeft += ckAssetsWidth;
		if ((curCheckboxLeft + ckResourcesWidth) > leftPanelClientWidth && curCheckboxLeft > leftPanelLeft)
		{
			leftPanelTop += checkboxHeight + 4;
			curCheckboxLeft = leftPanelLeft;
		}
		doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CKRESOURCES), curCheckboxLeft, leftPanelTop, std::min<long>(ckResourcesWidth, leftPanelClientWidth - curCheckboxLeft), checkboxHeight);
		curCheckboxLeft += ckResourcesWidth;
		if ((curCheckboxLeft + ckGenericsWidth) > leftPanelClientWidth && curCheckboxLeft > leftPanelLeft)
		{
			leftPanelTop += checkboxHeight + 4;
			curCheckboxLeft = leftPanelLeft;
		}

		doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CKGENERICS), curCheckboxLeft, leftPanelTop, std::min<long>(ckGenericsWidth, leftPanelClientWidth - curCheckboxLeft), checkboxHeight);
		leftPanelTop += checkboxHeight + 4;
		curCheckboxLeft = leftPanelLeft;

		doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CKSELALL), curCheckboxLeft, leftPanelTop, std::min<long>(ckSelAllWidth, leftPanelClientWidth - curCheckboxLeft), checkboxHeight);
		leftPanelTop += checkboxHeight + 4;
	}
	
	HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
	{
		//Calculate relative column sizes for the file tree list.
		MC_TLCOLUMN col;
		col.fMask = MC_TLCF_WIDTH;
		col.cx = 1;
		SendMessage(hTree, MC_TLM_GETCOLUMN, 0, (LPARAM)&col);
		int cxTree = col.cx;
		col.fMask = MC_TLCF_WIDTH;
		col.cx = 1;
		SendMessage(hTree, MC_TLM_GETCOLUMN, 1, (LPARAM)&col);
		int cxFileID = col.cx;
		RECT treeClientRect = {};
		GetClientRect(hTree, &treeClientRect);
		LONG actualWidth = treeClientRect.right - treeClientRect.left - 1;
		int totalWidth = cxTree + cxFileID;
		if (cxTree > 0 && cxFileID > 0 && actualWidth > 0)
		{
			cxTree = (int)(cxTree * ((float)totalWidth / (float)actualWidth));
			cxFileID = (int)(cxFileID * ((float)totalWidth / (float)actualWidth));
			this->fileTreeColumnRatio = (float)cxTree / (float)actualWidth;
			if (this->fileTreeColumnRatio < 0.75f) this->fileTreeColumnRatio = 0.75f;
			else if (this->fileTreeColumnRatio > 0.95f) this->fileTreeColumnRatio = 0.95f;
		}
	}
	long leftPanelHeight = (bottomPanelTop - 4) - leftPanelTop;
	doMoveWindow(deferCtx, retry, hTree, leftPanelLeft, leftPanelTop, leftPanelClientWidth, leftPanelHeight);

	long contentPanelLeft = leftPanelLeft + leftPanelWidth, contentPanelTop = 10;
	long contentPanelWidth = width - contentPanelLeft;
	long contentPanelHeight = panelHeight - 20;
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_CONTENTSEPARATE), contentPanelLeft, -2, contentPanelWidth + 2, panelHeight + 2);
	
	HWND hTabsControl = GetDlgItem(hDlg, IDC_MANIPDLGTABS);
	long tabsControlBottom = std::min<int>(24, panelHeight);
	doMoveWindow(deferCtx, retry, hTabsControl, contentPanelLeft + 1, 0, contentPanelWidth, tabsControlBottom - 0);

	if (defer)
	{
		if (retry || !EndDeferWindowPos(deferCtx))
			onResize(false);
		deferCtx = NULL;
	}
	//For some reason, using the deferred method for the child dialog silently makes no window resize.
	if (IFileManipulateDialog *pCurManipDlg = this->getActiveManipDlg())
	{
		//GetWindowRect(hTabsControl, &tabsRect);
		RECT tabsRect = {};
		tabsRect.left = 0; tabsRect.right = contentPanelWidth;
		tabsRect.top = tabsControlBottom + 1; tabsRect.bottom = panelHeight;
		//TabCtrl_AdjustRect(hTabsControl, FALSE, &tabsRect);
		MoveWindow(pCurManipDlg->getWindowHandle(), 
			tabsRect.left,					tabsRect.top, 
			tabsRect.right-tabsRect.left,	tabsRect.bottom-tabsRect.top, TRUE);
	}
	
	UpdateWindow(hDlg);
	//InvalidateRect(GetDlgItem(hDlg, IDC_PROGMAIN), NULL, TRUE);
	//InvalidateRect(GetDlgItem(hDlg, IDC_SPROGDESC), NULL, TRUE);
	//InvalidateRect(GetDlgItem(hDlg, IDC_TREEFILES), NULL, TRUE);
	//InvalidateRect(hDlg, NULL, TRUE);

	//Resize the file tree control's main column.
	RECT treeClientRect = {};
	GetClientRect(hTree, &treeClientRect);
	MC_TLCOLUMN col;
	col.fMask = MC_TLCF_WIDTH;
	col.cx = (int)((treeClientRect.right - treeClientRect.left - 1) * fileTreeColumnRatio);
	SendMessage(hTree, MC_TLM_SETCOLUMN, 0, (LPARAM)&col);
	col.cx = (int)((treeClientRect.right - treeClientRect.left - 1) * (1.0 - fileTreeColumnRatio));
	SendMessage(hTree, MC_TLM_SETCOLUMN, 1, (LPARAM)&col);
}
inline bool manipDlgIsCompatibleWith(IFileManipulateDialog *pDialog, ITreeParameter *newItem)
{
	bool tmp;
	FileEntryUIInfo *pNewFileEntryInfo = getEntryParam_FileEntryInfo(newItem, tmp);
	return (pNewFileEntryInfo
		&& newItem->isFileManipulateDialogInfo()
		&& newItem->asFileManipulateDialogInfo()->type == pDialog->getType());
}

inline bool getSelectionType(ITreeParameter *pSel, EFileContextType &type, bool onlyPrimaryDialogOrFileEntry)
{
	type = FileContext_COUNT;
	bool tmp;
	if (FileEntryUIInfo *pSelFileEntryInfo = getEntryParam_FileEntryInfo(pSel, tmp))
	{
		if (!pSelFileEntryInfo->pending && pSelFileEntryInfo->pContextInfo
			&& pSelFileEntryInfo->pContextInfo->getFileContext()
			&& (!onlyPrimaryDialogOrFileEntry ||
				pSelFileEntryInfo->standardDialogsCount <= 0 ||
				&pSelFileEntryInfo->standardDialogs[0] == pSel))
		{
			type = pSelFileEntryInfo->pContextInfo->getFileContext()->getType();
			if (type < (EFileContextType)0 || type >= FileContext_COUNT)
				return false;
			return true;
		}
	}
	return false;
}
void MainWindow2::onClickSelectionCheckbox(unsigned int checkboxID, int checkState)
{
	HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);

	EFileContextType fileContextType;
	switch (checkboxID)
	{
	case IDC_CKBUNDLES:
		fileContextType = FileContext_Bundle;
		break;
	case IDC_CKASSETS:
		fileContextType = FileContext_Assets;
		break;
	case IDC_CKRESOURCES:
		fileContextType = FileContext_Resources;
		break;
	case IDC_CKGENERICS:
		fileContextType = FileContext_Generic;
		break;
	case IDC_CKSELALL:
		{
			this->ignoreTreeSelChanges = true;
			if (checkState == BST_CHECKED)
			{
				for (auto fileIt = fileEntries.begin(); fileIt != fileEntries.end(); ++fileIt)
				{
					if (fileIt->hTreeItem)
						setSelectItem(hTree, fileIt->hTreeItem, true);
				}
			}
			else
			{
				MC_HTREELISTITEM selection = NULL;
				while ((selection = MCTreeList_GetNextSelection(hTree, selection)) != NULL)
				{
					setSelectItem(hTree, selection, false);
				}
			}
			this->ignoreTreeSelChanges = false;
			this->onChangeFileSelection();
		}
		return;
	default:
		return;
	}
	bool selectionUpdated = false;
	switch (checkState)
	{
	case BST_INDETERMINATE:
		Button_SetCheck(GetDlgItem(hDlg, checkboxID), BST_UNCHECKED);
		//Fall through to the unchecked case.
	case BST_UNCHECKED:
		if (ManipDlgDesc *pTab = getActiveManipDlgDesc())
		{
			this->ignoreTreeSelChanges = true;
			for (size_t i = 0; i < pTab->selection.size(); i++)
			{
				EFileContextType type;
				if (getSelectionType(pTab->selection[i], type, false) && type == fileContextType)
				{
					bool tmp;
					if (FileEntryUIInfo *pSelFileEntryInfo = getEntryParam_FileEntryInfo(pTab->selection[i], tmp))
					{
						selectionUpdated = true;
						setSelectItem(hTree, pSelFileEntryInfo->hTreeItem, false);
					}
				}
			}
			this->ignoreTreeSelChanges = false;
		}
		break;
	case BST_CHECKED:
		{
			this->ignoreTreeSelChanges = true;
			for (auto fileIt = fileEntries.begin(); fileIt != fileEntries.end(); ++fileIt)
			{
				EFileContextType type;
				if (fileIt->hTreeItem && getSelectionType(&*fileIt, type, false) && type == fileContextType)
				{
					selectionUpdated = true;
					setSelectItem(hTree, fileIt->hTreeItem, true);
				}
			}
			this->ignoreTreeSelChanges = false;
		}
		break;
	}
	if (selectionUpdated)
		this->onChangeFileSelection();
}
void MainWindow2::doUpdateSelectionCheckboxes(const std::vector<ITreeParameter*> &selections)
{
	std::array<size_t, FileContext_COUNT> selectionCountersByType = {};
	for (size_t i = 0; i < selections.size(); i++)
	{
		EFileContextType type;
		if (getSelectionType(selections[i], type, true))
			selectionCountersByType[type]++;
	}
	
	static const int entryTypeDlgItems[] = {IDC_CKBUNDLES, IDC_CKASSETS, IDC_CKRESOURCES, IDC_CKGENERICS};
	assert(fileEntryCountersByType.size() == selectionCountersByType.size());
	assert(fileEntryCountersByType.size() == sizeof(entryTypeDlgItems) / sizeof(int));
	size_t selectionSum = 0;
	for (size_t i = 0; i < fileEntryCountersByType.size()
		&& i < selectionCountersByType.size()
		&& i < sizeof(entryTypeDlgItems) / sizeof(int); i++)
	{
		HWND hCheckbox = GetDlgItem(this->hDlg, entryTypeDlgItems[i]);
		selectionSum += selectionCountersByType[i];
		if (selectionCountersByType[i] > 0)
		{
			assert(selectionCountersByType[i] <= fileEntryCountersByType[i]);
			if (selectionCountersByType[i] >= fileEntryCountersByType[i])
				Button_SetCheck(hCheckbox, BST_CHECKED);
			else
				Button_SetCheck(hCheckbox, BST_INDETERMINATE);
		}
		else
		{
			Button_SetCheck(hCheckbox, BST_UNCHECKED);
		}
	}
	HWND hSelAllCheckbox = GetDlgItem(this->hDlg, IDC_CKSELALL);
	if (selectionSum > 0)
	{
		assert(selectionSum <= fileEntries.size());
		if (selectionSum >= fileEntries.size())
			Button_SetCheck(hSelAllCheckbox, BST_CHECKED);
		else
			Button_SetCheck(hSelAllCheckbox, BST_UNCHECKED);
	}
	else
		Button_SetCheck(hSelAllCheckbox, BST_UNCHECKED);
}
void MainWindow2::selectFileContext(unsigned int fileID, bool preventOpenNewTab)
{
	HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
	FileContextInfo_ptr pContextInfo = this->pContext->getContextInfo(fileID);
	if (pContextInfo == nullptr)
		return;
	auto fileUIEntryIt = this->fileEntriesByContextInfo.find(pContextInfo.get());
	if (fileUIEntryIt == this->fileEntriesByContextInfo.end())
		return;
	ManipDlgDesc *pCurTabDesc = this->getActiveManipDlgDesc();
	FileManipulateDialogInfo *pDialogInfo = nullptr;
	for (auto iter = fileUIEntryIt->second->getDialogsIterator(); !iter.end(); ++iter)
	{
		if (pCurTabDesc == nullptr || pCurTabDesc->pCurManipDlg == nullptr || 
			iter->type == pCurTabDesc->pCurManipDlg->getType())
		{
			pDialogInfo = &*iter;
			break;
		}
	}
	if (pDialogInfo != nullptr && pDialogInfo->hTreeItem != NULL)
	{
		MC_TLITEM item;
		item.fMask = MC_TLIF_STATE;
		item.state = MC_TLIS_SELECTED;
		item.stateMask = MC_TLIS_SELECTED;
		this->oneshot_applySelectionToCurrentTab = preventOpenNewTab;
		SendMessage(hTree, MC_TLM_SETITEM, reinterpret_cast<WPARAM>(pDialogInfo->hTreeItem), reinterpret_cast<LPARAM>(&item));
		this->oneshot_applySelectionToCurrentTab = false;
	}
}
bool MainWindow2::loadBundleEntry(std::shared_ptr<BundleFileContextInfo> pBundleInfo, unsigned int bundleEntryIdx)
{
	HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
	auto fileUIEntryIt = this->fileEntriesByContextInfo.find(pBundleInfo.get());
	if (fileUIEntryIt == this->fileEntriesByContextInfo.end())
		return false;
	std::shared_ptr<ITask> pTask = pContext->CreateBundleEntryOpenTask(pBundleInfo, bundleEntryIdx);
	if (!pTask)
	{
		if (!pBundleInfo->entryIsRemoved(bundleEntryIdx))
			MessageBox(this->hDlg, TEXT("Failed to read the bundle entry."), TEXT("UABE"), 16);
		return false;
	}
	char entryNameBuf[32];
	std::string entryName = pBundleInfo->getNewEntryName(bundleEntryIdx);
	if (entryName.empty())
	{
		sprintf_s(entryNameBuf, "Entry %u", bundleEntryIdx);
		entryName = entryNameBuf;
	}
	MC_HTREELISTITEM treeItem = insertPendingEntry(hTree, fileUIEntryIt->second->hTreeItem, entryName);

	fileEntries.emplace_back(FileEntryUIInfo(treeItem, entryName, false));
	fileEntries.back().myiter = --fileEntries.end();

	updateEntryInfoRef(hTree, treeItem, fileEntries.back());
	updateEntryName(hTree, treeItem, std::string("Pending : ") + entryName);
	pendingFileEntriesByTask.insert(std::make_pair(pTask.get(), &fileEntries.back()));

	if (!this->pContext->taskManager.enqueue(pTask))
	{
		OnFileEntryLoadFailure(pTask.get(), std::string("Failed to enqueue the file open task."));
		return false;
	}
	return true;
}
void MainWindow2::onChangeFileSelection()
{
	HWND hTabsControl = GetDlgItem(hDlg, IDC_MANIPDLGTABS);
	ManipDlgDesc *pCurTabDesc = this->getActiveManipDlgDesc();
	if (!pCurTabDesc && this->manipDlgTabs.size() > 0)
	{
		//Choose the last tab.
		MC_MTITEM itemInfo = {};
		itemInfo.dwMask = MC_MTIF_PARAM;
		//Retrieve the index of the tab in pThis->manipDlgTabs.
		SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)(this->manipDlgTabs.size() - 1), (LPARAM)&itemInfo);
		size_t internalItemIdx = (size_t)itemInfo.lParam;
		assert(internalItemIdx < this->manipDlgTabs.size());
		if (internalItemIdx < this->manipDlgTabs.size())
		{
			//Select the tab.
			SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)(this->manipDlgTabs.size() - 1), (LPARAM)0); //wParam:idx
			this->activeManipDlgTab = internalItemIdx;
			pCurTabDesc = &this->manipDlgTabs[internalItemIdx];
		}
	}
	//May want to fine-tune the 'auto-close tab or don't' / 'auto-open new tab' behavior.
	//-> What if the user selects just one additional file, while the tab returns true on hasUnappliedChanges or doesPreferNoAutoclose?
	//   Problem could be that new tabs are opened too easily when not desired.
	// Could be quite cumbersome and error-prone to handle in detail.
	if (!pCurTabDesc
		|| (pCurTabDesc->pCurManipDlg
			&& (pCurTabDesc->pCurManipDlg->hasUnappliedChanges() || pCurTabDesc->pCurManipDlg->doesPreferNoAutoclose()))
			&& !this->oneshot_applySelectionToCurrentTab)
	{
		size_t newTabIdx = this->manipDlgTabs.size();
		//Note: Manipulating manipDlgTabs invalidates pCurTabDesc.
		this->manipDlgTabs.emplace_back(ManipDlgDesc());
		this->activeManipDlgTab = newTabIdx;
		pCurTabDesc = &this->manipDlgTabs[newTabIdx];

		this->skipDeselectOnTabChange = true;

		MC_MTITEM newItem = {};
		newItem.dwMask = MC_MTIF_TEXT | MC_MTIF_PARAM;
		TCHAR sprntTmp[36];
		_stprintf_s(sprntTmp, TEXT("Tab %u"), (unsigned int)(newTabIdx + 1)); //TODO: Proper names
		newItem.pszText = sprntTmp;
		newItem.lParam = (LPARAM)newTabIdx;
		SendMessage(hTabsControl, MC_MTM_INSERTITEM, (WPARAM)newTabIdx, (LPARAM)&newItem);
		SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)newTabIdx, (LPARAM)0); //wParam:idx

		this->skipDeselectOnTabChange = false;
	}

	HWND hTree = GetDlgItem(hDlg, IDC_TREEFILES);
	std::vector<ITreeParameter*> newSelection;
	MC_HTREELISTITEM selection = NULL;
	while ((selection = MCTreeList_GetNextSelection(hTree, selection)) != NULL)
	{
		ITreeParameter *pCurParam = getEntryParam(hTree, selection);
		newSelection.push_back(pCurParam);
	}
	bool selectionIsSaveable = false;
	for (size_t i = 0; i < newSelection.size(); ++i)
	{
		bool tmp;
		FileEntryUIInfo *pNewFileEntryInfo = getEntryParam_FileEntryInfo(newSelection[i], tmp);
		if (pNewFileEntryInfo != nullptr
			&& pNewFileEntryInfo->getContextInfoPtr() != nullptr
			&& pNewFileEntryInfo->getContextInfoPtr()->getParentFileID() == 0
			&& pNewFileEntryInfo->getContextInfoPtr()->hasAnyChanges(*this->pContext))
		{
			selectionIsSaveable = true;
			break;
		}
	}
	EnableMenuItem(getMenu(), IDM_FILE_SAVE, selectionIsSaveable ? MF_ENABLED : MF_GRAYED);
	doUpdateSelectionCheckboxes(newSelection);
	if (pCurTabDesc->pCurManipDlg)
	{
		//Find the FileEntryUIInfo entries that were selected and those that were deselected.
		//-> Add/remove these from the active manipulate dialog.
		//Assuming the selections are both sorted by the tree view order.

		//Count the amount of selected dialog info entries for the current dialog.
		size_t nShownSelections = 0;

		auto addFileContextsToDialog = [&newSelection, pCurTabDesc](size_t start, size_t limit, FileEntryUIInfo* pCurFileEntryInfo = nullptr)
		{
			size_t n = 0;
			FileEntryUIInfo* pLastAddedFileEntry = nullptr; bool tmp;
			for (size_t i = start; i < limit; i++)
			{
				ITreeParameter* newParam = newSelection[i];
				FileEntryUIInfo* pNewFileEntryInfo = getEntryParam_FileEntryInfo(newParam, tmp);
				if (pNewFileEntryInfo
					&& pNewFileEntryInfo != pCurFileEntryInfo
					&& pNewFileEntryInfo != pLastAddedFileEntry
					&& newParam->isFileManipulateDialogInfo()
					&& newParam->asFileManipulateDialogInfo()->type == pCurTabDesc->pCurManipDlg->getType())
				{
					//Add a new file context to this dialog.
					FileManipulateDialogInfo* pNewDialogInfo = newParam->asFileManipulateDialogInfo();
					pCurTabDesc->pCurManipDlg->addFileContext(
						std::make_pair(pNewFileEntryInfo, pNewDialogInfo->param));
					n++;
					pLastAddedFileEntry = pNewFileEntryInfo;
				}
			}
			return n;
		};

		size_t curIdx = pCurTabDesc->selection.size(), newIdx = newSelection.size();
		bool tmp;
		FileEntryUIInfo *pLastFileEntryInfo = nullptr;
		for (; curIdx > 0; curIdx--)
		{
			ITreeParameter *curParam = pCurTabDesc->selection[curIdx-1];
			FileEntryUIInfo *pCurFileEntryInfo = getEntryParam_FileEntryInfo(curParam, tmp);
			if (!pCurFileEntryInfo || pCurFileEntryInfo == pLastFileEntryInfo
				|| !curParam->isFileManipulateDialogInfo()
				|| curParam->asFileManipulateDialogInfo()->type != pCurTabDesc->pCurManipDlg->getType())
				//Ignore entries that are removed/handled already and those with a different dialog type.
				continue;
			size_t addLimit = newIdx;
			bool fileEntryInfoFound = false;
			for (; newIdx > 0; newIdx--)
			{
				ITreeParameter *newParam = newSelection[newIdx-1];
				if (newParam == curParam)
				{
					//Found the same tree parameter (i.e. a tree view entry).
					fileEntryInfoFound = true;
				}
				else if (FileEntryUIInfo *pNewFileEntryInfo = getEntryParam_FileEntryInfo(newParam, tmp))
				{
					if (pNewFileEntryInfo->pending)
					{
						//Pending selected items can only be added later, once loaded.
						//Set the selection entry to nullptr so it will not be treated as added.
						newSelection[newIdx-1] = nullptr;
						continue;
					}
					if (pNewFileEntryInfo == pLastFileEntryInfo)
					{
						if (fileEntryInfoFound)
							break; //Went past the last item with the same file.
						//Skip this tree entry in the new selections list,
						// since one entry with this file was handled in the last iteration of the outer loop.
						continue; 
					}
					if (pCurFileEntryInfo && pNewFileEntryInfo == pCurFileEntryInfo)
					{
						if (newParam->isFileManipulateDialogInfo()
						  && newParam->asFileManipulateDialogInfo()->type == curParam->asFileManipulateDialogInfo()->type)
						{
							//New tree parameter is a dialog structure of the same type that refers to the same file.
							fileEntryInfoFound = true;
						}
					}
					else if (fileEntryInfoFound)
						break; //Went past the last item with the same file.
				}
			}
			if (!fileEntryInfoFound)
			{
				//Do not add any new elements (yet), since we can't tell which elements are new :
				//  the old selection is no proper anchor to determine what is new since it has been deleted.
				newIdx = addLimit;
			}
			nShownSelections += addFileContextsToDialog(newIdx, addLimit, pCurFileEntryInfo);
			if (!fileEntryInfoFound)
			{
				//Remove the file context from the dialog since no matching tree element is selected anymore.
				pCurTabDesc->pCurManipDlg->removeFileContext(pCurFileEntryInfo);
				//The dialog may have closed itself after removing a file context.
				if (!pCurTabDesc->pCurManipDlg)
					break;
			}
			else
				nShownSelections++; //This entry still is shown.
			pLastFileEntryInfo = pCurFileEntryInfo;
		}
		FileEntryUIInfo *pLastAddedFileEntry = nullptr;
		if (pCurTabDesc->pCurManipDlg)
			nShownSelections += addFileContextsToDialog(0, newIdx, pLastFileEntryInfo);
		if (nShownSelections == 0)
		{
			//If none of the new selections apply to the current dialog, close the dialog.
			if (pCurTabDesc->pCurManipDlg)
			{
				pCurTabDesc->pCurManipDlg->onHide();
				pCurTabDesc->pCurManipDlg.reset();
				InvalidateRect(hDlg, NULL, TRUE); //TODO: Redraw only the manipulate dialog area.
			}
		}
		else
		{
			//If at least one file is shown in the dialog, there should be a matching selection.
			assert(newSelection.size() > 0); 
		}
	}

	if (!pCurTabDesc->pCurManipDlg)
	{
		if (newSelection.size() > 0)
		{
			std::set<EFileManipulateDialogType> possibleDialogTypes;
			for (size_t i = 0; i < newSelection.size(); i++)
			{
				ITreeParameter *curParam = newSelection[i];
				if (!curParam) continue;
				if (FileManipulateDialogInfo *pDialogInfo = curParam->asFileManipulateDialogInfo())
				{
					if (pDialogInfo->type != FileManipulateDialog_Other)
						possibleDialogTypes.insert(pDialogInfo->type);
				}
			}
			if (!possibleDialogTypes.empty())
			{
				//TODO : Handle the case where several dialog types are possible for the selection.
				//Option A : Display a notification dialog so the user selects only the entries they want.
				//Option B : Display a selection for a dialog type to use.
				EFileManipulateDialogType targetDialogType = *(possibleDialogTypes.begin());
				FileEntryUIInfo *pLastAddedFileEntry = nullptr;
				for (size_t i = 0; i < newSelection.size(); i++)
				{
					ITreeParameter *curParam = newSelection[i];
					bool tmp;
					FileEntryUIInfo *pCurFileEntry = getEntryParam_FileEntryInfo(curParam, tmp);
					if (!pCurFileEntry || pCurFileEntry == pLastAddedFileEntry)
						continue; //Only add one dialog info per file.
					if (FileManipulateDialogInfo *pDialogInfo = curParam->asFileManipulateDialogInfo())
					{
						if (pDialogInfo->type == targetDialogType)
						{
							if (!pCurTabDesc->pCurManipDlg)
							{
								//Create the dialog type if necessary.
								//TODO: Move the factory to the MainWindow2 class instead.
								if (!(pCurTabDesc->pCurManipDlg = this->pDialogFactory->construct(
										pDialogInfo->type, GetDlgItem(hDlg, IDC_CONTENTSEPARATE))))//hTabsControl))))
									continue;
							}
							pCurTabDesc->pCurManipDlg->selfPtr = pCurTabDesc->pCurManipDlg;
							pCurTabDesc->pCurManipDlg->addFileContext(std::make_pair(pCurFileEntry, pDialogInfo->param));
						}
					}
					pLastAddedFileEntry = pCurFileEntry;
				}
			}
		}
		pCurTabDesc->selection.swap(newSelection);
		if (pCurTabDesc->pCurManipDlg)
		{
			//Show the new dialog and size it properly.
			pCurTabDesc->pCurManipDlg->onShow();
			this->onResize();
			SetFocus(hTree);
		}
	}
	else
	{
		pCurTabDesc->selection.swap(newSelection);
	}
}
void MainWindow2::addPendingBaseFileEntry(ITask *pTask, const std::string &path)
{
	HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
	MC_HTREELISTITEM treeItem = insertPendingEntry(hTree, NULL, path);

	fileEntries.emplace_back(FileEntryUIInfo(treeItem, path, true));
	fileEntries.back().myiter = --fileEntries.end();

	updateEntryInfoRef(hTree, treeItem, fileEntries.back());
	updateEntryName(hTree, treeItem, std::string("Pending : ") + fileEntries.back().getShortName());
	pendingFileEntriesByTask.insert(std::make_pair(pTask, &fileEntries.back()));
}
void MainWindow2::onOpenFileCommand()
{
	//assert(this->pContext);
	std::vector<char*> filePaths;
	HRESULT hr = ShowFileOpenDialogMultiSelect(this->hDlg, filePaths,
		"*.*|All types:*.unity3d|Bundle file:*.assets|Assets file", NULL, NULL,
		"Select the files to open",
		UABE_FILEDIALOG_FILE_GUID);
	if (SUCCEEDED(hr))
	{
		HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
		for (size_t i = 0; i < filePaths.size(); i++)
		{
			std::string pathString(filePaths[i]);
			std::shared_ptr<ITask> pTask = this->pContext->CreateFileOpenTask(pathString);
			if (!pTask)
				MessageBox(this->hDlg, TEXT("Failed to open the file."), TEXT("UABE"), 16);
			else
			{
				addPendingBaseFileEntry(pTask.get(), pathString);

				if (!this->pContext->taskManager.enqueue(pTask))
					OnFileEntryLoadFailure(pTask.get(), std::string("Failed to enqueue the file open task."));
			}
		}
		FreeFilePathsMultiSelect(filePaths);
	}
}
void MainWindow2::OnFindClassDatabaseFailure(AssetsFileContextInfo *pAssetsFileInfo, ClassDatabasePackage &package)
{
	if (pAssetsFileInfo->getAssetsFileContext() && pAssetsFileInfo->getAssetsFileContext()->getAssetsFile())
	{
		if (!TryFindClassDatabase(pAssetsFileInfo))
		{
			auto entryIt = fileEntriesByContextInfo.find(pAssetsFileInfo);
			if (entryIt != fileEntriesByContextInfo.end())
			{
				fileEntriesPendingForDbSelection.push_back(DbSelectionQueueEntry(entryIt->second, true));
				if (pSelectClassDbDialog == nullptr)
				{
					OpenClassDatabaseSelection(pAssetsFileInfo, true);
				}
			}
		}
	}
}
bool MainWindow2::TryFindClassDatabase(AssetsFileContextInfo *pAssetsFileInfo)
{
	const char *targetVersion = pAssetsFileInfo->getAssetsFileContext()->getAssetsFile()->typeTree.unityVersion;
	for (auto it = databaseFilesByEngineVersion.begin(); it != databaseFilesByEngineVersion.end(); ++it)
	{
		if (!it->first.compare(targetVersion))
		{
			pAssetsFileInfo->SetClassDatabase(it->second);
			return true;
		}
	}
	if (defaultDatabaseFile != nullptr)
	{
		pAssetsFileInfo->SetClassDatabase(defaultDatabaseFile);
		return true;
	}
	return false;
}
void MainWindow2::OpenClassDatabaseSelection(AssetsFileContextInfo *pAssetsFileInfo, bool reason_DatabaseNotFound)
{
	const char *targetVersion = pAssetsFileInfo->getAssetsFileContext()->getAssetsFile()->typeTree.unityVersion;
	pSelectClassDbDialog.reset(new SelectClassDbDialog(hInstance, hDlg, pContext->classPackage));
	pSelectClassDbDialog->setAffectedFileName(pAssetsFileInfo->getFileName());
	pSelectClassDbDialog->setDialogReason(reason_DatabaseNotFound);
	pSelectClassDbDialog->setEngineVersion(std::string(targetVersion));
	HWND hSelectDialogWnd = pSelectClassDbDialog->ShowModeless(WM_APP+2);
	if (hSelectDialogWnd == NULL) pSelectClassDbDialog.reset();
	assert(hSelectDialogWnd != NULL);
}

void MainWindow2::OnRemoveContextInfo(FileContextInfo *info)
{
	auto cacheIt = disposableCacheElements.find(info);
	if (cacheIt != disposableCacheElements.end())
		disposableCacheElements.erase(cacheIt);
	while (!fileEntriesPendingForDbSelection.empty() && fileEntriesPendingForDbSelection.front().pEntry == nullptr)
		fileEntriesPendingForDbSelection.pop_front(); //Cleanup
	fileEntriesByContextInfo.erase(info);
	//info->decRef(); //Reference from FileEntryUIInfo
}
void MainWindow2::OnUpdateContainers(AssetsFileContextInfo *pFile)
{
	for (auto handlerIt = eventHandlers.begin(); handlerIt != eventHandlers.end(); ++handlerIt)
	{
		(*handlerIt)->onUpdateContainers(pFile);
	}
}
void MainWindow2::OnUpdateDependencies(AssetsFileContextInfo *pFile, size_t from, size_t to)
{
	for (auto handlerIt = eventHandlers.begin(); handlerIt != eventHandlers.end(); ++handlerIt)
	{
		(*handlerIt)->onUpdateDependencies(pFile, from, to);
	}
}
void MainWindow2::OnChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved)
{
	for (auto handlerIt = eventHandlers.begin(); handlerIt != eventHandlers.end(); ++handlerIt)
	{
		(*handlerIt)->onChangeAsset(pFile, pathID, wasRemoved);
	}
}
void MainWindow2::OnChangeBundleEntry(BundleFileContextInfo *pFile, size_t index)
{
	this->updateBundleEntryName(pFile, index, pFile->getNewEntryName(index));
	for (auto handlerIt = eventHandlers.begin(); handlerIt != eventHandlers.end(); ++handlerIt)
	{
		(*handlerIt)->onUpdateBundleEntry(pFile, index);
	}
}
void MainWindow2::hideManipulateDialog(IFileManipulateDialog *pDialog)
{
	if (this->activeManipDlgTab < this->manipDlgTabs.size() && 
		this->manipDlgTabs[this->activeManipDlgTab].pCurManipDlg.get() == pDialog &&
		pDialog != nullptr)
	{
		pDialog->onHide();
		this->manipDlgTabs[this->activeManipDlgTab].pCurManipDlg = nullptr;
		InvalidateRect(hDlg, NULL, TRUE); //TODO: Redraw only the manipulate dialog area.
	}
}
void MainWindow2::CloseUIFileEntry(FileEntryUIInfo *info, HWND hTree)
{
	auto cacheIt = disposableCacheElements.find(info);
	if (cacheIt != disposableCacheElements.end())
		disposableCacheElements.erase(cacheIt);

	if (hTree == NULL)
		hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);

	DeleteFileEntry_TreeItems(hTree, info);
	for (size_t iTab = 0; iTab < this->manipDlgTabs.size(); iTab++)
	{
		ManipDlgDesc &desc = this->manipDlgTabs[iTab];
		for (size_t iSel = 0; iSel < desc.selection.size(); iSel++)
		{
			bool tmp; 
			ITreeParameter *pCurSelection = desc.selection[iSel];
			if (pCurSelection && getEntryParam_FileEntryInfo(pCurSelection, tmp) == info)
			{
				if (desc.pCurManipDlg)
					desc.pCurManipDlg->removeFileContext(info);
				desc.selection[iSel] = nullptr;
				break;
			}
		}
	}

	//Remove references from the class database selection queue.
	for (auto it = fileEntriesPendingForDbSelection.begin(); it != fileEntriesPendingForDbSelection.end(); ++it)
	{
		if (it->pEntry == info)
			it->pEntry = nullptr;
	}
	//Additionally, cancel the active cldb selection dialog if it refers to a closed file.
	if (!fileEntriesPendingForDbSelection.empty() && fileEntriesPendingForDbSelection.front().pEntry == nullptr
		&& pSelectClassDbDialog != nullptr)
		pSelectClassDbDialog->ForceCancel();

	fileEntries.erase(info->myiter);
}
bool MainWindow2::fileHasUnappliedChanges(FileEntryUIInfo *pFileInfo)
{
	for (size_t iTab = 0; iTab < this->manipDlgTabs.size(); iTab++)
	{
		bool isSelectedInTab = false;
		ManipDlgDesc &desc = this->manipDlgTabs[iTab];
		if (!desc.pCurManipDlg)
			continue;
		for (size_t iSel = 0; iSel < desc.selection.size(); iSel++)
		{
			bool tmp; 
			ITreeParameter *pCurSelection = desc.selection[iSel];
			if (pCurSelection && getEntryParam_FileEntryInfo(pCurSelection, tmp) == pFileInfo)
			{
				isSelectedInTab = true;
				break;
			}
		}
		if (isSelectedInTab && desc.pCurManipDlg && desc.pCurManipDlg->hasUnappliedChanges())
			return true;
	}
	return false;
}
bool MainWindow2::fileHasUnsavedChanges(FileEntryUIInfo *pFileInfo)
{
	return pFileInfo->pContextInfo && pFileInfo->pContextInfo->hasNewChanges(*pContext);
}
//Returns true if the user chose to proceed anyway.
static bool askUserApplyChangeBeforeInstaller(HWND hParent)
{
	return IDYES == MessageBox(hParent,
		TEXT("There are unapplied changes in an open tab.\nDo you want to apply these changes before creating an installer?"),
		TEXT("Asset Bundle Extractor"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
}
//Returns true if the user chose to proceed anyway.
static bool warnUserOnUnappliedFileChange(HWND hParent, bool onSelectedFile)
{
	return IDYES == MessageBox(hParent,
		onSelectedFile ?
			TEXT("A tab that uses this file has unapplied changes.\nDo you want to proceed anyway?") :
			TEXT("A tab that uses an opened file has unapplied changes.\nDo you want to proceed anyway?"),
		TEXT("Asset Bundle Extractor"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
}
//Returns true if the user chose to proceed anyway.
static bool warnUserOnUnsavedFileChange(HWND hParent, bool onSelectedFile)
{
	return IDYES == MessageBox(hParent,
		onSelectedFile ? TEXT("The file has unapplied changes.\nDo you want to proceed anyway?")
		: TEXT("A file has unapplied changes.\nDo you want to proceed anyway?"),
		TEXT("Asset Bundle Extractor"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
}
bool MainWindow2::CloseFile(unsigned int fileID)
{
	FileContextInfo_ptr pContextInfo = this->pContext->getContextInfo(fileID);
	if (pContextInfo == nullptr)
		return false;
	auto fileUIEntryIt = this->fileEntriesByContextInfo.find(pContextInfo.get());
	if (fileUIEntryIt == this->fileEntriesByContextInfo.end())
		return false;
	return CloseFile(fileUIEntryIt->second);
}
bool MainWindow2::CloseFile(FileEntryUIInfo *info, HWND hTree)
{
	if (hTree == NULL)
		hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
	bool choseToProceedUnapplied = false, choseToProceedUnsaved = false;
	if (!choseToProceedUnapplied && fileHasUnappliedChanges(info))
	{
		if (warnUserOnUnappliedFileChange(this->hDlg, true))
			choseToProceedUnapplied = true;
		else
			return false;
	}
	if (!choseToProceedUnsaved && fileHasUnsavedChanges(info))
	{
		if (warnUserOnUnsavedFileChange(this->hDlg, true))
			choseToProceedUnsaved = true;
		else
			return false;
	}
	std::list<FileEntryUIInfo*> selectedEntries;
	selectedEntries.push_back(info);
	for (auto it = selectedEntries.begin(); it != selectedEntries.end(); )
	{
		FileEntryUIInfo *pEntryInfo = *it;
		if (pEntryInfo->pending)
		{
			bool taskRunning;
			if (this->pContext->taskManager.cancel(pEntryInfo->getTask(), &taskRunning) && !taskRunning)
			{
				//The task did not start and therefore will not issue a finish callback.
				pendingFileEntriesByTask.erase(pEntryInfo->getTask());
				delete pEntryInfo->getTask();
				
				CloseUIFileEntry(pEntryInfo, hTree);
			}
		}
		else
		{
			std::shared_ptr<FileContextInfo> pFileContextInfo = pEntryInfo->getContextInfo();
			if (pFileContextInfo)
			{
				std::vector<unsigned int> childFileIDs;
				pFileContextInfo->getChildFileIDs(childFileIDs);
				auto firstChildIt = it;
				for (size_t k = 0; k < childFileIDs.size(); k++)
				{
					if (childFileIDs[k] == 0)
						continue;
					//TODO: Handle pending child entries properly
					std::shared_ptr<FileContextInfo> pCurChild = pContext->getContextInfo(childFileIDs[k]);
					auto childEntryInfoIt = fileEntriesByContextInfo.find(pCurChild.get());
					if (childEntryInfoIt != fileEntriesByContextInfo.end())
					{
						//Note: If there are >2 nesting levels for opened files,
						//         we may end up having some of the sub-files closed even if the user decides to cancel now.
						if (!choseToProceedUnapplied && fileHasUnappliedChanges(childEntryInfoIt->second))
						{
							if (warnUserOnUnappliedFileChange(this->hDlg, true))
								choseToProceedUnapplied = true;
							else
								return false;
						}
						if (!choseToProceedUnsaved && fileHasUnsavedChanges(childEntryInfoIt->second))
						{
							if (warnUserOnUnsavedFileChange(this->hDlg, true))
								choseToProceedUnsaved = true;
							else
								return false;
						}

						//Insert before firstChildIt and update firstChildIt.
						firstChildIt = selectedEntries.insert(firstChildIt, childEntryInfoIt->second);
					}
				}
				if (firstChildIt != it)
				{
					//If this entry has children, close those first. This entry will be revisited afterwards.
					it = firstChildIt;
					continue;
				}
				if (pFileContextInfo->getFileContext())
				{
					EFileContextType type = pFileContextInfo->getFileContext()->getType();
					if (type >= (EFileContextType)0 && type < FileContext_COUNT)
					{
						assert(this->fileEntryCountersByType[type] > 0);
						this->fileEntryCountersByType[type]--;
					}
				}
			}
			CloseUIFileEntry(pEntryInfo, hTree);

			if (pFileContextInfo)
			{
				pContext->RemoveContextInfo(pFileContextInfo.get());
			}
		}
		//No need to keep the old entry. 
		it = selectedEntries.erase(it);
	}
#ifdef _DEBUG
	assert(selectedEntries.empty());
#endif
	return true;
}
void MainWindow2::onCloseFileCommand()
{
	HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
	size_t nSelectionsLast = 0;
	bool continueClose = false;
	do
	{
		std::vector<MC_HTREELISTITEM> selections;
		MC_HTREELISTITEM selection = NULL;
		while ((selection = MCTreeList_GetNextSelection(hTree, selection)) != NULL)
			selections.push_back(selection);
		if (selections.empty())
			break;

		continueClose = false;
		for (size_t i = 0; i < selections.size(); i++)
		{
			selection = selections[i];
			setSelectItem(hTree, selection, false);

			ITreeParameter *pEntryParam = getEntryParam(hTree, selection);
			bool isCloseable;
			FileEntryUIInfo *pEntryInfo = getEntryParam_FileEntryInfo(pEntryParam, isCloseable);
			
			if (pEntryInfo)
			{
				size_t oldFileCount = fileEntries.size();
				if (isCloseable && !CloseFile(pEntryInfo, hTree))
					return;
				if (fileEntries.size() != oldFileCount)
				{
					continueClose = true;
					break;
				}
			}
			else
			{
				if (isCloseable)
				{
					MCTreeList_DeleteItem(hTree, selection);
					continueClose = true;
					break;
				}
			}

		}
	} while (continueClose);
}
bool MainWindow2::onCloseProgramCommand()
{
	bool choseToProceedUnapplied = false, choseToProceedUnsaved = false;
	for (FileEntryUIInfo& entryInfo : getFileEntries())
	{
		if (!choseToProceedUnapplied && fileHasUnappliedChanges(&entryInfo))
		{
			if (warnUserOnUnappliedFileChange(this->hDlg, false))
				choseToProceedUnapplied = true;
			else
				return false;
		}
		if (!choseToProceedUnsaved && fileHasUnsavedChanges(&entryInfo))
		{
			if (warnUserOnUnsavedFileChange(this->hDlg, false))
				choseToProceedUnsaved = true;
			else
				return false;
		}
	}
	return true;
}

void MainWindow2::onSaveFileRequest(FileEntryUIInfo *pUIInfo)
{
	assert(pUIInfo != nullptr);
	if (pUIInfo->pContextInfo && pUIInfo->pContextInfo->hasAnyChanges(*this->pContext))
	{
		std::string defaultFilePath = pUIInfo->pContextInfo->getFileContext()->getFilePath();
		if (!defaultFilePath.empty())
				defaultFilePath += "-mod";
		if (pUIInfo->pContextInfo->getParentFileID() != 0)
		{
			//Ignore bundled files here.
		}
		else
		{
			size_t tmp;
			TCHAR *defaultFilePathT = (defaultFilePath.empty() ? nullptr : _MultiByteToTCHAR(defaultFilePath.c_str(), tmp));
			WCHAR *saveFilePath = nullptr;
			HRESULT hr = ShowFileSaveDialog(this->hDlg, &saveFilePath,
				TEXT("*.*|File:"), nullptr, defaultFilePathT,
				TEXT("Save changes"),
				UABE_FILEDIALOG_FILE_GUID);
			if (defaultFilePathT != nullptr)
				_FreeTCHAR(defaultFilePathT);
			if (SUCCEEDED(hr))
			{
				IAssetsWriter *pWriter = Create_AssetsWriterToFile(saveFilePath, true, true, RWOpenFlags_Immediately);
				FreeCOMFilePathBuf(&saveFilePath);
				if (pWriter == nullptr)
					MessageBox(this->hDlg, TEXT("Unable to open the selected file for writing!"), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
				else
				{
					uint64_t size = pUIInfo->pContextInfo->write(*this->pContext, pWriter, 0, true);
					Free_AssetsWriter(pWriter);
					switch (pUIInfo->pContextInfo->getFileContext()->getType())
					{
					case FileContext_Assets:
					case FileContext_Bundle:
						if (size == 0)
							MessageBox(this->hDlg, TEXT("An error occured while saving the file!"), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
						break;
					}
				}
			}
		}
	}
}

static std::string formatNameFor(FileContextInfo *pContextInfo, FileEntryUIInfo *pUIInfo)
{
	if (pContextInfo == nullptr || pContextInfo->getFileContext() == nullptr)
		return std::string("Pending : ") + pUIInfo->getShortName();
	assert(!pUIInfo->pending);
	assert(pUIInfo->pContextInfo.get() == pContextInfo);
	const char *nameSuffix = "";
	switch (pContextInfo->getFileContext()->getType())
	{
	case FileContext_Assets:
		nameSuffix = " (Assets)";
		break;
	case FileContext_Bundle:
		nameSuffix = " (Bundle)";
		break;
	case FileContext_Resources:
		nameSuffix = " (Resources)";
		break;
	case FileContext_Generic:
		nameSuffix = " (Generic)";
		break;
	}
	return std::string(pUIInfo->getShortName()) + nameSuffix;
}

void MainWindow2::updateBundleEntryName(BundleFileContextInfo *pBundleInfo, size_t bundleEntryIdx, std::string newName)
{
	std::vector<unsigned int> childFileIDs;
	pBundleInfo->getChildFileIDs(childFileIDs);
	if (bundleEntryIdx >= childFileIDs.size())
		return;
	FileEntryUIInfo *pEntry = nullptr;
	FileContextInfo_ptr pChildInfo = nullptr;
	if (childFileIDs[bundleEntryIdx] != 0)
	{
		pChildInfo = this->pContext->getContextInfo(childFileIDs[bundleEntryIdx]);
		if (pChildInfo != nullptr)
		{
			auto uiEntryIt = fileEntriesByContextInfo.find(pChildInfo.get());
			if (uiEntryIt != fileEntriesByContextInfo.end())
				pEntry = uiEntryIt->second;
		}
	}
	else
	{
		for (auto it = pendingFileEntriesByTask.begin(); it != pendingFileEntriesByTask.end(); ++it)
		{
			auto pFileOpenTask = dynamic_cast<AppContext::FileOpenTask*>(it->first);
			if (pFileOpenTask->parentFileID == pBundleInfo->getFileID() && pFileOpenTask->directoryEntryIdx == bundleEntryIdx)
			{
				pEntry = it->second;
				break;
			}
		}
	}
	if (pEntry == nullptr)
		return;
	pEntry->updateName(std::move(newName));
	
	HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
	updateEntryName(hTree, pEntry->hTreeItem, formatNameFor(pChildInfo.get(), pEntry));
}

bool MainWindow2::OnFileEntryLoadSuccess(ITask *pTask, std::shared_ptr<FileContextInfo> &pContextInfo, TaskResult result)
{
	if (getMenu() != NULL)
		EnableMenuItem(getMenu(), IDM_FILE_SAVEALL, MF_ENABLED);
	auto entryIt = pendingFileEntriesByTask.find(pTask);
	if (entryIt == pendingFileEntriesByTask.end()
		&& pTask != nullptr && pContextInfo != nullptr && pContextInfo->getFileContext()
		&& pContextInfo->getParentFileID() == 0)
	{
		this->addPendingBaseFileEntry(pTask, pContextInfo->getFileContext()->getFilePath());
		entryIt = pendingFileEntriesByTask.find(pTask);
	}
	if (entryIt != pendingFileEntriesByTask.end())
	{
		FileEntryUIInfo &entry = *(entryIt->second);
		pendingFileEntriesByTask.erase(pTask);
		entry.failed = false;
		entry.pending = false;
		entry.pContextInfo = nullptr;
		entry.setContextInfo(pContextInfo);
		//entry.pContextInfo = pContextInfo;
		//pContextInfo->incRef(); //Reference from FileEntryUIInfo
		fileEntriesByContextInfo.insert(std::make_pair(pContextInfo.get(), &entry));
		
		HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);

		setEntryFileID(hTree, entry.hTreeItem, pContextInfo->getFileID());
		IFileContext *pFileContext = pContextInfo->getFileContext();
		switch (pFileContext->getType())
		{
		case FileContext_Assets:
			{
				EAssetsFileOpenStatus openStatus = static_cast<EAssetsFileOpenStatus>(result);
				fileEntryCountersByType[FileContext_Assets]++;

				
				assert(entry.standardDialogsCount == 0);
				assert(entry.standardDialogsCount < entry.standardDialogs.size());
				entry.standardDialogsCount = 0;
				FileManipulateDialogInfo &assetsDialog = entry.standardDialogs[entry.standardDialogsCount++];
				assetsDialog.hTreeItem = entry.hTreeItem;
				assetsDialog.pEntry = &entry;
				assetsDialog.type = FileManipulateDialog_AssetList;
				updateEntryInfoRef(hTree, assetsDialog.hTreeItem, assetsDialog); //Intentional so the tree item is linked to the dialog action.
				
				assert(entry.standardDialogsCount < entry.standardDialogs.size());
				FileManipulateDialogInfo &dependenciesDialog = entry.standardDialogs[entry.standardDialogsCount++];
				dependenciesDialog.hTreeItem = insertEntry(hTree, entry.hTreeItem, std::string("Dependencies"));
				dependenciesDialog.pEntry = &entry;
				dependenciesDialog.type = FileManipulateDialog_AssetsDependencies;
				updateEntryInfoRef(hTree, dependenciesDialog.hTreeItem, dependenciesDialog);
					
				assert(entry.standardDialogsCount < entry.standardDialogs.size());
				FileManipulateDialogInfo &containersDialog = entry.standardDialogs[entry.standardDialogsCount++];
				containersDialog.hTreeItem = insertEntry(hTree, entry.hTreeItem, std::string("Containers"));
				containersDialog.pEntry = &entry;
				containersDialog.type = FileManipulateDialog_AssetsContainers;
				updateEntryInfoRef(hTree, containersDialog.hTreeItem, containersDialog);
				
				assert(entry.standardDialogsCount < entry.standardDialogs.size());
				FileManipulateDialogInfo &altAssetsDialog = entry.standardDialogs[entry.standardDialogsCount++];
				altAssetsDialog.hTreeItem = insertEntry(hTree, entry.hTreeItem, std::string("Assets"));
				altAssetsDialog.pEntry = &entry;
				altAssetsDialog.type = FileManipulateDialog_AssetList;
				updateEntryInfoRef(hTree, altAssetsDialog.hTreeItem, altAssetsDialog);
			}
			break;
		case FileContext_Bundle:
			{
				EBundleFileOpenStatus openStatus = static_cast<EBundleFileOpenStatus>(result);
				std::shared_ptr<BundleFileContextInfo> pBundleInfo = std::static_pointer_cast<BundleFileContextInfo, FileContextInfo>(pContextInfo);

				if (openStatus == BundleFileOpenStatus_CompressedDirectory ||
					openStatus == BundleFileOpenStatus_CompressedData)
				{
					entry.pending = true;
					entry.pContextInfo = nullptr;
					fileEntriesByContextInfo.erase(pContextInfo.get());

					if (decompressTargetDir.empty())
					{
						//Let the user select a decompression output directory.
						WCHAR *folderPath = nullptr;
						if (decompressTargetDir_cancel ||
							!ShowFolderSelectDialog(this->hDlg, &folderPath, L"Select a decompression output directory", UABE_FILEDIALOG_FILE_GUID))
						{
							decompressTargetDir_cancel = (pendingFileEntriesByTask.empty()) ? false : true;
							updateEntryName(hTree, entry.hTreeItem, std::string("Failed : ") + entry.getShortName() + " (Compressed Bundle)");
							entry.failed = true;
							entry.pending = false;
							entry.openLogText += "Decompression was cancelled\n";
							return false; //Remove the bundle from the AppContext.
						}
						auto folderPathUTF8 = unique_WideToMultiByte(folderPath);
						decompressTargetDir_cancel = false;
						decompressTargetDir.assign(folderPathUTF8.get());
						FreeCOMFilePathBuf(&folderPath);
					}
					updateEntryName(hTree, entry.hTreeItem, std::string(entry.getShortName()) + " (Compressed Bundle)");
					std::shared_ptr<ITask> pDecompressTask = pBundleInfo->EnqueueDecompressTask(*pContext, pBundleInfo,
						decompressTargetDir + "\\" + pBundleInfo->getFileName() + "-decompressed");
					if (pendingFileEntriesByTask.empty())
						decompressTargetDir.clear();
					if (pDecompressTask == nullptr)
					{
						entry.failed = true;
						entry.pending = false;
						updateEntryName(hTree, entry.hTreeItem, std::string("Failed : ") + entry.getShortName() + " (Compressed Bundle)");
						entry.openLogText += "Failed to enqueue decompression\n";
						return false; //Remove the bundle from the AppContext.
					}
					else
					{
						pendingFileEntriesByTask.insert(std::make_pair(pDecompressTask.get(), &entry));
					}

					return true;
				}

				fileEntryCountersByType[FileContext_Bundle]++;

				BundleFileContext *pBundleContext = pBundleInfo->getBundleFileContext();
				if (pBundleInfo->getEntryCount() > 0)
					setHasChildren(hTree, entry.hTreeItem, true);
				assert(pBundleInfo->getEntryCount() <= UINT_MAX);
				for (size_t i = 0; i < pBundleInfo->getEntryCount(); i++)
				{
					this->loadBundleEntry(pBundleInfo, (unsigned int)i);
				}

				assert(entry.standardDialogsCount == 0);
				assert(entry.standardDialogsCount < entry.standardDialogs.size());
				entry.standardDialogsCount = 0;
				FileManipulateDialogInfo &bundleDialog = entry.standardDialogs[entry.standardDialogsCount++];
				bundleDialog.hTreeItem = entry.hTreeItem;
				bundleDialog.pEntry = &entry;
				bundleDialog.type = FileManipulateDialog_Bundle;
				updateEntryInfoRef(hTree, bundleDialog.hTreeItem, bundleDialog); //Intentional so the tree item is linked to the dialog action.
			}
			break;
		case FileContext_Resources:
			{
				EResourcesFileOpenStatus openStatus = static_cast<EResourcesFileOpenStatus>(result);
				fileEntryCountersByType[FileContext_Resources]++;
			}
			break;
		case FileContext_Generic:
			{
				EGenericFileOpenStatus openStatus = static_cast<EGenericFileOpenStatus>(result);
				fileEntryCountersByType[FileContext_Generic]++;
			}
			break;
		default:
			break;
		}
		updateEntryName(hTree, entry.hTreeItem, formatNameFor(pContextInfo.get(), &entry));
		if (pendingFileEntriesByTask.empty())
		{
			//Forget the decompress target directory once all files are loaded.
			decompressTargetDir_cancel = false;
			decompressTargetDir.clear();
		}
		if (getSelectItem(hTree, entry.hTreeItem))
		{
			//If the item is selected and just finished loading, the manipulate dialog has to be created or notified.
			this->onChangeFileSelection();
		}
		else
		{
			//The selection checkboxes have to be updated in case a new file has loaded that is not selected.
			// -> deselect IDC_CKSELALL, make type-specific check boxes indeterminate.
			std::vector<ITreeParameter*> newSelection;
			MC_HTREELISTITEM selection = NULL;
			while ((selection = MCTreeList_GetNextSelection(hTree, selection)) != NULL)
			{
				ITreeParameter *pCurParam = getEntryParam(hTree, selection);
				newSelection.push_back(pCurParam);
			}
			this->doUpdateSelectionCheckboxes(newSelection);
		}
		return true;
	}
	return false;
}
void MainWindow2::OnFileEntryLoadFailure(ITask *pTask, std::string logText)
{
	auto entryIt = pendingFileEntriesByTask.find(pTask);
	if (entryIt != pendingFileEntriesByTask.end())
	{
		FileEntryUIInfo &entry = *(entryIt->second);
		pendingFileEntriesByTask.erase(pTask);
		entry.failed = true;
		entry.pending = false;
		entry.pContextInfo = nullptr;
		logText.swap(entry.openLogText);

		HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
		updateEntryName(hTree, entry.hTreeItem, std::string("Failed : ") + entry.getShortName());
	}
}
void MainWindow2::OnDecompressSuccess(BundleFileContextInfo::DecompressTask *pTask)
{
	std::shared_ptr<FileContextInfo> pContextInfo = std::static_pointer_cast<FileContextInfo, BundleFileContextInfo>(pTask->getFileContextInfo());
	if (!OnFileEntryLoadSuccess(pTask, pContextInfo, BundleFileOpenStatus_OK))
		pContext->RemoveContextInfo(pContextInfo.get());
}
void MainWindow2::OnDecompressFailure(BundleFileContextInfo::DecompressTask *pTask)
{
	auto entryIt = pendingFileEntriesByTask.find(pTask);
	if (entryIt != pendingFileEntriesByTask.end())
	{
		FileEntryUIInfo &entry = *(entryIt->second);
		pendingFileEntriesByTask.erase(pTask);
		entry.failed = true;
		entry.pending = false;
		entry.pContextInfo = nullptr;

		HWND hTree = GetDlgItem(this->hDlg, IDC_TREEFILES);
		updateEntryName(hTree, entry.hTreeItem, std::string("Failed : ") + entry.getShortName() + " (Compressed Bundle)");
	}
	pContext->RemoveContextInfo(pTask->getFileContextInfo().get());
}

MainWindow2::~MainWindow2(void)
{
	//Remove objects that may unregister main window event handlers during destruction
	// before the event handlers list is cleared.
	disposableCacheElements.clear();
	fileEntriesByContextInfo.clear();
	pendingFileEntriesByTask.clear();
	fileEntries.clear();
	manipDlgTabs.clear();

	pDialogFactory.reset();

	pStatusTracker.reset();

	assert(eventHandlers.empty());
}

IFileManipulateDialog::IFileManipulateDialog()
{}
IFileManipulateDialog::~IFileManipulateDialog()
{}
