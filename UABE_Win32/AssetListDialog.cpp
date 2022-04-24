#include "stdafx.h"
#include "resource.h"
#include "MainWindow2.h"
#include "Win32AppContext.h"
#include "AssetListDialog.h"
#include "AssetViewModifyDialog.h"
#include "FileDialog.h"
#include "ProgressDialog.h"
#include <tchar.h>
#include <Windows.h>
#include <WindowsX.h>
#include "Win32PluginManager.h"
#include "../libStringConverter/convert.h"
#include <format> //Note: For compiler support troubles, check out fmtlib https://github.com/fmtlib/fmt as a drop-in replacement.

AssetModifyDialog::AssetModifyDialog()
{}
AssetModifyDialog::~AssetModifyDialog()
{}

AssetListDialog::FileEntryCache::FileEntryCache(FileEntryUIInfo *pUIInfo, Win32AppContext *pContext)
	: pContext(pContext), pUIInfo(pUIInfo), nUsers(0), lastUseTime(0)
{
	pContext->getMainWindow().addDisposableCacheElement(pUIInfo, this);
	eventHandlerHandle = pContext->getMainWindow().registerEventHandler(this);
}
AssetListDialog::FileEntryCache::~FileEntryCache()
{
	pContext->getMainWindow().unregisterEventHandler(eventHandlerHandle);
}
size_t AssetListDialog::FileEntryCache::approxMemory()
{
	//Roughly approximate the memory usage.
	size_t i = 0;
	size_t ret = 0;
	//Retrieve the size of the first four cache elements (chosen 'randomly' by iterating through the unordered map).
	for (auto cacheIt = assetCache.begin(); cacheIt != assetCache.end(); ++cacheIt)
	{
		if (++i == 4)
			break;
		ret += cacheIt->second.getSize();
	}
	ret *= ((assetCache.size() + 3) & ~size_t(3)) / 4;
	ret += sizeof(FileEntryCache);
	return ret;
}
time_t AssetListDialog::FileEntryCache::getLastUseTime()
{
	return lastUseTime;
}
bool AssetListDialog::FileEntryCache::isInUse()
{
	return nUsers > 0;
}
void AssetListDialog::FileEntryCache::onUpdateContainers(AssetsFileContextInfo *pFile)
{
	if (pFile == pUIInfo->getContextInfoPtr())
	{
		//Invalidate all cache entries.
		std::unordered_map<pathid_t, AssetInfo> tmp;
		assetCache.swap(tmp);
	}
}
void AssetListDialog::FileEntryCache::onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved)
{
	FileContextInfo *pContextInfo = pUIInfo->getContextInfoPtr();
	if (pContextInfo == nullptr)
	{
		//Any cached element is invalid
		std::unordered_map<pathid_t, AssetInfo> tmp;
		assetCache.swap(tmp);
	}
	else if (pFile->getFileID() == pContextInfo->getFileID())
	{
		auto cacheIt = assetCache.find(pathID);
		if (cacheIt != assetCache.end())
		{
			assetCache.erase(cacheIt); //Cache entry needs an update
		}
	}
}

AssetListDialog::~AssetListDialog()
{
	pContext->getMainWindow().unregisterEventHandler(eventHandlerHandle);
}
AssetListDialog::AssetListDialog(class Win32AppContext *pContext, HWND hParentWnd)
	: pContext(pContext), hParentWnd(hParentWnd), hDialog(NULL),
	cachedListEntryCount(0), cachedListEntryStartIdx(0), maxEntriesPerTick(100), ticksUntilCacheFreqUpdate(0)
{
	eventHandlerHandle = pContext->getMainWindow().registerEventHandler(this);
	sorted = false;
	sortOrderAscending = false;
	iSortColumn = 0;
	iFocusedItem = -1;
	iLastTopItem = -1;
	qpfrequency.QuadPart = 1;
}
EFileManipulateDialogType AssetListDialog::getType()
{
	return FileManipulateDialog_AssetList;
}
void AssetListDialog::addFileContext(const std::pair<FileEntryUIInfo*,uintptr_t> &fileContext)
{
	FileContextInfo *pContextInfo = fileContext.first->getContextInfoPtr();
	if (AssetsFileContextInfo *pAssetsInfo = dynamic_cast<AssetsFileContextInfo*>(pContextInfo))
	{
		unsigned int fileID = pAssetsInfo->getFileID();
		auto entryIt = fileEntries.find(fileID);
		assert(entryIt == fileEntries.end());
		if (entryIt == fileEntries.end())
		{
			FileEntryCacheRef cachedRef = pContext->getMainWindow().findDisposableCacheElement<FileEntryCache>(fileContext.first);
			if (cachedRef.get() != nullptr)
			{
				cachedRef->nUsers++;
				assert(cachedRef->nUsers >= 1);
				fileEntries[fileID] = std::move(cachedRef);
			}
			else
			{
				FileEntryCache *pNewCache = new FileEntryCache(fileContext.first, pContext);
				pNewCache->nUsers++;
				assert(pNewCache->nUsers >= 1);
				fileEntries[fileID] = FileEntryCacheRef(pNewCache);
			}
			AssetIdentifier identifier;
			size_t start = listEntries.size();
			for (AssetIterator iter(pAssetsInfo); !iter.isEnd(); ++iter)
			{
				iter.get(identifier);
				listEntries.push_back(ListEntry(fileID, identifier.pathID));
			}
			if (hDialog)
				ListView_SetItemCount(GetDlgItem(hDialog, IDC_ASSETLIST), (int)std::min<size_t>(listEntries.size(), INT_MAX));
			if (sorted)
				resort();
			if (!entryCachingScheduled)
			{
				entryCachingScheduled = true;
				SetTimer(hDialog, (uintptr_t)1, 16, NULL);
			}
		}
	}
}
void AssetListDialog::removeFileContext(FileEntryUIInfo *pEntryInfo)
{
	//No need to update the sort order.
	unsigned int fileID = 0;
	auto entryIt = fileEntries.end();
	if (pEntryInfo->pContextInfo != nullptr)
	{
		entryIt = fileEntries.find(pEntryInfo->pContextInfo->getFileID());
	}
	else
	{
		for (auto curEntryIt = fileEntries.begin(); curEntryIt != fileEntries.end(); ++curEntryIt)
		{
			if (curEntryIt->second->pUIInfo == pEntryInfo)
			{
				entryIt = curEntryIt;
				break;
			}
		}
	}
	if (entryIt != fileEntries.end())
	{
		fileID = entryIt->first;
		entryIt->second->lastUseTime = time(nullptr);
		assert(entryIt->second->nUsers >= 1);
		entryIt->second->nUsers--;
		fileEntries.erase(entryIt);
	}
	HWND hAssetListView = NULL;
	if (hDialog)
		hAssetListView = GetDlgItem(hDialog, IDC_ASSETLIST);
	if (fileID != 0)
	{
		{
			auto deferredChangesIt = deferredChangesByFileID.find(fileID);
			if (deferredChangesIt != deferredChangesByFileID.end())
				deferredChangesByFileID.erase(deferredChangesIt);
		}
		//Delete the list entries for this file.
		//Assuming that listEntries only consists of trivially destructible types (allowed : std::tuple<void*,int>, not allowed : std::vector<int>).
		size_t moveOffset = 0;
		if (hAssetListView)
			ListView_SetItemCount(hAssetListView, 0);
			//ListView_DeleteAllItems(hAssetListView);
		for (size_t i = 0; i < listEntries.size() - moveOffset; i++)
		{
			while ((i + moveOffset) < listEntries.size() && listEntries[i + moveOffset].fileID == fileID)
			{
				//if (hAssetListView && (i + moveOffset) <= INT_MAX)
				//	ListView_DeleteItem(hAssetListView, (i + moveOffset));
				moveOffset++;
			}
			if (moveOffset > 0 && (i + moveOffset) < listEntries.size())
				listEntries[i] = std::move(listEntries[i + moveOffset]);
		}
		if (moveOffset > 0)
		{
			listEntries.erase(listEntries.begin() + (listEntries.size() - moveOffset), listEntries.end());
		}
		if (cachedListEntryCount >= moveOffset)
			cachedListEntryCount -= moveOffset;
		else
			cachedListEntryCount = 0;
	}
	if (hAssetListView)
		ListView_SetItemCount(hAssetListView, static_cast<int>(std::min<size_t>(listEntries.size(), INT_MAX)));

	if (fileEntries.empty())
		this->pContext->getMainWindow().hideManipulateDialog(this);
}
HWND AssetListDialog::getWindowHandle()
{
	return hDialog;
}
//Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
bool AssetListDialog::onCommand(WPARAM wParam, LPARAM lParam)
{
	int wmId = LOWORD(wParam);
	if (wmId == IDM_FILE_SAVEALL || !this->hDialog)
	{
		bool applyAll = (wmId == IDM_FILE_SAVEALL);
		if (wmId == IDM_FILE_SAVEALL)
			wmId = IDM_FILE_APPLY;
		bool ret = false;
		for (auto dialogIt = this->modifyDialogs.begin(); dialogIt != this->modifyDialogs.end(); ++dialogIt)
		{
			if (*dialogIt)
			{
				if (applyAll)
				{
					(*dialogIt)->applyChanges();
					ret = true;
				}
				else
					ret = ret || (*dialogIt)->onCommand(wParam, lParam);
			}
		}
		if (hDialog)
			ret = ret || (AssetListProc(hDialog, WM_COMMAND, wParam, lParam) == (INT_PTR)TRUE);
		return ret;
	}
	else
	{
		HWND hTabsControl = GetDlgItem(this->hDialog, IDC_ASSETLISTMODIFYTABS);
		int curTab = (int)SendMessage(hTabsControl, MC_MTM_GETCURSEL, 0, 0);
		if (curTab != -1)
		{
			MC_MTITEM item = {};
			item.dwMask = MC_MTIF_PARAM;
			if (SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)curTab, (LPARAM)&item) == TRUE)
			{
				if (item.lParam != 0)
				{
					AssetModifyDialog *pModifyDlg = reinterpret_cast<AssetModifyDialog*>(item.lParam);
					if (wmId == IDM_FILE_APPLY)
					{
						pModifyDlg->applyChanges();
						return true;
					}
					return pModifyDlg->onCommand(wParam, lParam);
				}
				else
					return AssetListProc(hDialog, WM_COMMAND, wParam, lParam) == (INT_PTR)TRUE;
			}
		}
	}
	return false;
}
//message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
void AssetListDialog::onHotkey(ULONG message, DWORD keyCode)
{
	if (message == WM_KEYDOWN && keyCode == VK_F3)
	{
		this->searchNext();
	}
}

void AssetListDialog::onUpdateContainers(AssetsFileContextInfo *pFile)
{
	unsigned int fileID = pFile->getFileID();
	auto entryIt = fileEntries.find(pFile->getFileID());
	if (entryIt != fileEntries.end())
	{
		FileEntryCache &entryCache = *entryIt->second.get();
		if (entryCache.assetCache.size() > 0 && entryCache.assetCache.size() <= cachedListEntryCount)
			cachedListEntryCount -= entryCache.assetCache.size();
		else
		{
			//The amount of discarded cache elements is unknown, since the FileEntryCache may have cleared itself already.
			cachedListEntryCount = 0; 
		}
		if (hDialog)
		{
			PostMessage(GetDlgItem(hDialog, IDC_ASSETLIST), LVM_REDRAWITEMS, 0, (LPARAM)(int)std::min<size_t>(listEntries.size(), INT_MAX));
			//ListView_RedrawItems(GetDlgItem(hDialog, IDC_ASSETLIST), 0, (int)std::min<size_t>(listEntries.size(), INT_MAX));
			if (!windowUpdateScheduled)
			{
				windowUpdateScheduled = true;
				SetTimer(hDialog, (uintptr_t)0, 50, NULL);
			}
		}
	}
}

void AssetListDialog::applyDeferredChanges()
{
	auto deferredChangesByFileID = std::move(this->deferredChangesByFileID);
	size_t totalChanges = 0;
	for (auto& changesVal : deferredChangesByFileID)
		totalChanges += changesVal.second.size();
	if (totalChanges == 0)
		return;
	//Generate a hash table that lists all changes,
	// since about listEntries.size()*3 steps are much less than O(listEntries.size()^2) steps
	// which would occur if a change notification was posted for every single entry.
	// (Could be even worse in theory, if several change notifications per asset occured between two calls).
	auto hasher = [](const std::pair<unsigned int/*fileID*/, pathid_t>& entry)
	{
		static std::hash<decltype(entry.first)> fidHasher;
		static std::hash<decltype(entry.second)> pidHasher;
		size_t fidHash = fidHasher(entry.first);
		size_t pidHash = pidHasher(entry.second);
		return (fidHash << 1) ^ pidHash;
	};
	std::unordered_map<std::pair<unsigned int/*fileID*/, pathid_t>, bool/*wasRemoved*/, decltype(hasher)> changesLookup(totalChanges);
	for (auto changesForFileIDIt = deferredChangesByFileID.begin(); changesForFileIDIt != deferredChangesByFileID.end(); ++changesForFileIDIt)
	{
		unsigned int fileID = changesForFileIDIt->first;
		for (DeferredChangeDesc& changeDesc : changesForFileIDIt->second)
			changesLookup.insert({ {fileID, changeDesc.pathID}, changeDesc.wasRemoved });
	}

	std::vector<ListEntry> newListEntries;
	newListEntries.reserve(this->listEntries.size());

	size_t iChangedFirst = SIZE_MAX;

	std::vector<std::pair<ListEntry, size_t/*outIndexHint*/>> entriesToSort;
	size_t numEntriesToSortWithOldIdx = 0;
	size_t iCurOutIndexIfUnsorted = 0;
	//First, fill newListEntries with all unchanged entries from this->listEntries. 
	for (size_t i = 0; i < this->listEntries.size(); i++)
	{
		bool keepEntry = true;
		auto lookupEntryIt = changesLookup.find({ this->listEntries[i].fileID, this->listEntries[i].pathID });
		if (lookupEntryIt != changesLookup.end())
		{
			if (lookupEntryIt->second) //wasRemoved
			{
				keepEntry = false;
				if (cachedListEntryCount > i)
					cachedListEntryCount--;
			}
			else
			{
				keepEntry = false;
				entriesToSort.push_back({ this->listEntries[i], iCurOutIndexIfUnsorted });
				iCurOutIndexIfUnsorted++;
				numEntriesToSortWithOldIdx++;
			}
			changesLookup.erase(lookupEntryIt);
		}
		if (keepEntry)
		{
			newListEntries.push_back(this->listEntries[i]);
			iCurOutIndexIfUnsorted++;
		}
		else
		{
			if (iChangedFirst == SIZE_MAX)
				iChangedFirst = i;
		}
	}
	for (auto& newAssetLookupEntry : changesLookup)
		entriesToSort.push_back({ ListEntry(newAssetLookupEntry.first.first, newAssetLookupEntry.first.second), SIZE_MAX });
	this->listEntries = std::move(newListEntries);
	if (hDialog)
		ListView_SetItemCount(GetDlgItem(hDialog, IDC_ASSETLIST), (int)std::min<size_t>(listEntries.size(), INT_MAX));
	if (entriesToSort.size() > 100) 
	{
		//Since inserting many items into a vector at random locations is slow (O(nlogn+n^2)),
		// insert all at the end and then sort the whole vector if needed (O(n+nlogn)).
		//Crude check to limit the worst case time overhead.
		for (std::pair<ListEntry, size_t/*outIndexHint*/>& listEntryToInsert : entriesToSort)
			listEntries.push_back(listEntryToInsert.first);
		if (hDialog)
			ListView_SetItemCount(GetDlgItem(hDialog, IDC_ASSETLIST), (int)std::min<size_t>(listEntries.size(), INT_MAX));
		if (sorted)
			resort();
	}
	else
	{
		//More user-friendly insertion method (more stable entry locations),
		// but slower depending on the size of listEntries.
		//Note: Each listEntryInsertSorted may also invoke ListView_RedrawItems.
		for (std::pair<ListEntry, size_t/*outIndexHint*/>& listEntryToInsert : entriesToSort)
		{
			listEntryInsertSorted(listEntryToInsert.first, listEntryToInsert.second);
		}
	}
	if (hDialog)
		ListView_SetItemCount(GetDlgItem(hDialog, IDC_ASSETLIST), (int)std::min<size_t>(listEntries.size(), INT_MAX));
	if (hDialog && iChangedFirst < INT_MAX)
		ListView_RedrawItems(GetDlgItem(hDialog, IDC_ASSETLIST), iChangedFirst, (int)std::min<size_t>(listEntries.size(), INT_MAX));
}
void AssetListDialog::onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved)
{
	//Called whenever a replacer was added, even if the dialog is hidden (as long as it has the file context).
	unsigned int fileID = pFile->getFileID();
	this->deferredChangesByFileID[fileID].push_back(DeferredChangeDesc{ pathID, wasRemoved });
	//static_cast<AssetsFileContext*>(pFile->getFileContext())->getAssetsFileTable()->getAssetInfo(pathID)
	if (hDialog && !windowUpdateScheduled)
	{
		windowUpdateScheduled = true;
		SetTimer(hDialog, (uintptr_t)0, 50, NULL);
	}
	if (hDialog && !selectionUpdateScheduled)
	{
		selectionUpdateScheduled = true;
		SetTimer(hDialog, (uintptr_t)2, 50, NULL);
	}
}
void AssetListDialog::onHide()
{
	if (pActiveModifyDialog) pActiveModifyDialog->onHide();
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_ADDASSET, MF_GRAYED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_FILE_APPLY, MF_GRAYED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_SEARCHBYNAME, MF_GRAYED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_CONTINUESEARCH, MF_GRAYED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_GOTOASSET, MF_GRAYED);
	//EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_MODMAKER_CREATESTANDALONE, MF_GRAYED);
	//EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_MODMAKER_CREATEPACKAGE, MF_GRAYED);
	if (this->hDialog)
	{
		if (this->hCurPopupMenu != NULL)
		{
			DestroyMenu(this->hCurPopupMenu);
			this->hCurPopupMenu = NULL;
		}
		if (windowUpdateScheduled)
			KillTimer(this->hDialog, (uintptr_t)0);
		if (entryCachingScheduled)
			KillTimer(this->hDialog, (uintptr_t)1);
		if (selectionUpdateScheduled)
			KillTimer(this->hDialog, (uintptr_t)2);
		windowUpdateScheduled = false;
		entryCachingScheduled = false;
		selectionUpdateScheduled = false;
		
		HWND hTabsControl = GetDlgItem(this->hDialog, IDC_ASSETLISTMODIFYTABS);
		int curTab = (int)SendMessage(hTabsControl, MC_MTM_GETCURSEL, 0, 0);
		if (curTab != -1)
		{
			MC_MTITEM item = {};
			item.dwMask = MC_MTIF_PARAM;
			if (SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)curTab, (LPARAM)&item) == TRUE)
			{
				if (item.lParam != 0)
					reinterpret_cast<AssetModifyDialog*>(item.lParam)->onHide();
			}
		}

		HWND hAssetList = GetDlgItem(this->hDialog, IDC_ASSETLIST);
		this->iLastTopItem = ListView_GetTopIndex(hAssetList);

		SendMessage(this->hDialog, WM_CLOSE, 0, 0);
	}
}
void AssetListDialog::onShow()
{
	if (!this->hDialog)
		this->hDialog = CreateDialogParam(pContext->getMainWindow().getHInstance(), MAKEINTRESOURCE(IDD_ASSETSINFO), hParentWnd, AssetListProc, (LPARAM)this);

	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_ADDASSET, MF_ENABLED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_FILE_APPLY, MF_ENABLED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_SEARCHBYNAME, MF_ENABLED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_CONTINUESEARCH, 
		this->searchQuery.empty() ? MF_GRAYED : MF_ENABLED);
	EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_VIEW_GOTOASSET, MF_ENABLED);
	
	if (this->hDialog)
	{
		this->selectionUpdateScheduled = true;
		SetTimer(this->hDialog, (uintptr_t)2, 16, NULL);
	}
	//EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_MODMAKER_CREATESTANDALONE, MF_ENABLED);
	//EnableMenuItem(pContext->getMainWindow().getMenu(), IDM_MODMAKER_CREATEPACKAGE, MF_ENABLED);
}
bool AssetListDialog::hasUnappliedChanges(bool *applyable)
{
	bool ret = false;
	//Only the modify dialogs can have unapplied changes; AssetListDialog doesn't store any relevant saveable state.
	for (auto modifyDialogPtrIt = this->modifyDialogs.begin(); modifyDialogPtrIt != this->modifyDialogs.end(); ++modifyDialogPtrIt)
	{
		AssetModifyDialog *pDialog = modifyDialogPtrIt->get();
		bool curApplyable = false;
		if (pDialog && pDialog->hasUnappliedChanges(&curApplyable))
		{
			if (!applyable)
				return true;
			ret = true;
			if (curApplyable)
			{
				if (applyable) *applyable = true;
				return true;
			}
		}
	}
	return ret;
}
bool AssetListDialog::applyChanges()
{
	bool ret = true;
	for (auto modifyDialogPtrIt = this->modifyDialogs.begin(); modifyDialogPtrIt != this->modifyDialogs.end(); ++modifyDialogPtrIt)
	{
		AssetModifyDialog *pDialog = modifyDialogPtrIt->get();
		if (pDialog && !pDialog->applyChanges())
			ret = false; //Still apply changes of any other open dialog. Need to check whether this is good behavior, or if it could .
	}
	return ret;
}
bool AssetListDialog::doesPreferNoAutoclose()
{
	//Do not autoclose when:
	//1. At least one modify dialog is open.
	if (!this->modifyDialogs.empty()) return true;
	//2. More than one asset is selected.
	if (this->lastNumSelections > 1) return true;
	return false;
}

struct SortPred_Base
{
	AssetListDialog *pDlg;
	bool smallerThan; //ascending
	inline SortPred_Base(AssetListDialog *pDlg, bool smallerThan) : pDlg(pDlg), smallerThan(smallerThan) {}
};
struct SortPred_Name : SortPred_Base
{
	inline SortPred_Name(AssetListDialog *pDlg, bool smallerThan) : SortPred_Base(pDlg, smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		std::string *pNameA = pDlg->getName(a.fileID, a.pathID);
		std::string *pNameB = pDlg->getName(b.fileID, b.pathID);
		assert(pNameA && pNameB); //Should always be the case. 
		return (smallerThan ? ((*pNameA) < (*pNameB)) : ((*pNameA) > (*pNameB)));
	}
};
struct SortPred_ContainerName : SortPred_Base
{
	inline SortPred_ContainerName(AssetListDialog *pDlg, bool smallerThan) : SortPred_Base(pDlg, smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		std::string *pNameA = pDlg->getContainerName(a.fileID, a.pathID);
		std::string *pNameB = pDlg->getContainerName(b.fileID, b.pathID);
		assert(pNameA && pNameB); //Should always be the case. 
		return (smallerThan ? ((*pNameA) < (*pNameB)) : ((*pNameA) > (*pNameB)));
	}
};
struct SortPred_TypeName : SortPred_Base
{
	inline SortPred_TypeName(AssetListDialog *pDlg, bool smallerThan) : SortPred_Base(pDlg, smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		std::string *pNameA = pDlg->getTypeName(a.fileID, a.pathID);
		std::string *pNameB = pDlg->getTypeName(b.fileID, b.pathID);
		assert(pNameA && pNameB); //Should always be the case. 
		return (smallerThan ? ((*pNameA) < (*pNameB)) : ((*pNameA) > (*pNameB)));
	}
};
struct SortPred_FileID
{
	bool smallerThan;
	inline SortPred_FileID(bool smallerThan) : smallerThan(smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		return (smallerThan ? (a.fileID < b.fileID) : (a.fileID > b.fileID));
	}
};
struct SortPred_PathID
{
	bool smallerThan;
	inline SortPred_PathID(bool smallerThan) : smallerThan(smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		return (smallerThan ? ((int64_t)a.pathID < (int64_t)b.pathID) : ((int64_t)a.pathID > (int64_t)b.pathID));
	}
};
struct SortPred_Size : SortPred_Base
{
	inline SortPred_Size(AssetListDialog *pDlg, bool smallerThan) : SortPred_Base(pDlg, smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		auto sizeA = pDlg->getSize(a.fileID, a.pathID);
		auto sizeB = pDlg->getSize(b.fileID, b.pathID);
		return (smallerThan ? (sizeA < sizeB) : (sizeA > sizeB));
	}
};
struct SortPred_Modified : SortPred_Base
{
	inline SortPred_Modified(AssetListDialog *pDlg, bool smallerThan) : SortPred_Base(pDlg, smallerThan) {}
	bool operator()(const AssetListDialog::ListEntry &a, const AssetListDialog::ListEntry &b) const
	{
		bool modA = pDlg->getIsModified(a.fileID, a.pathID);
		bool modB = pDlg->getIsModified(b.fileID, b.pathID);
		return (smallerThan ? (!modA && modB) : (modA && !modB));
	}
};

template <class It, class Ty, class Pred>
static It upper_bound_or_target(It begin, It end, It target, const Ty& val, Pred pred)
{
	if (target != end)
	{
		if (!pred(*target, val) && !pred(val, *target)) //if (*target compares equal to val)
			return target;
	}
	return std::upper_bound(begin, end, val, pred);
}
bool AssetListDialog::cacheAllAndShowProgress(bool allowCancel)
{
	applyDeferredChanges();
	bool ret = true;
	AssetInfo* tmp;
	size_t start = 0;
	auto pProgressIndicator = std::make_shared<CProgressIndicator>(this->pContext->getMainWindow().getHInstance());
	bool indicatorStarted = false;
	while (start < listEntries.size())
	{
		if (pProgressIndicator && allowCancel && pProgressIndicator->IsCancelled())
		{
			ret = false;
			break;
		}
		if (start != 0 && pProgressIndicator)
		{
			if (!indicatorStarted)
			{
				if (!pProgressIndicator->Start(this->hDialog, pProgressIndicator, 2000))
				{
					pProgressIndicator.reset();
					continue;
				}
				pProgressIndicator->SetStepRange(0, (unsigned int)std::min<size_t>(INT_MAX, listEntries.size()));
				pProgressIndicator->SetCancellable(allowCancel);
				pProgressIndicator->SetTitle("Processing asset entries");
			}
			pProgressIndicator->SetDescription(std::format("Processing asset {} / {}", start + 1, listEntries.size()));
			pProgressIndicator->SetStepStatus((unsigned int)std::min<size_t>(INT_MAX, start));
		}
		cachedListEntryCount += cacheEntries(start, listEntries.size(), tmp, 431, &start);
	}
	if (pProgressIndicator != nullptr)
	{
		pProgressIndicator->End();
		pProgressIndicator->Free();
	}
	return ret;
}
void AssetListDialog::listEntryInsertSorted(ListEntry newEntry, size_t targetIdx)
{
	if (targetIdx == (size_t)-1 || targetIdx > listEntries.size())
		targetIdx = listEntries.size();
	size_t newEntryIndex = listEntries.size();
	if (sorted)
	{
		if (iSortColumn != 3 && iSortColumn != 4) //Most predicates require cached info, FileID and PathID being the exceptions.
		{
			//Generate the cache info for the new entry before inserting it at the right place.
			listEntries.push_back(newEntry);
			this->cacheAllAndShowProgress(false);
			listEntries.erase(listEntries.begin() + newEntryIndex);
		}
		auto targetIt = listEntries.begin() + targetIdx;
		switch (iSortColumn)
		{
			case 0: //Name
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_Name(this, sortOrderAscending)));
				break;
			case 1: //Container
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_ContainerName(this, sortOrderAscending)));
				break;
			case 2: //Type
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_TypeName(this, sortOrderAscending)));
				break;
			case 3: //File ID
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_FileID(sortOrderAscending)));
				break;
			case 4: //Path ID
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_PathID(sortOrderAscending)));
				break;
			case 5: //Size (Bytes)
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_Size(this, sortOrderAscending)));
				break;
			case 6: //Modified
				newEntryIndex = std::distance(listEntries.begin(), 
					upper_bound_or_target(listEntries.begin(), listEntries.end(), targetIt, newEntry, SortPred_Modified(this, sortOrderAscending)));
				break;
			default:
				sorted = false;
				break;
		}
	}
	listEntries.insert(listEntries.begin() + newEntryIndex, std::move(newEntry));
	if (iFocusedItem >= 0 && (size_t)iFocusedItem >= newEntryIndex && iFocusedItem < INT_MAX)
	{
		iFocusedItem++;
	}
	if (hDialog && newEntryIndex <= INT_MAX)
	{
		HWND hAssetList = GetDlgItem(hDialog, IDC_ASSETLIST);
		if (iFocusedItem != -1)
			ListView_SetItemState(hAssetList, iFocusedItem, LVIS_FOCUSED, LVIS_FOCUSED);
		ListView_RedrawItems(hAssetList, (int)newEntryIndex, (int)std::min<size_t>(listEntries.size(), INT_MAX));
		if (!windowUpdateScheduled)
		{
			windowUpdateScheduled = true;
			SetTimer(hDialog, (uintptr_t)0, 50, NULL);
		}
	}
}
void AssetListDialog::resort()
{
	unsigned int focusTarget_FileID = 0; pathid_t focusTarget_PathID = 0;
	if (iFocusedItem >= 0 && (size_t)iFocusedItem < listEntries.size())
	{
		focusTarget_FileID = listEntries[iFocusedItem].fileID;
		focusTarget_PathID = listEntries[iFocusedItem].pathID;
	}
	if (iSortColumn != 3 && iSortColumn != 4) //Most predicates require cached info, FileID and PathID being the exceptions.
	{
		if (!this->cacheAllAndShowProgress(true))
		{
			sorted = false;
			return;
		}
	}
	auto pProgressIndicator = std::make_shared<CProgressIndicator>(this->pContext->getMainWindow().getHInstance());
	if (pProgressIndicator->Start(this->hDialog, pProgressIndicator, 2000))
	{
		pProgressIndicator->SetStepRange(0, 0);
		pProgressIndicator->SetCancellable(false);
		pProgressIndicator->SetTitle("Sorting the asset list");
	}
	else
		pProgressIndicator.reset();
	bool noChanges = false;
	switch (iSortColumn)
	{
		case 0: //Name
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_Name(this, sortOrderAscending));
			break;
		case 1: //Container
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_ContainerName(this, sortOrderAscending));
			break;
		case 2: //Type
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_TypeName(this, sortOrderAscending));
			break;
		case 3: //File ID
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_FileID(sortOrderAscending));
			break;
		case 4: //Path ID
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_PathID(sortOrderAscending));
			break;
		case 5: //Size (Bytes)
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_Size(this, sortOrderAscending));
			break;
		case 6: //Modified
			std::stable_sort(listEntries.begin(), listEntries.end(), SortPred_Modified(this, sortOrderAscending));
			break;
		default:
			noChanges = true;
			break;
	}
	if (pProgressIndicator != nullptr)
	{
		pProgressIndicator->End();
		pProgressIndicator->Free();
		pProgressIndicator.reset();
	}
	if (noChanges)
		return;

	if (focusTarget_FileID != 0)
	{
		iFocusedItem = -1;
		for (size_t i = 0; i < listEntries.size() && i <= INT_MAX; i++)
		{
			if (listEntries[i].fileID == focusTarget_FileID && listEntries[i].pathID == focusTarget_PathID)
			{
				iFocusedItem = (int)i;
			}
		}
		assert(iFocusedItem != -1);
	}
	if (hDialog)
	{
		HWND hAssetList = GetDlgItem(hDialog, IDC_ASSETLIST);
		if (iFocusedItem != -1)
		{
			ListView_SetItemState(hAssetList, iFocusedItem, LVIS_FOCUSED, LVIS_FOCUSED);
			ListView_EnsureVisible(hAssetList, iFocusedItem, FALSE);
		}
		ListView_RedrawItems(hAssetList, 0, (int)std::min<size_t>(listEntries.size(), INT_MAX));
		if (!windowUpdateScheduled)
		{
			windowUpdateScheduled = true;
			SetTimer(hDialog, (uintptr_t)0, 50, NULL);
		}
	}
	sorted = true;
}

void AssetListDialog::getContainerInfo(AssetsFileContextInfo *pContextInfo, pathid_t pathID,
		OUT std::string &baseName, OUT std::string &containerListName)
{
	unsigned int fileID = pContextInfo->getFileID();
	bool hasContainerBase = false;
	uint64_t baseCount = 0;
	uint64_t dependantCount = 0;
	baseName.clear();
	containerListName.clear();
	std::string exampleContainerName;
	{
		AssetContainerList *pMainContainersList = pContextInfo->tryLockContainersRead();
		if (pMainContainersList)
		{
			std::vector<const ContainerEntry*> mainContainers = pMainContainersList->getContainers(0, pathID);
			if (mainContainers.size() > 0)
			{
				hasContainerBase = true;
				exampleContainerName = mainContainers[0]->name;
				baseCount += mainContainers.size();
			}
			std::vector<const ContainerEntry*> parentContainers = pMainContainersList->getParentContainers(0, pathID);
			if (parentContainers.size() > 0)
			{
				dependantCount += parentContainers.size();
				if (exampleContainerName.empty())
					exampleContainerName = parentContainers[0]->name;
			}
			pContextInfo->unlockContainersRead();
		}
	}
	for (size_t i = 0; i < pContextInfo->getContainerSources().size(); i++)
	{
		FileContextInfo_ptr pDepContextInfo = this->pContext->getContextInfo(pContextInfo->getContainerSources()[i]);
		if (pDepContextInfo != nullptr &&
			pDepContextInfo->getFileContext() &&
			pDepContextInfo->getFileContext()->getType() == FileContext_Assets)
		{
			AssetsFileContextInfo *pDepContextInfo_Assets = static_cast<AssetsFileContextInfo*>(pDepContextInfo.get());
			const std::vector<unsigned int> referenceFileIDs = pDepContextInfo_Assets->getReferences();
			auto ownIDIt = std::find(referenceFileIDs.begin(), referenceFileIDs.end(), fileID);
			if (ownIDIt != referenceFileIDs.end())
			{
				size_t relFileID = std::distance(referenceFileIDs.begin(), ownIDIt) + 1;
				assert(relFileID < UINT_MAX);
				
				AssetContainerList *pDepContainersList = pDepContextInfo_Assets->tryLockContainersRead();
				if (pDepContainersList)
				{
					std::vector<const ContainerEntry*> mainContainers = pDepContainersList->getContainers((unsigned int)relFileID, pathID);
					if (mainContainers.size() > 0)
					{
						if (!hasContainerBase || exampleContainerName.empty())
							exampleContainerName = mainContainers[0]->name;
						hasContainerBase = true;
						baseCount += mainContainers.size();
					}
					std::vector<const ContainerEntry*> parentContainers = pDepContainersList->getParentContainers((unsigned int)relFileID, pathID);
					if (parentContainers.size() > 0)
					{
						dependantCount += parentContainers.size();
						if (exampleContainerName.empty())
							exampleContainerName = parentContainers[0]->name;
					}
					pDepContextInfo_Assets->unlockContainersRead();
				}
			}
		}
	}
	char nameExt[128]; nameExt[0] = 0;
	if (hasContainerBase)
	{
		if (baseCount > 1)
			sprintf_s(nameExt, " (Base, %llu other base containers, %llu dependants)", baseCount - 1, dependantCount);
		else
			sprintf_s(nameExt, " (Base, %llu dependants)", dependantCount);
	}
	else if (!exampleContainerName.empty())
	{
		if (dependantCount > 1)
			sprintf_s(nameExt, " (and %llu other dependant containers)", dependantCount - 1);
		else
			strcpy_s(nameExt, " (dependant container)");
	}
	if (hasContainerBase)
	{
		auto slashIt = std::find(exampleContainerName.rbegin(), exampleContainerName.rend(), '/');
		if (slashIt != exampleContainerName.rend() && slashIt != exampleContainerName.rbegin())
			baseName.assign(slashIt.base(), exampleContainerName.end()); //<reverse_iterator>.base() returns a forward iterator for the next item (in forward direction).
		else
			baseName = exampleContainerName;
	}
	containerListName = std::move(exampleContainerName);
	if (nameExt[0] != 0)
		containerListName += nameExt;
}
bool AssetListDialog::TryRetrieveAssetNameField(AssetsFileContextInfo *pContextInfo, AssetIdentifier &identifier, std::string &nameOut,
	std::unordered_map<NameTypeCacheKey, NameTypeCacheValue> &nameTypesCache)
{
	if (identifier.pAssetInfo && !identifier.pReplacer)
	{
		IAssetsReader_ptr pReader(pContextInfo->getAssetsFileContext()->createReaderView(), Free_AssetsReader);

		if (identifier.pAssetInfo->ReadName(pContextInfo->getAssetsFileContext()->getAssetsFile(), nameOut, pReader.get()))
			return true;
	}
	bool ret = false;
	{
		NameTypeCacheValue *pNameTypeValue = nullptr;
		NameTypeCacheKey key(pContextInfo->getFileID(), identifier.getClassID(), identifier.getMonoScriptID());
		auto typeFileCacheIt = nameTypesCache.find(key);
		if (typeFileCacheIt != nameTypesCache.end())
			pNameTypeValue = &typeFileCacheIt->second;
		else
		{
			NameTypeCacheValue newValue;
			newValue.hasName = false;
			newValue.nameChildIdx = 0;
			if (pContextInfo->MakeTemplateField(&newValue.templateBase, *this->pContext, identifier.getClassID(), identifier.getMonoScriptID(), &identifier))
			{
				AssetTypeTemplateField &templateBase = newValue.templateBase;
				for (DWORD i = 0; i < templateBase.children.size(); i++)
				{
					if (templateBase.children[i].name == "m_Name"
						&& (templateBase.children[i].valueType == ValueType_String))
					{
						newValue.hasName = true;
						newValue.nameChildIdx = i;
						break;
					}
				}
			}
			pNameTypeValue = &(nameTypesCache[key] = std::move(newValue));
		}
		if (pNameTypeValue->hasName)
		{
			IAssetsReader_ptr pReader = identifier.makeReader();
			if (pReader)
			{
				AssetTypeTemplateField *pTemplateBase = &pNameTypeValue->templateBase;
				AssetTypeValueField *pBase;

				uint32_t origChildCount = (uint32_t)pNameTypeValue->templateBase.children.size();
				std::vector<AssetTypeTemplateField> childrenTmp = std::move(pNameTypeValue->templateBase.children);
				pNameTypeValue->templateBase.children.assign(childrenTmp.begin(), childrenTmp.begin() + (pNameTypeValue->nameChildIdx + 1));
				bool bigEndian = false;
				pContextInfo->getEndianness(bigEndian);
				AssetTypeInstance instance(1, &pTemplateBase, identifier.getDataSize(), pReader.get(), bigEndian);
				pBase = instance.GetBaseField();
				if (pBase)
				{
					AssetTypeValueField *pNameField = pBase->Get("m_Name");
					if (!pNameField->IsDummy() && pNameField->GetValue())
					{
						nameOut.assign(pNameField->GetValue()->AsString());
						ret = true;
					}
				}
				pNameTypeValue->templateBase.children = std::move(childrenTmp);
			}
		}
	}
	return ret;
}
size_t AssetListDialog::cacheEntries(size_t start, size_t end, AssetInfo *&pFirstEntry, size_t nMax, size_t *maxVisitedIndex)
{
	applyDeferredChanges();
	std::unordered_map<NameTypeCacheKey, NameTypeCacheValue> nameTypesCache;
	pFirstEntry = nullptr;
	if (maxVisitedIndex) *maxVisitedIndex = start;
	size_t newCachedEntries = 0;
	if (start < listEntries.size())
	{
		if (end > listEntries.size())
			end = listEntries.size();
		size_t i;
		for (i = start; i < end && nMax > 0; i++)
		{
			unsigned int fileID = listEntries[i].fileID;
			pathid_t pathID = listEntries[i].pathID;
			auto fileEntryIt = fileEntries.find(fileID);
			if (fileEntryIt != fileEntries.end())
			{
				auto cacheEntryIt = fileEntryIt->second->assetCache.find(pathID);
				if (cacheEntryIt == fileEntryIt->second->assetCache.end())
				{
					FileEntryUIInfo *pUIInfo = fileEntryIt->second->pUIInfo;
					AssetsFileContextInfo *pContextInfo = static_cast<AssetsFileContextInfo*>(pUIInfo->getContextInfoPtr());
					AssetIdentifier identifier(std::static_pointer_cast<AssetsFileContextInfo,FileContextInfo>(pUIInfo->pContextInfo), pathID);
					if (identifier.resolve(*this->pContext))
					{
						AssetInfo info;
						info.typeID = identifier.getClassID();
						info.monoScriptID = identifier.getMonoScriptID();
						info.isModified = (identifier.pReplacer != nullptr);
						info.size = identifier.getDataSize();
						getContainerInfo(pContextInfo, pathID, info.name, info.containerName);
						info.typeName = pContextInfo->GetClassName_(*this->pContext, info.typeID, info.monoScriptID, &identifier);
						if (info.name.empty())
							TryRetrieveAssetNameField(pContextInfo, identifier, info.name, nameTypesCache);
						if (info.typeName.empty())
						{
							char sprntTmp[20];
							sprintf_s(sprntTmp, "0x%08X", info.typeID);
							info.typeName.assign(sprntTmp);
						}
						auto newCacheEntryIt = fileEntryIt->second->assetCache.insert(
							fileEntryIt->second->assetCache.begin(), 
							std::make_pair(pathID, std::move(info)));
						if (!pFirstEntry)
							pFirstEntry = &newCacheEntryIt->second;
						newCachedEntries++;
						nMax--;
					}
				}
				else if (!pFirstEntry)
					pFirstEntry = &cacheEntryIt->second;
			}
		}
		if (maxVisitedIndex) *maxVisitedIndex = i;
	}
	return newCachedEntries;
}
void AssetListDialog::getEntryText(int iItem, int iSubItem, AssetInfo &entry, OUT std::unique_ptr<TCHAR[]> &newTextBuf)
{
	newTextBuf.reset();
	TCHAR sprntTmp[64];
	int sprntSize = 0;
	newTextBuf = nullptr;
	switch (iSubItem)
	{
		case 0: //Name
			{
				if (entry.name.empty())
					sprntSize = _stprintf_s(sprntTmp, TEXT("%s"), TEXT("Unnamed asset"));
				else
				{
					size_t nameLenT;
					TCHAR *pNameT = _MultiByteToTCHAR(entry.name.c_str(), nameLenT);
					if (pNameT)
					{
						newTextBuf.reset(new TCHAR[nameLenT + 1]);
						memcpy(newTextBuf.get(), pNameT, (nameLenT + 1) * sizeof(TCHAR));
						_FreeTCHAR(pNameT);
					}
				}
			}
			break;
		case 1: //Container
			{
				size_t nameLenT;
				TCHAR *pNameT = _MultiByteToTCHAR(entry.containerName.c_str(), nameLenT);
				if (pNameT)
				{
					newTextBuf.reset(new TCHAR[nameLenT + 1]);
					memcpy(newTextBuf.get(), pNameT, (nameLenT + 1) * sizeof(TCHAR));
					_FreeTCHAR(pNameT);
				}
			}
			break;
		case 2: //Type
			{
				size_t nameLenT;
				TCHAR *pNameT = _MultiByteToTCHAR(entry.typeName.c_str(), nameLenT);
				if (pNameT)
				{
					newTextBuf.reset(new TCHAR[nameLenT + 1]);
					memcpy(newTextBuf.get(), pNameT, (nameLenT + 1) * sizeof(TCHAR));
					_FreeTCHAR(pNameT);
				}
			}
			break;
		case 128: //Type (plus Hex typeID)
			{
				size_t nameLenT;
				TCHAR *pNameT = _MultiByteToTCHAR(entry.typeName.c_str(), nameLenT);
				if (pNameT)
				{
					int curSprntSize = _stprintf_s(sprntTmp, TEXT(" (0x%08X)"), entry.typeID);
					if (curSprntSize < 0) curSprntSize = 0;
					newTextBuf.reset(new TCHAR[nameLenT + curSprntSize + 1]);
					memcpy(newTextBuf.get(), pNameT, (nameLenT + 1) * sizeof(TCHAR));
					memcpy(newTextBuf.get() + nameLenT, sprntTmp, (curSprntSize + 1) * sizeof(TCHAR));
					_FreeTCHAR(pNameT);
				}
			}
			break;
		case 3: //File ID
			sprntSize = _stprintf_s(sprntTmp, TEXT("%u"), this->listEntries[iItem].fileID);
			break;
		case 4: //Path ID
			sprntSize = _stprintf_s(sprntTmp, TEXT("%lld"), (int64_t)this->listEntries[iItem].pathID);
			break;
		case 5: //Size (Bytes)
			sprntSize = _stprintf_s(sprntTmp, TEXT("%llu"), entry.size);
			break;
		case 6: //Modified
			sprntTmp[0] = (entry.isModified ? TEXT('*') : 0);
			sprntTmp[1] = 0;
			sprntSize = (entry.isModified ? 1 : 0);
			break;
	}
	if (sprntSize > 0)
	{
		newTextBuf.reset(new TCHAR[sprntSize + 1]);
		memcpy(newTextBuf.get(), sprntTmp, (sprntSize + 1) * sizeof(TCHAR));
	}
}

void AssetListDialog::updateSelectionDesc()
{
	size_t firstSelection = SIZE_MAX;
	size_t lastSelection = SIZE_MAX;
	size_t nSelections = 0;
	for (size_t i = 0; i < this->listEntries.size(); i++)
	{
		if (this->listEntries[i].isSelected)
		{
			lastSelection = i;
			nSelections++;
		}
	}
	if (nSelections <= 1)
	{
		if (this->pActiveModifyDialog == nullptr)
		{
			ShowWindow(GetDlgItem(hDialog, IDC_NAMESTATIC), SW_SHOW);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETNAME), SW_SHOW);

			ShowWindow(GetDlgItem(hDialog, IDC_PATHIDSTATIC), SW_SHOW);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETPATHID), SW_SHOW);

			ShowWindow(GetDlgItem(hDialog, IDC_FILEIDSTATIC), SW_SHOW);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETFILEID), SW_SHOW);

			ShowWindow(GetDlgItem(hDialog, IDC_TYPESTATIC), SW_SHOW);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETTYPE), SW_SHOW);

			ShowWindow(GetDlgItem(hDialog, IDC_NUMSELSTATIC), SW_HIDE);
		}

		if (lastSelection > INT_MAX)
			nSelections = 0;
		if (nSelections == 1)
		{
			AssetInfo *pEntry = nullptr;
			this->cacheEntry(lastSelection, pEntry);
			if (pEntry == nullptr)
				nSelections = 0;
			else
			{
				std::unique_ptr<TCHAR[]> namePtr;
				this->getEntryText(static_cast<int>(lastSelection), 0, *pEntry, namePtr); //Name
				Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETNAME), namePtr ? namePtr.get() : TEXT(""));
				this->getEntryText(static_cast<int>(lastSelection), 4, *pEntry, namePtr); //Path ID
				Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETPATHID), namePtr ? namePtr.get() : TEXT(""));
				this->getEntryText(static_cast<int>(lastSelection), 3, *pEntry, namePtr); //File ID
				Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETFILEID), namePtr ? namePtr.get() : TEXT(""));
				this->getEntryText(static_cast<int>(lastSelection), 128, *pEntry, namePtr); //Type Name + Hex ID
				Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETTYPE), namePtr ? namePtr.get() : TEXT(""));
			}
		}
		if (nSelections == 0)
		{
			Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETNAME), TEXT(""));
			Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETPATHID), TEXT(""));
			Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETFILEID), TEXT(""));
			Edit_SetText(GetDlgItem(hDialog, IDC_EDITASSETTYPE), TEXT(""));
		}
	}
	else //if (nSelections > 1)
	{
		if (this->pActiveModifyDialog == nullptr)
		{
			ShowWindow(GetDlgItem(hDialog, IDC_NAMESTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETNAME), SW_HIDE);

			ShowWindow(GetDlgItem(hDialog, IDC_PATHIDSTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETPATHID), SW_HIDE);

			ShowWindow(GetDlgItem(hDialog, IDC_FILEIDSTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETFILEID), SW_HIDE);

			ShowWindow(GetDlgItem(hDialog, IDC_TYPESTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDialog, IDC_EDITASSETTYPE), SW_HIDE);

			ShowWindow(GetDlgItem(hDialog, IDC_NUMSELSTATIC), SW_SHOW);
		}

		TCHAR sprntTmp[64];
		_stprintf_s(sprntTmp, TEXT("%llu selected assets"), (uint64_t)nSelections);
		Edit_SetText(GetDlgItem(hDialog, IDC_NUMSELSTATIC), sprntTmp);
	}
	this->lastNumSelections = nSelections;
}

void AssetListDialog::requestRemoveSelectedAssets()
{
	std::vector<size_t> selections;
	for (size_t i = 0; i < this->listEntries.size(); i++)
	{
		if (this->listEntries[i].isSelected)
		{
			selections.push_back(i);
		}
	}
	if (selections.size() > 0 &&
		MessageBox(
			hDialog, 
			TEXT("Are you sure you want to remove the selected asset(s)?\nThis will break any reference to the selection."), 
			TEXT("Warning"), 
			MB_YESNO)
			== IDYES)
	{
		auto pProgressIndicator = std::make_shared<CProgressIndicator>(this->pContext->getMainWindow().getHInstance());
		if (pProgressIndicator->Start(this->hDialog, pProgressIndicator, 2000))
		{
			pProgressIndicator->SetStepRange(0, (unsigned int)std::min<size_t>(INT_MAX, listEntries.size()));
			pProgressIndicator->SetCancellable(true);
			pProgressIndicator->SetTitle("Removing assets");
		}
		else
			pProgressIndicator.reset();
		size_t _progressUpdateCounter = 431;
		for (size_t i = 0; i < selections.size(); ++i)
		{
			if (pProgressIndicator && pProgressIndicator->IsCancelled())
				break;
			if (pProgressIndicator && (_progressUpdateCounter++) == 431)
			{
				_progressUpdateCounter = 0;
				pProgressIndicator->SetDescription(std::format("Removing asset {} / {}", i + 1, listEntries.size()));
				pProgressIndicator->SetStepStatus((unsigned int)std::min<size_t>(INT_MAX, i));
			}
			size_t nListEntriesPre = this->listEntries.size();
			unsigned int fileID = this->listEntries[selections[i]].fileID;
			pathid_t pathID = this->listEntries[selections[i]].pathID;
			FileContextInfo_ptr pContextInfo = this->pContext->getContextInfo(fileID);
			if (pContextInfo->getFileContext() != nullptr && pContextInfo->getFileContext()->getType() == FileContext_Assets)
			{
				AssetsFileContextInfo *pAssetsInfo = reinterpret_cast<AssetsFileContextInfo*>(pContextInfo.get());
				AssetIdentifier identifier(std::shared_ptr<AssetsFileContextInfo>(pContextInfo, pAssetsInfo), pathID);
				if (identifier.resolve(*this->pContext))
					pAssetsInfo->addReplacer(std::shared_ptr<AssetsEntryReplacer>(
						MakeAssetRemover(fileID, pathID, identifier.getClassID(), identifier.getMonoScriptID()), FreeAssetsReplacer),
						*this->pContext);
			}
			//Check the assumption that the 'change asset' callbacks are not applied recursively (i.e. PostMessage, not SendMessage).
			assert(this->listEntries.size() == nListEntriesPre);
		}
		if (pProgressIndicator != nullptr)
		{
			pProgressIndicator->End();
			pProgressIndicator->Free();
		}
		ListView_SetItemCount(GetDlgItem(hDialog, IDC_ASSETLIST), (int)std::min<size_t>(listEntries.size(), INT_MAX));
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
static void onResize(HWND hDlg, HWND hActiveTabWnd, bool defer = true)
{
	bool showTabs = false;
	LONG tabY = 10;
	if (SendMessage(GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS), MC_MTM_GETITEMCOUNT, 0, 0) > 1)
	{
		showTabs = true;
		tabY = 35;
	}

	{
		bool showAssetList = true;
		ShowWindow(GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS), showTabs ? SW_SHOW : SW_HIDE);
		if (hActiveTabWnd != NULL)
		{
			ShowWindow(hActiveTabWnd, SW_SHOW);
			showAssetList = false;
		}
		ShowWindow(GetDlgItem(hDlg, IDC_ASSETSSTATIC),        showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_ASSETLIST),           showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_VIEWDATA),            showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_EXPORTRAW),           showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_DUMPDATA),            showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_PLUGINS),             showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_IMPORTRAW),           showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_IMPORTDUMP),          showAssetList ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_REMOVEASSET),         showAssetList ? SW_SHOW : SW_HIDE);
		if (!showAssetList)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_NAMESTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_EDITASSETNAME), SW_HIDE);

			ShowWindow(GetDlgItem(hDlg, IDC_PATHIDSTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_EDITASSETPATHID), SW_HIDE);

			ShowWindow(GetDlgItem(hDlg, IDC_FILEIDSTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_EDITASSETFILEID), SW_HIDE);

			ShowWindow(GetDlgItem(hDlg, IDC_TYPESTATIC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_EDITASSETTYPE), SW_HIDE);

			ShowWindow(GetDlgItem(hDlg, IDC_NUMSELSTATIC), SW_HIDE);
		}
	}


	HDWP deferCtx = defer ? BeginDeferWindowPos(12) : NULL;
	bool retry = false;

	RECT client = {};
	GetClientRect(hDlg, &client);
	LONG clientWidth = client.right-client.left;
	LONG clientHeight = client.bottom-client.top;
	LONG rightPanelSize = std::min<LONG>(200, (clientWidth / 3) - 56);
	LONG rightPanelStart = clientWidth - (rightPanelSize + 16);
	LONG leftPanelStart = 19;
	LONG leftPanelSize = rightPanelStart - 7 - leftPanelStart;
	//LONG rightPanelSize = std::min<LONG>(200, clientWidth - ((2*clientWidth / 3) + 40));
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS), 0, 0,  clientWidth, 25);
	if (hActiveTabWnd != NULL)
		doMoveWindow(deferCtx, retry, hActiveTabWnd, leftPanelStart, tabY, rightPanelStart + rightPanelSize - leftPanelStart, clientHeight - tabY);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_ASSETSSTATIC),        leftPanelStart + 2, tabY + 0,   leftPanelSize - 2, 15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_ASSETLIST),           leftPanelStart,     tabY + 20,  leftPanelSize,     clientHeight - tabY - 20 - 7);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_NUMSELSTATIC),        rightPanelStart,    tabY + 20,  rightPanelSize,    15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_NAMESTATIC),          rightPanelStart,    tabY + 20,  50, 15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_EDITASSETNAME),       rightPanelStart,    tabY + 35,  rightPanelSize,    20);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_PATHIDSTATIC),        rightPanelStart,    tabY + 59,  50, 15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_EDITASSETPATHID),     rightPanelStart,    tabY + 74,  rightPanelSize,    20);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_FILEIDSTATIC),        rightPanelStart,    tabY + 98,  50, 15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_EDITASSETFILEID),     rightPanelStart,    tabY + 113, rightPanelSize,    20);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_TYPESTATIC),          rightPanelStart,    tabY + 137, 50, 15);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_EDITASSETTYPE),       rightPanelStart,    tabY + 152, rightPanelSize,    20);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_VIEWDATA),            rightPanelStart,    tabY + 191, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_EXPORTRAW),           rightPanelStart,    tabY + 235, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_DUMPDATA),            rightPanelStart,    tabY + 279, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_PLUGINS),             rightPanelStart,    tabY + 323, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_IMPORTRAW),           rightPanelStart,    tabY + 367, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_IMPORTDUMP),          rightPanelStart,    tabY + 411, rightPanelSize,    25);
	doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDC_REMOVEASSET),         rightPanelStart,    tabY + 460, rightPanelSize,    25);
	//doMoveWindow(deferCtx, retry, GetDlgItem(hDlg, IDOK), (clientWidth / 2) - 38, clientHeight - 33, 76, 26);

	if (defer)
	{
		if (retry || !EndDeferWindowPos(deferCtx))
			onResize(hDlg, hActiveTabWnd, false);
		else
			UpdateWindow(hDlg);
		deferCtx = NULL;
	}
	else
		UpdateWindow(hDlg);

}
void AssetListDialog::onCacheUpdateTick()
{
	KillTimer(hDialog, (uintptr_t)1);
	applyDeferredChanges();
	if (!entryCachingScheduled || cachedListEntryCount >= listEntries.size())
	{
		entryCachingScheduled = false;
	}
	else
	{
		bool updateTickFrequency = false;
		if (ticksUntilCacheFreqUpdate-- == 0)
		{
			updateTickFrequency = true;
			ticksUntilCacheFreqUpdate = 10;
		}
		LARGE_INTEGER preTimer;
		if (updateTickFrequency) QueryPerformanceCounter(&preTimer);

		if (cachedListEntryStartIdx > listEntries.size()) cachedListEntryStartIdx = 0;
		AssetInfo *tmp;
		size_t newEntries = cacheEntries(cachedListEntryStartIdx, listEntries.size(), tmp, maxEntriesPerTick, &cachedListEntryStartIdx);
		if (newEntries < std::min(maxEntriesPerTick, listEntries.size() - cachedListEntryCount))
		{
			//The list probably was sorted after starting auto caching, or another dialog worked on the same file cache.
			newEntries += cacheEntries(0, listEntries.size(), tmp, maxEntriesPerTick - newEntries, &cachedListEntryStartIdx);
			cachedListEntryCount = cachedListEntryStartIdx;
		}
		else
			cachedListEntryCount += newEntries;
		if (newEntries == 0 && maxEntriesPerTick > 0)
			cachedListEntryCount = listEntries.size();
		if (cachedListEntryCount > listEntries.size())
		{
			assert(false); //cachedListEntryCount is inconsistent with listEntries
			cachedListEntryCount = listEntries.size();
		}
		if (updateTickFrequency)
		{
			LARGE_INTEGER postTimer;
			QueryPerformanceCounter(&postTimer);
			QWORD deltaMicro = ((postTimer.QuadPart - preTimer.QuadPart) * 1000000) / qpfrequency.QuadPart;
			if (deltaMicro < 9000)
			{
				if (maxEntriesPerTick >= deltaMicro / 4)
				{
					//Calculating the "optimal" amount of ticks would run
					// into precision issues and potentially division by zero.
					//Controlled cultivation should be a healthy choice.
					if (deltaMicro < 6500)
						maxEntriesPerTick *= 2;
					else
						maxEntriesPerTick = (maxEntriesPerTick / 4) * 5;
					if (maxEntriesPerTick < 10) 
						maxEntriesPerTick = 10;
				}
				else
				{
					//Try to approach the target of 10ms/tick by increasing the amount of entries per tick.
					size_t targetMaxEntries = std::max<size_t>(16, 10000 / (deltaMicro / maxEntriesPerTick));
					if (targetMaxEntries < maxEntriesPerTick) targetMaxEntries = maxEntriesPerTick + 8;
					size_t delta = targetMaxEntries - maxEntriesPerTick;
					//Contain the spread by limiting the growth rate.
					maxEntriesPerTick += (delta / 4) * 3;
				}
			}
			//else if (deltaMicro >= 9000 && deltaMicro < 13000)
			//{growth rate near 1 for now}
			else if (deltaMicro >= 13000)
			{
				if (maxEntriesPerTick <= 10) 
				{
					//Probably some bottleneck in traversing the list if it was sorted after starting caching.
					//Just disable auto caching in that case, since reducing the amount of entries further likely won't help.
					entryCachingScheduled = false;
				}
				else if (maxEntriesPerTick >= deltaMicro / 4)
				{
					//Calculating the "optimal" amount of ticks would run
					// into precision issues and potentially division by zero.
					//Some numeric distancing should be a reasonable choice in this case.
					maxEntriesPerTick = deltaMicro / 8;
				}
				else
				{
					//Try to approach the target of 10ms/tick by decreasing the amount of entries per tick.
					size_t targetMaxEntries = std::max<size_t>(10, 10000 / (deltaMicro / maxEntriesPerTick));
					if (targetMaxEntries >= maxEntriesPerTick) targetMaxEntries = maxEntriesPerTick - 8;
					size_t delta = maxEntriesPerTick - targetMaxEntries;
					if (delta < 4)
					{
						switch (delta)
						{
						case 1: break;
						case 2: maxEntriesPerTick--; break;
						case 3: maxEntriesPerTick--; break;
						}
					}
					else
						maxEntriesPerTick -= (delta / 4) * 3;
				}
			}
		}
		if (entryCachingScheduled)
			SetTimer(hDialog, (uintptr_t)1, 16, NULL);
	}
}
LRESULT CALLBACK AssetListDialog::ListViewSubclassProc(HWND hWnd, UINT message, 
		WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	AssetListDialog *pThis = (AssetListDialog*)dwRefData;
	//switch (message)
	//{
	//}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}
void AssetListDialog::searchNext()
{
	if (searchQuery.empty())
		return;
	size_t iCur = (this->searchDirectionUp) ? this->listEntries.size() : 0;
	ListEntrySelectionIterator selectionIter(*this);
	if (!selectionIter.isEnd())
	{
		iCur = (*selectionIter) + 1;
	}
	auto checkEntry = [this](size_t iCur)
	{
		if (this->listEntries[iCur].isSelected)
			return false;
		unsigned int fileID = this->listEntries[iCur].fileID;
		pathid_t pathID = this->listEntries[iCur].pathID;
		AssetInfo* pEntry = nullptr;
		auto fileEntryIt = fileEntries.find(fileID);
		if (fileEntryIt != fileEntries.end())
		{
			auto cacheEntryIt = fileEntryIt->second->assetCache.find(pathID);
			if (cacheEntryIt != fileEntryIt->second->assetCache.end())
				pEntry = &cacheEntryIt->second;
		}
		if (pEntry == nullptr)
		{
			this->cacheEntries(
				iCur,
				(iCur < SIZE_MAX - 10) ? std::min(iCur + 10, this->listEntries.size()) : (iCur + 1),
				pEntry);
			if (pEntry == nullptr)
				return false;
		}
		if (std::regex_search(pEntry->name, this->searchRegex) || std::regex_search(pEntry->containerName, this->searchRegex))
		{
			this->selectAsset(fileID, pathID);
			return true;
		}
		return false;
	};

	auto pProgressIndicator = std::make_shared<CProgressIndicator>(this->pContext->getMainWindow().getHInstance());
	if (pProgressIndicator->Start(this->hDialog, pProgressIndicator, 2000))
	{
		pProgressIndicator->SetCancellable(true);
		pProgressIndicator->SetTitle("Searching for asset");
	}
	else
		pProgressIndicator.reset();

	size_t _progressUpdateCounter = 431;
	bool requestCancel = false;
	auto updateProgress = [&pProgressIndicator, &_progressUpdateCounter, &requestCancel](size_t progress, size_t total)
	{
		if (pProgressIndicator && (_progressUpdateCounter++) == 431)
		{
			_progressUpdateCounter = 0;
			pProgressIndicator->SetDescription(std::format("Processing asset {} / {}", progress + 1, total));
			pProgressIndicator->SetStepStatus((unsigned int)std::min<size_t>(INT_MAX, progress));
		}
		if (pProgressIndicator && pProgressIndicator->IsCancelled())
			requestCancel = true;
	};

	if (this->searchDirectionUp)
	{
		size_t iStart = iCur;
		size_t total = iCur;
		if (pProgressIndicator)
			pProgressIndicator->SetStepRange(0, (unsigned int)std::min<size_t>(INT_MAX, total));
		for (; !requestCancel && iCur > 0 && !checkEntry(iCur - 1); --iCur)
			updateProgress(iStart - iCur, total);
	}
	else
	{
		size_t iStart = iCur;
		size_t total = this->listEntries.size() - iCur;
		if (pProgressIndicator)
			pProgressIndicator->SetStepRange(0, (unsigned int)std::min<size_t>(INT_MAX, total));
		for (; !requestCancel && iCur < this->listEntries.size() && !checkEntry(iCur); ++iCur)
			updateProgress(iCur - iStart, total);
	}
	if (pProgressIndicator != nullptr)
	{
		pProgressIndicator->End();
		pProgressIndicator->Free();
	}
	//TODO: Add some indication if no result was found.
}
INT_PTR CALLBACK AssetListDialog::GotoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	AssetListDialog* pThis = (AssetListDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_CLOSE:
	case WM_DESTROY:
		break;
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
		pThis = (AssetListDialog*)lParam;

		unsigned int selectedFileID = 0;
		pathid_t selectedPathID = 0;
		ListEntrySelectionIterator selectionIter(*pThis);
		if (!selectionIter.isEnd())
		{
			size_t iSelection = *selectionIter;
			selectedFileID = pThis->listEntries[iSelection].fileID;
			selectedPathID = pThis->listEntries[iSelection].pathID;
		}

		HWND hComboAssets = GetDlgItem(hDlg, IDC_COMBOASSETS);
		pThis->gotoDlg_uiToFileIDMapping.clear();
		//Iterating through an unordered_map is not the most efficient, but good enough here.
		for (std::pair<const unsigned int, FileEntryCacheRef>& entryRef : pThis->fileEntries)
		{
			unsigned int fileID = entryRef.first;
			pThis->gotoDlg_uiToFileIDMapping.push_back(fileID);
		}
		std::sort(pThis->gotoDlg_uiToFileIDMapping.begin(), pThis->gotoDlg_uiToFileIDMapping.end());
		int cbSelection = -1;
		for (size_t i = 0; i < pThis->gotoDlg_uiToFileIDMapping.size(); ++i)
		{
			unsigned int fileID = pThis->gotoDlg_uiToFileIDMapping[i];
			auto pAssetsInfo = std::dynamic_pointer_cast<AssetsFileContextInfo>(
				pThis->pContext->getContextInfo(fileID));
			assert(pAssetsInfo != nullptr);
			std::string desc = std::format("{} - {}",
				fileID,
				(pAssetsInfo != nullptr) ? pAssetsInfo->getFileName() : std::string(""));
			auto tCbEntryText = unique_MultiByteToTCHAR(desc.c_str());
			ComboBox_AddString(hComboAssets, tCbEntryText.get());
			if (fileID == selectedFileID && i < std::numeric_limits<int>::max())
				cbSelection = (int)i;
		}
		if (cbSelection != -1)
		{
			ComboBox_SetCurSel(hComboAssets, cbSelection);
			std::basic_string<TCHAR> pathIDText = std::to_wstring((int64_t)selectedPathID);
			Edit_SetText(GetDlgItem(hDlg, IDC_EDITPATHID), pathIDText.c_str());
		}
		return (INT_PTR)TRUE;
	}
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
		case IDOK:
		{
			char numberTmp[25];
			GetWindowTextA(GetDlgItem(hDlg, IDC_EDITPATHID), numberTmp, 25);
			numberTmp[24] = 0;
			__int64 pathID;
			if (numberTmp[0] == '-')
				pathID = _strtoi64(numberTmp, NULL, 0);
			else
				pathID = (__int64)_strtoui64(numberTmp, NULL, 0);

			int fileIDSel = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_COMBOASSETS));
			if (fileIDSel < 0 || (unsigned int)fileIDSel >= pThis->gotoDlg_uiToFileIDMapping.size())
				return (INT_PTR)TRUE;

			unsigned int fileID = pThis->gotoDlg_uiToFileIDMapping[(unsigned int)fileIDSel];

			pThis->selectAsset(fileID, (pathid_t)pathID);
		}
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK AssetListDialog::SearchDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	AssetListDialog* pThis = (AssetListDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_CLOSE:
	case WM_DESTROY:
		break;
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
		pThis = (AssetListDialog*)lParam;
		auto tQuery = unique_MultiByteToTCHAR(pThis->searchQuery.c_str());
		SetWindowText(GetDlgItem(hDlg, IDC_EDITQUERY), tQuery.get());
		Button_SetCheck(GetDlgItem(hDlg, IDC_CKCASESENS), pThis->searchCaseSensitive ? TRUE : FALSE);
		Button_SetCheck(GetDlgItem(hDlg, pThis->searchDirectionUp ? IDC_RBUP : IDC_RBDOWN), TRUE);
		return (INT_PTR)TRUE;
	}
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
		case IDC_RBUP:
		{
			if (Button_GetCheck(GetDlgItem(hDlg, IDC_RBUP)) == BST_CHECKED)
				Button_SetCheck(GetDlgItem(hDlg, IDC_RBDOWN), BST_UNCHECKED);
			else if (Button_GetCheck(GetDlgItem(hDlg, IDC_RBDOWN)) != BST_CHECKED)
				Button_SetCheck(GetDlgItem(hDlg, IDC_RBDOWN), BST_CHECKED);
			break;
		}
		case IDC_RBDOWN:
		{
			if (Button_GetCheck(GetDlgItem(hDlg, IDC_RBDOWN)) == BST_CHECKED)
				Button_SetCheck(GetDlgItem(hDlg, IDC_RBUP), BST_UNCHECKED);
			else if (Button_GetCheck(GetDlgItem(hDlg, IDC_RBUP)) != BST_CHECKED)
				Button_SetCheck(GetDlgItem(hDlg, IDC_RBUP), BST_CHECKED);
			break;
		}
		case IDOK:
		{
			HWND hEditQuery = GetDlgItem(hDlg, IDC_EDITQUERY);
			DWORD textLen = Edit_GetTextLength(hEditQuery);
			if (textLen >= (DWORD)std::numeric_limits<int>::max())
				break;
			std::vector<wchar_t> textBuf(textLen + 1);
			GetWindowText(GetDlgItem(hDlg, IDC_EDITQUERY), textBuf.data(), (int)textBuf.size());
			textBuf.back() = 0;
			auto queryU8 = unique_TCHARToMultiByte(textBuf.data());
			pThis->searchQuery.assign(queryU8.get());

			pThis->searchCaseSensitive =
				Button_GetCheck(GetDlgItem(hDlg, IDC_CKCASESENS)) ? true : false;
			pThis->searchDirectionUp =
				(Button_GetCheck(GetDlgItem(hDlg, IDC_RBUP)) == BST_CHECKED);

			std::string regexStr = "";
			for (size_t iChar = 0; iChar < pThis->searchQuery.size(); ++iChar)
			{
				switch (pThis->searchQuery[iChar])
				{
				case '*':
					regexStr += ".*";
					break;
				case '(': case '[': case ']': case ')':
				case '{': case '}':
				case '\\': case '^': case '$':
				case '.': case '|': case '?': case '+':
					regexStr += '\\';
				default:
					regexStr += pThis->searchQuery[iChar];
					break;
				}
			}
			std::regex_constants::syntax_option_type regexOptions = std::regex_constants::optimize;
			if (pThis->searchCaseSensitive)
				regexOptions |= std::regex_constants::icase;
			pThis->searchRegex = std::regex(regexStr, regexOptions);

			EndDialog(hDlg, 1);
			return (INT_PTR)TRUE;
		}
		break;
		case IDCANCEL:
			EndDialog(hDlg, 0);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK AssetListDialog::AssetListProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	INT_PTR ret = (INT_PTR)FALSE;
	AssetListDialog *pThis = (AssetListDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	switch (message)
	{
	case WM_CLOSE:
		if (pThis)
			pThis->hDialog = NULL;
		DestroyWindow(hDlg);
		ret = (INT_PTR)TRUE;
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (AssetListDialog*)lParam;
			pThis->hDialog = hDlg;
			//pMainWindow->assetsInfoDialog.hHotkeyHook = SetWindowsHookEx(WH_GETMESSAGE, AssetsInfoKeyboardHookProc, NULL, GetCurrentThreadId());

			pThis->selectionUpdateScheduled = false;
			pThis->windowUpdateScheduled = false;
			pThis->entryCachingScheduled = false;
			HWND hAssetListView = GetDlgItem(hDlg, IDC_ASSETLIST);
			//SetWindowSubclass(hAssetListView, ListViewSubclassProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
			ShowWindow(hAssetListView, SW_HIDE);

			ShowWindow(hAssetListView, SW_SHOW);
			ListView_SetItemCount(GetDlgItem(hDlg, IDC_ASSETLIST), (int)std::min<size_t>(pThis->listEntries.size(), INT_MAX));

			LVCOLUMN column;
			ZeroMemory(&column, sizeof(LVCOLUMN));
			column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			column.cx = 150;
			column.pszText = const_cast<TCHAR*>(TEXT("Name"));
			column.iSubItem = 0;
			ListView_InsertColumn(hAssetListView, 0, &column);
			column.cx = 150;
			column.pszText = const_cast<TCHAR*>(TEXT("Container"));
			//column.iSubItem = 1;
			ListView_InsertColumn(hAssetListView, 1, &column);
			column.cx = 60;
			column.pszText = const_cast<TCHAR*>(TEXT("Type"));
			//column.iSubItem = 2;
			ListView_InsertColumn(hAssetListView, 2, &column);
			column.cx = 50;
			column.pszText = const_cast<TCHAR*>(TEXT("File ID"));
			//column.iSubItem = 3;
			ListView_InsertColumn(hAssetListView, 3, &column);
			column.cx = 60;
			column.pszText = const_cast<TCHAR*>(TEXT("Path ID"));
			//column.iSubItem = 4;
			ListView_InsertColumn(hAssetListView, 4, &column);
			column.mask |= LVCF_FMT;
			column.cx = 70;
			column.fmt = LVCFMT_RIGHT;
			column.pszText = const_cast<TCHAR*>(TEXT("Size (bytes)"));
			//column.iSubItem = 5;
			ListView_InsertColumn(hAssetListView, 5, &column);
			column.fmt = LVCFMT_LEFT;
			column.cx = 60;
			column.pszText = const_cast<TCHAR*>(TEXT("Modified"));
			ListView_InsertColumn(hAssetListView, 6, &column);

			ListView_SetCallbackMask(hAssetListView, LVIS_SELECTED);

			{
				HWND hTabsControl = GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS);
				MC_MTITEMWIDTH widths;
				widths.dwDefWidth = 0;
				widths.dwMinWidth = 90;
				SendMessage(hTabsControl, MC_MTM_SETITEMWIDTH, 0, (LPARAM) &widths);
				
				std::shared_ptr<AssetModifyDialog> modifyDialogToSelect = pThis->pActiveModifyDialog;

				MC_MTITEM newItem = {};
				newItem.dwMask = MC_MTIF_TEXT | MC_MTIF_PARAM | MC_MTIF_CLOSEFLAG;
				newItem.pszText = const_cast<TCHAR*>(TEXT("Asset list"));
				newItem.lParam = 0;
				newItem.bDisableClose = TRUE;
				SendMessage(hTabsControl, MC_MTM_INSERTITEM, (WPARAM)0, (LPARAM)&newItem);
				if (!pThis->modifyDialogs.empty())
				{
					ShowWindow(hTabsControl, SW_SHOW);
					size_t newTabIdx = 1;

					size_t tabIdxToSelect = 0;
					for (auto dialogIt = pThis->modifyDialogs.begin(); dialogIt != pThis->modifyDialogs.end(); ++dialogIt)
					{
						if (modifyDialogToSelect != nullptr && dialogIt->get() == modifyDialogToSelect.get())
							tabIdxToSelect = newTabIdx;
						std::string tabName8 = (*dialogIt != nullptr) ? dialogIt->get()->getTabName() : "-";
						auto upTabNameT = unique_MultiByteToTCHAR(tabName8.c_str());

						newItem.dwMask = MC_MTIF_TEXT | MC_MTIF_PARAM;
						newItem.pszText = upTabNameT.get();
						newItem.lParam = (LPARAM)dialogIt->get();
						SendMessage(hTabsControl, MC_MTM_INSERTITEM, (WPARAM)newTabIdx, (LPARAM)&newItem);

						newTabIdx++;
					}

					SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)tabIdxToSelect, 0); //Also sends a SELCHANGE notification.
				}
			}

			
			if (pThis->iLastTopItem >= 0 && pThis->iLastTopItem < pThis->listEntries.size())
			{
				ListView_EnsureVisible(hAssetListView, pThis->iLastTopItem, FALSE);
			}

			//Tooltip style for item text; Double buffering to prevent flickering while scrolling, resizing, etc.
			ListView_SetExtendedListViewStyle(hAssetListView, LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER | LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT);
			
			pThis->iFocusedItem = -1;

			if (pThis->listEntries.size() > 0)
			{
				pThis->entryCachingScheduled = true;
				SetTimer(hDlg, (uintptr_t)1, 16, NULL);
			}

			//Will not fail (on Win >= XP) according to MS docs.
			QueryPerformanceFrequency(&pThis->qpfrequency);
			pThis->ticksUntilCacheFreqUpdate = 1;

			ShowWindow(hDlg, SW_SHOW);
			PostMessage(hDlg, WM_SIZE, 0, 0);
			ret = (INT_PTR)TRUE;
		}
		break;
	case WM_TIMER:
		{
			if (wParam == (uintptr_t)0 && pThis && pThis->windowUpdateScheduled)
			{
				pThis->windowUpdateScheduled = false;
				KillTimer(hDlg, wParam);
				UpdateWindow(GetDlgItem(hDlg, IDC_ASSETLIST));
			}
			if (wParam == (uintptr_t)1 && pThis)
			{
				pThis->onCacheUpdateTick();
			}
			if (wParam == (uintptr_t)2 && pThis->selectionUpdateScheduled)
			{
				pThis->selectionUpdateScheduled = false;
				KillTimer(hDlg, wParam);
				pThis->applyDeferredChanges();
				pThis->updateSelectionDesc();
			}
		}
		ret = 0;
		break;
	case WM_NOTIFY:
		{
			NMLISTVIEW *pNotifyLV = (NMLISTVIEW*)lParam;
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
									for (size_t i = 0; i < pThis->listEntries.size(); i++)
									{
										pThis->listEntries[i].isSelected = isSelected;
									}
								}
								else if (iItem >= 0 && iItem < pThis->listEntries.size())
								{
									pThis->listEntries[iItem].isSelected = isSelected;
								}
							}
							if (pInfo->uNewState & LVIS_FOCUSED)
							{
								pThis->iFocusedItem = pInfo->iItem;
							}
						}
					}
					pThis->updateSelectionDesc();
					break;
				case LVN_ODSTATECHANGED:
					{
						NMLVODSTATECHANGE *pInfo = (NMLVODSTATECHANGE*)lParam;
						if (pThis)
						{
							int from = pInfo->iFrom; 
							if (from < 0)
								from = 0;
							int to = pInfo->iTo; 
							if (to < 0 || to >= pThis->listEntries.size()) 
								to = (pThis->listEntries.size() > INT_MAX) ? INT_MAX : (int)(pThis->listEntries.size() - 1);

							if ((pInfo->uOldState ^ pInfo->uNewState) & LVIS_SELECTED)
							{
								bool isSelected = (pInfo->uNewState & LVIS_SELECTED) ? true : false;
								for (int i = from; i <= to; i++)
								{
									pThis->listEntries[i].isSelected = isSelected;
								}
							}
							if (pInfo->uNewState & LVIS_FOCUSED)
							{
								pThis->iFocusedItem = pInfo->iFrom;
							}
						}
					}
					pThis->updateSelectionDesc();
					break;
				case LVN_GETINFOTIP:
					{
						NMLVGETINFOTIP *pInfo = (NMLVGETINFOTIP*)lParam;
						int iItem = pInfo->iItem;
						if (pThis && iItem >= 0 && iItem < pThis->listEntries.size())
						{
							AssetInfo *pEntry = nullptr;
							pThis->cacheEntry(iItem, pEntry);
							if (pEntry)
							{
								assert(pThis->lvStringBuf_Tooltip.size() >= 2);
								//Shift the string buffer list (removing the oldest if needed).
								for (size_t i = pThis->lvStringBuf_Tooltip.size() - 1; i > 0; i--)
									pThis->lvStringBuf_Tooltip[i] = std::move(pThis->lvStringBuf_Tooltip[i-1]);
								pThis->lvStringBuf_Tooltip[0].reset();
								pInfo->cchTextMax = 0;
								pInfo->pszText = const_cast<TCHAR*>(TEXT(""));
								//Retrieve the entry text and store the string buffer.
								std::unique_ptr<TCHAR[]> newTextBuf;
								pThis->getEntryText(iItem, pInfo->iSubItem, *pEntry, newTextBuf);
								pThis->lvStringBuf_Tooltip[0].swap(newTextBuf);
								if (pThis->lvStringBuf_Tooltip[0] != nullptr)
									pInfo->pszText = pThis->lvStringBuf_Tooltip[0].get();
							}
						}
					}
					break;
				case LVN_GETDISPINFO:
					{
						NMLVDISPINFO *pInfo = (NMLVDISPINFO*)lParam;
						int iItem = pInfo->item.iItem;
						if (pThis && iItem >= 0 && iItem < pThis->listEntries.size())
						{
							AssetInfo *pEntry = nullptr;
							pThis->cacheEntry(iItem, pEntry);
							if (pEntry)
							{
								UINT newMask = 0;
								pInfo->item.lParam = (LPARAM)iItem;
								newMask |= LVIF_PARAM;
								pInfo->item.stateMask = LVIS_SELECTED;
								if (pInfo->item.iSubItem == 0)
									pInfo->item.state = (pThis->listEntries[iItem].isSelected ? LVIS_SELECTED : 0);
								else
									pInfo->item.state = 0;
								newMask |= LVIF_STATE;
								if (pInfo->item.mask & LVIF_TEXT)
								{
									newMask |= LVIF_TEXT;
									assert(pThis->lvStringBuf_Ownerdata.size() >= 2);
									//Shift the string buffer list (removing the oldest if needed).
									for (size_t i = pThis->lvStringBuf_Ownerdata.size() - 1; i > 0; i--)
										pThis->lvStringBuf_Ownerdata[i] = std::move(pThis->lvStringBuf_Ownerdata[i-1]);
									pThis->lvStringBuf_Ownerdata[0].reset();
									//Retrieve the entry text and store the string buffer.
									pInfo->item.cchTextMax = 0;
									pInfo->item.pszText = const_cast<TCHAR*>(TEXT(""));
									std::unique_ptr<TCHAR[]> newTextBuf = nullptr;
									pThis->getEntryText(iItem, pInfo->item.iSubItem, *pEntry, newTextBuf);
									pThis->lvStringBuf_Ownerdata[0].swap(newTextBuf);
									if (pThis->lvStringBuf_Ownerdata[0] != nullptr)
										pInfo->item.pszText = pThis->lvStringBuf_Ownerdata[0].get();
								}
								pInfo->item.mask = newMask;

								bool isSelected = pThis->listEntries[iItem].isSelected;
								bool isFocused = (iItem == pThis->iFocusedItem);
								//Workaround in case the ListView's selection states are not in sync.
								//The GetDispInfo selection status is only used for visual purposes and does not touch the item state.
								ListView_SetItemState(pInfo->hdr.hwndFrom, iItem, 
									((isSelected) ? LVIS_SELECTED : 0) | ((isFocused) ? LVIS_FOCUSED : 0), //state
									LVIS_SELECTED | ((isFocused) ? LVIS_FOCUSED : 0)); //mask; Only set LVIS_FOCUSED, don't reset.
							}
						}
					}
					ret = (INT_PTR)TRUE;
					break;
				case LVN_SETDISPINFO:
					{
						NMLVDISPINFO *pInfo = (NMLVDISPINFO*)lParam;
						int iItem = pInfo->item.iItem;
						if (pThis && iItem >= 0 && iItem < pThis->listEntries.size())
						{
							if (pInfo->item.mask & LVIF_STATE)
							{
								if ((pInfo->item.stateMask & LVIS_FOCUSED) && (pInfo->item.state & LVIS_FOCUSED))
								{
									pThis->iFocusedItem = iItem;
								}
								if (pInfo->item.stateMask & LVIS_SELECTED)
								{
									bool isSelected = (pInfo->item.state & LVIS_SELECTED) ? true : false;
									if (isSelected != pThis->listEntries[iItem].isSelected)
									{
										pThis->listEntries[iItem].isSelected = isSelected;
										pThis->updateSelectionDesc();
									}
								}
							}
						}
					}
					break;
				case LVN_ODCACHEHINT:
					{
						NMLVCACHEHINT *pInfo = (NMLVCACHEHINT*)lParam;
						if (pThis && pInfo->iFrom >= 0 && pInfo->iTo >= pInfo->iFrom)
						{
							AssetInfo *pEntry = nullptr;
							pThis->cachedListEntryCount += pThis->cacheEntries(pInfo->iFrom, pInfo->iTo + 1, pEntry);
						}
					}
					ret = (INT_PTR)TRUE;
					break;
				case LVN_ODFINDITEM:
					{
						LVFINDINFO *pInfo = (LVFINDINFO*)lParam;
						//TODO
					}
					SetWindowLongPtr(hDlg, DWLP_MSGRESULT, -1); 
					ret = (INT_PTR)TRUE;
					break;
				case LVN_COLUMNCLICK:
					{
						NMLISTVIEW *pInfo = (NMLISTVIEW*)lParam;
						if (pThis)
						{
							pThis->sortOrderAscending ^= (pThis->sorted && pThis->iSortColumn == pInfo->iSubItem);
							pThis->iSortColumn = pInfo->iSubItem;
							pThis->sorted = false;
							pThis->resort();
						}
					}
					break;
				case LVN_ITEMACTIVATE:
					{
						NMITEMACTIVATE *pInfo = (NMITEMACTIVATE*)lParam;
						if (pThis)
						{
							if ((pInfo->iItem >= 0 && pInfo->iItem < pThis->listEntries.size() && pThis->listEntries[pInfo->iItem].isSelected)
								&& !(pInfo->uKeyFlags & (LVKF_CONTROL | LVKF_SHIFT)))
							{
								int topIdx = ListView_GetTopIndex(pInfo->hdr.hwndFrom);
								int bottomIdx = topIdx + ListView_GetCountPerPage(pInfo->hdr.hwndFrom) + 1;

								//Deselect all. Required when the Win32 dialog is closed and reopened and there are still selected items.
								//Otherwise, the old selection would stay even when just clicking an item.
								ListView_SetItemState(pInfo->hdr.hwndFrom, -1, 0, LVIS_SELECTED);
								ListView_SetItemState(pInfo->hdr.hwndFrom, pInfo->iItem, LVIS_SELECTED, LVIS_SELECTED);
							}
						}
					}
					break;
				case MC_MTN_CLOSEITEM:
					if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS))
					{
						MC_NMMTCLOSEITEM *pNotification = (MC_NMMTCLOSEITEM*)lParam;
						if (!pThis->preDeleteTab(pNotification))
							SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
						return (INT_PTR)TRUE; //Prevent tab deletion.
					}
					break;
				case MC_MTN_DELETEITEM:
					if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS))
					{
						MC_NMMTDELETEITEM *pNotification = (MC_NMMTDELETEITEM*)lParam;
						pThis->onDeleteTab(pNotification);
					}
					break;
				case MC_MTN_SELCHANGE:
					if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS))
					{
						MC_NMMTSELCHANGE *pNotification = (MC_NMMTSELCHANGE*)lParam;
						pThis->onSwitchTabs(pNotification);
					}
					break;
				case MC_MTN_DELETEALLITEMS:
					if (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS))
					{
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
						return (INT_PTR)TRUE; //When the dialog closes due to a <onHide> call, keep the internal tabs.
					}
					break;
			}
		}
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDM_FILE_CLOSE:
				//if (AskSaveAssetsInfo(pMainWindow, hDlg))
				{
					if (pThis)
						pThis->pContext->getMainWindow().hideManipulateDialog(pThis);
					else
						SendMessage(hDlg, WM_CLOSE, 0, 0);
				}
				return (INT_PTR)TRUE;
			case IDM_FILE_APPLY:
				//Already handled in onCommand
				break;
			case IDM_VIEW_CONTINUESEARCH:
				pThis->searchNext();
				break;
			case IDM_VIEW_SEARCHBYNAME:
				{
					std::shared_ptr<IFileManipulateDialog> pSelfRef = pThis->selfPtr.lock();
					if (!pSelfRef) { assert(false); break; }
					if (DialogBoxParam(pThis->pContext->getMainWindow().getHInstance(),
						MAKEINTRESOURCE(IDD_SEARCHASSET),
						pThis->hParentWnd ? pThis->hParentWnd : hDlg,
						SearchDlgProc,
						(LPARAM)pThis)
						== 1)
					{
						pThis->searchNext();
						EnableMenuItem(pThis->pContext->getMainWindow().getMenu(), IDM_VIEW_CONTINUESEARCH, MF_ENABLED);
					}
				}
				break;
			case IDM_VIEW_GOTOASSET:
				{
					std::shared_ptr<IFileManipulateDialog> pSelfRef = pThis->selfPtr.lock();
					if (!pSelfRef) { assert(false); break; }
					DialogBoxParam(pThis->pContext->getMainWindow().getHInstance(),
						MAKEINTRESOURCE(IDD_GOTOASSET),
						pThis->hParentWnd ? pThis->hParentWnd : hDlg,
						GotoDlgProc,
						(LPARAM)pThis);
				}
				break;
			case IDM_VIEW_CONTAINERS:
				/*if (pMainWindow->assetsInfoDialog.hContainersDlg)
				{
					SetWindowPos(pMainWindow->assetsInfoDialog.hContainersDlg, hDlg, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				}
				else
				{
					pMainWindow->assetsInfoDialog.hContainersDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_VIEWCONTAINERS), NULL, ViewContainers, NULL);
					if (pMainWindow->assetsInfoDialog.hContainersDlg)
					{
						//https://stackoverflow.com/questions/812686/can-a-window-be-always-on-top-of-just-one-other-window
						SetWindowLongPtr(pMainWindow->assetsInfoDialog.hContainersDlg, GWLP_HWNDPARENT, (LONG_PTR)hDlg);
						RECT targetRect = {};
						GetWindowRect(hDlg, &targetRect);
						SetWindowPos(pMainWindow->assetsInfoDialog.hContainersDlg, hDlg, targetRect.left, targetRect.top, 0, 0, SWP_NOSIZE);
					}
				}*/
				break;
			case IDC_VIEWDATA:
				pThis->openViewDataTab();
				break;
			case IDC_DUMPDATA:
				pThis->onExportDumpButton();
				break;
			case IDC_EXPORTRAW:
				pThis->exportAssetsRaw(pThis->getSelectedAssets());
				break;
			case IDC_PLUGINS:
				pThis->onPluginButton();
				break;
			case IDC_IMPORTRAW:
				pThis->importAssetsRaw(pThis->getSelectedAssets());
				break;
			case IDC_IMPORTDUMP:
				pThis->importAssetsDump(pThis->getSelectedAssets());
				break;
			case IDC_REMOVEASSET:
				pThis->requestRemoveSelectedAssets();
				break;
		}
		break;
	case WM_SIZE:
		{
			HWND hTabsControl = GetDlgItem(hDlg, IDC_ASSETLISTMODIFYTABS);
			HWND hActiveTabWnd = NULL;
			int curTab = (int)SendMessage(hTabsControl, MC_MTM_GETCURSEL, 0, 0);
			if (curTab != -1)
			{
				MC_MTITEM item = {};
				item.dwMask = MC_MTIF_PARAM;
				if (SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)curTab, (LPARAM)&item) == TRUE)
				{
					if (item.lParam != 0)
						hActiveTabWnd = reinterpret_cast<AssetModifyDialog*>(item.lParam)->getWindowHandle();
				}
			}
			onResize(hDlg, hActiveTabWnd);
			break;
		}
	}
	return ret;
}

void AssetListDialog::switchToListTab()
{
	HWND hTabsControl = GetDlgItem(this->hDialog, IDC_ASSETLISTMODIFYTABS);
	int itemCount = static_cast<int>(SendMessage(hTabsControl, MC_MTM_GETITEMCOUNT, 0, 0));
	for (int i = 0; i < itemCount; i++)
	{
		MC_MTITEM item;
		item.dwMask = MC_MTIF_PARAM;
		item.lParam = 0;
		if (SendMessage(hTabsControl, MC_MTM_GETITEM, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item))
			&& item.lParam == 0)
		{
			SendMessage(hTabsControl, MC_MTM_SETCURSEL, static_cast<WPARAM>(i), 0);
			HWND hAssetList = GetDlgItem(hDialog, IDC_ASSETLIST);
			SetFocus(hAssetList);
			break;
		}
	}
}
bool AssetListDialog::openViewDataTab(unsigned int fileID, pathid_t pathID)
{
	auto fileIDEntry = fileEntries.find(fileID);
	if (fileIDEntry == fileEntries.end())
	{
		//Try to add the file context to the list dialog.
		this->pContext->getMainWindow().selectFileContext(fileID, true);
		//If a proper dialog type was found for the file context, it should now be added via addFileContext(..).
		fileIDEntry = fileEntries.find(fileID);
		if (fileIDEntry == fileEntries.end())
			return false;
	}
	for (size_t i = 0; i < this->listEntries.size(); i++)
	{
		if (this->listEntries[i].fileID == fileID && this->listEntries[i].pathID == pathID)
		{
			this->openViewDataTab(i);
			return true;
		}
	}
	return false;
}
bool AssetListDialog::selectAsset(unsigned int fileID, pathid_t pathID)
{
	HWND hAssetList = GetDlgItem(hDialog, IDC_ASSETLIST);
	auto fileIDEntry = fileEntries.find(fileID);
	if (fileIDEntry == fileEntries.end())
	{
		//Try to add the file context to the list dialog.
		this->pContext->getMainWindow().selectFileContext(fileID, true);
		//If a proper dialog type was found for the file context, it should now be added via addFileContext(..).
		fileIDEntry = fileEntries.find(fileID);
		if (fileIDEntry == fileEntries.end())
			return false;
	}
	size_t targetIndex = SIZE_MAX;
	for (size_t i = 0; i < this->listEntries.size() && i < INT_MAX; i++)
	{
		if (this->listEntries[i].fileID == fileID && this->listEntries[i].pathID == pathID)
		{
			targetIndex = i;
			break;
		}
	}
	if (targetIndex == SIZE_MAX)
		return false;
	
	ListView_SetItemState(hAssetList, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
	ListView_SetItemState(hAssetList, static_cast<int>(targetIndex), LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
	ListView_EnsureVisible(hAssetList, static_cast<int>(targetIndex), FALSE);
	return true;
}

void AssetListDialog::addModifyDialog(std::shared_ptr<AssetModifyDialog> dialogTmp)
{
	HWND hTabsControl = GetDlgItem(this->hDialog, IDC_ASSETLISTMODIFYTABS);
	AssetModifyDialog *pDialog = dialogTmp.get();

	size_t newTabIdx = this->modifyDialogs.size() + 1;
	pDialog->selfHandle = this->modifyDialogs.insert(this->modifyDialogs.end(), std::move(dialogTmp));
	
	std::string tabName8 = pDialog->getTabName();
	auto upTabNameT = unique_MultiByteToTCHAR(tabName8.c_str());
	MC_MTITEM newItem = {};
	newItem.dwMask = MC_MTIF_TEXT | MC_MTIF_PARAM;
	newItem.pszText = upTabNameT.get();
	newItem.lParam = (LPARAM)pDialog;
	SendMessage(hTabsControl, MC_MTM_INSERTITEM, (WPARAM)newTabIdx, (LPARAM)&newItem);
	SendMessage(hTabsControl, MC_MTM_SETCURSEL, (WPARAM)newTabIdx, 0); //Also sends a SELCHANGE notification.
}
void AssetListDialog::removeModifyDialog(AssetModifyDialog *pModifyDialog)
{
	HWND hTabsControl = (this->hDialog == NULL) ? NULL : GetDlgItem(this->hDialog, IDC_ASSETLISTMODIFYTABS);
	if (this->hDialog == NULL || hTabsControl == NULL)
	{
		pModifyDialog->onDestroy();
		auto dialogSelfHandle = pModifyDialog->selfHandle;
		pModifyDialog->selfHandle = this->modifyDialogs.end();
		this->modifyDialogs.erase(dialogSelfHandle);
		return;
	}
	//Find the tab index in the control, since the user may have changed it by dragging.
	int nTabs = (int)SendMessage(hTabsControl, MC_MTM_GETITEMCOUNT, 0, 0);
	MC_MTITEM item = {};
	for (int i = 0; i < nTabs; i++)
	{
		item.dwMask = MC_MTIF_PARAM;
		if (SendMessage(hTabsControl, MC_MTM_GETITEM, (WPARAM)i, (LPARAM)&item) == TRUE)
		{
			if (item.lParam == (LPARAM)pModifyDialog)
			{
				SendMessage(hTabsControl, MC_MTM_DELETEITEM, (WPARAM)i, 0);
				break;
			}
		}
	}
}
std::shared_ptr<AssetModifyDialog> AssetListDialog::getModifyDialogRef(AssetModifyDialog *pDialog)
{
	if (pDialog->selfHandle == this->modifyDialogs.end())
		return std::shared_ptr<AssetModifyDialog>();
	return *pDialog->selfHandle;
}

bool AssetListDialog::preDeleteTab(MC_NMMTCLOSEITEM *pNotification)
{
	if (pNotification->lParam == 0) //Asset list tab
		return false;
	AssetModifyDialog *pModifyDialog = reinterpret_cast<AssetModifyDialog*>(pNotification->lParam);
	bool changesApplyable = false;
	if (pModifyDialog->hasUnappliedChanges(&changesApplyable))
	{
		SendMessage(pNotification->hdr.hwndFrom, MC_MTM_SETCURSEL, (WPARAM)pNotification->iItem, 0);
		if (changesApplyable)
		{
			switch (MessageBox(this->hDialog, 
				TEXT("This tab has unsaved changes.\nDo you want to apply the changes before closing the tab?"), 
				TEXT("Asset Bundle Extractor"), 
				MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3))
			{
			case IDYES:
				if (pModifyDialog->applyChanges())
					return true; //Close tab (changes applied).
				return false; //Don't close tab (changes not applied).
			case IDNO:
				return true; //Close tab without saving.
			case IDCANCEL:
				return false; //Don't close tab.
			}
		}
		else if (IDYES == MessageBox(this->hDialog, 
			TEXT("This tab has unsaved changes.\nDo you want to close the tab anyway and discard any unsaved changes?"), 
			TEXT("Asset Bundle Extractor"), 
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2))
		{
			return true;
		}
		return false;
	}
	return true;
}
void AssetListDialog::onDeleteTab(MC_NMMTDELETEITEM *pNotification)
{
	if (pNotification->lParam != 0)
	{
		AssetModifyDialog *pModifyDialog = reinterpret_cast<AssetModifyDialog*>(pNotification->lParam);
		pModifyDialog->onHide();
		pModifyDialog->onDestroy();
		if (pModifyDialog == pActiveModifyDialog.get())
			pActiveModifyDialog.reset();

		auto dialogSelfHandle = pModifyDialog->selfHandle;
		pModifyDialog->selfHandle = this->modifyDialogs.end();
		this->modifyDialogs.erase(dialogSelfHandle);

		if (this->modifyDialogs.empty())
		{
			//Hide the tab control.
			PostMessage(this->hDialog, WM_SIZE, 0, 0);
		}
	}
}
void AssetListDialog::onSwitchTabs(MC_NMMTSELCHANGE *pNotification)
{
	HWND newDialogHandle = NULL;
	bool runResize = false;
	if (pNotification->lParamNew != pNotification->lParamOld)
	{
		runResize = true;
		if (pNotification->lParamOld != 0)
		{
			AssetModifyDialog *pOldModifyDialog = reinterpret_cast<AssetModifyDialog*>(pNotification->lParamOld);
			HWND oldDialogHandle = pOldModifyDialog->getWindowHandle();
			if (oldDialogHandle != NULL)
				ShowWindow(oldDialogHandle, SW_HIDE);
			pOldModifyDialog->onHide();
		}
		if (pNotification->lParamNew != 0)
		{
			AssetModifyDialog *pNewModifyDialog = reinterpret_cast<AssetModifyDialog*>(pNotification->lParamNew);
			for (auto tabListIt = modifyDialogs.begin(); tabListIt != modifyDialogs.end(); ++tabListIt)
			{
				if (tabListIt->get() == pNewModifyDialog)
					pActiveModifyDialog = *tabListIt;
			}
			pNewModifyDialog->onShow(this->hDialog);
			newDialogHandle = pNewModifyDialog->getWindowHandle();
			if (newDialogHandle != NULL)
				ShowWindow(newDialogHandle, SW_SHOW);
		}
		else
		{
			pActiveModifyDialog = nullptr;
			updateSelectionDesc();
		}
	}
	if (runResize)
		onResize(this->hDialog, newDialogHandle);
}

void AssetListDialog::onExportDumpButton()
{
	if (this->hCurPopupMenu != NULL)
	{
		DestroyMenu(this->hCurPopupMenu);
		this->hCurPopupMenu = NULL;
	}
	std::vector<AssetUtilDesc> selection = getSelectedAssets();
	if (selection.empty())
		return;

	UINT popupMenuFlags = TPM_RETURNCMD | TPM_NONOTIFY;
	if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
		popupMenuFlags |= TPM_RIGHTALIGN | TPM_HORNEGANIMATION;
	else
		popupMenuFlags |= TPM_HORPOSANIMATION;

	this->hCurPopupMenu = CreatePopupMenu();
	if (this->hCurPopupMenu == NULL)
		return;
	AppendMenu(this->hCurPopupMenu, MF_STRING, 9000, TEXT("Dump as text file"));
	AppendMenu(this->hCurPopupMenu, MF_STRING, 9001, TEXT("Dump as json file"));
	POINT popupPos = {};
	RECT btnRect = {};
	if (GetWindowRect(GetDlgItem(this->hDialog, IDC_DUMPDATA), &btnRect))
	{
		popupPos.x = btnRect.left;
		popupPos.y = btnRect.bottom;
	}
	else
		GetCursorPos(&popupPos);
	uintptr_t selectedId = static_cast<uintptr_t>(TrackPopupMenuEx(this->hCurPopupMenu, popupMenuFlags, popupPos.x, popupPos.y, this->hDialog, NULL));
	switch (selectedId)
	{
	case 9000:
		this->exportAssetsTextDump(std::move(selection));
		break;
	case 9001:
		this->exportAssetsJSONDump(std::move(selection));
		break;
	}
}
void AssetListDialog::onPluginButton()
{
	if (this->hCurPopupMenu != NULL)
	{
		DestroyMenu(this->hCurPopupMenu);
		this->hCurPopupMenu = NULL;
	}

	std::vector<AssetUtilDesc> selection = getSelectedAssets();
	if (selection.empty())
		return;

	const PluginMapping& plugins = this->pContext->getPlugins();
	auto citer = plugins.options.cbegin();
	std::shared_ptr<IOptionProvider> pCurProvider;
	std::vector<std::pair<std::string, std::unique_ptr<IOptionRunner>>> viableOptions;
	while (citer = plugins.getNextOptionProvider(citer, pCurProvider), pCurProvider != nullptr)
	{
		std::string optionName;
		std::unique_ptr<IOptionRunner> pRunner;
		if (auto* pAssetListProvider = dynamic_cast<IAssetListTabOptionProvider*>(pCurProvider.get()))
		{
			pRunner = pAssetListProvider->prepareForSelection(*this->pContext, *this, selection, optionName);
		}
		else if (auto* pAssetGenericProvider = dynamic_cast<IAssetOptionProviderGeneric*>(pCurProvider.get()))
		{
			pRunner = pAssetGenericProvider->prepareForSelection(*this->pContext, selection, optionName);
		}
		if (pRunner != nullptr)
		{
			viableOptions.push_back({ std::move(optionName), std::move(pRunner) });
		}
	}

	UINT popupMenuFlags = TPM_RETURNCMD | TPM_NONOTIFY;
	if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
		popupMenuFlags |= TPM_RIGHTALIGN | TPM_HORNEGANIMATION;
	else
		popupMenuFlags |= TPM_HORPOSANIMATION;

	POINT popupPos = {};
	RECT btnRect = {};
	if (GetWindowRect(GetDlgItem(this->hDialog, IDC_PLUGINS), &btnRect))
	{
		popupPos.x = btnRect.left;
		popupPos.y = btnRect.bottom;
	}
	else
		GetCursorPos(&popupPos);

	if (viableOptions.empty())
	{
		this->hCurPopupMenu = CreatePopupMenu();
		if (this->hCurPopupMenu != NULL)
		{
			AppendMenu(this->hCurPopupMenu, MF_STRING | MF_GRAYED, 102, TEXT("(no options found)"));
			TrackPopupMenuEx(this->hCurPopupMenu, popupMenuFlags, popupPos.x, popupPos.y, this->hDialog, NULL);
			DestroyMenu(this->hCurPopupMenu);
			this->hCurPopupMenu = NULL;
		}
		return;
	}

	size_t sel = ShowContextMenu(viableOptions.size(), [&viableOptions](size_t i) {return viableOptions[i].first.c_str(); },
		popupMenuFlags, popupPos.x, popupPos.y, this->hDialog,
		this->hCurPopupMenu);
	if (sel != (size_t)-1)
		(*viableOptions[sel].second)(); //Let the plugin perform the action.
}

void AssetListDialog::openViewDataTab(size_t selection)
{
	if (selection == SIZE_MAX)
	{
		for (size_t i = 0; i < this->listEntries.size(); i++)
		{
			if (this->listEntries[i].isSelected)
			{
				selection = i;
				break;
			}
		}
		if (selection == SIZE_MAX)
			return;
	}
	AssetIdentifier identifier(this->listEntries[selection].fileID, this->listEntries[selection].pathID);
	if (!identifier.resolve(*pContext))
	{
		MessageBox(this->hDialog, TEXT("Unable to find the selected asset!"), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
		return;
	}
	AssetInfo *pFirstEntry = nullptr;
	this->cacheEntry(selection, pFirstEntry);
	std::shared_ptr<AssetViewModifyDialog> pSubDialog = 
		std::make_shared<AssetViewModifyDialog>(*this, *pContext, std::move(identifier), pFirstEntry ? pFirstEntry->name : std::string());
	if (pSubDialog->init(pSubDialog, this->hDialog))
		addModifyDialog(pSubDialog);
}

AssetUtilDesc AssetListDialog::makeExportDescForSelection(size_t selection) {
	AssetUtilDesc ret;
	unsigned int fileID = this->listEntries[selection].fileID;
	pathid_t pathID = this->listEntries[selection].pathID;

	auto fileIt = fileEntries.find(fileID);
	if (fileIt != fileEntries.end())
	{
		FileEntryCache& cache = *fileIt->second.get();
		FileContextInfo_ptr pContextInfo = cache.pUIInfo->getContextInfo();
		if (pContextInfo->getFileContext() && pContextInfo->getFileContext()->getType() == FileContext_Assets)
		{
			ret.assetsFileName = pContextInfo->getFileName();
			AssetInfo* pEntry = nullptr;
			cacheEntry(selection, pEntry);
			if (pEntry != nullptr)
				ret.assetName = pEntry->name;
			ret.asset = AssetIdentifier(std::reinterpret_pointer_cast<AssetsFileContextInfo>(pContextInfo), pathID);
			if (!ret.asset.resolve(*this->pContext))
				ret.asset = AssetIdentifier();
		}
	}
	return ret;
}

AssetListDialog::ListEntrySelectionIterator::ListEntrySelectionIterator(AssetListDialog& dialog)
	: dialog(dialog), selection(SIZE_MAX), nextSelection(SIZE_MAX)
{
	for (size_t i = 0; i < dialog.listEntries.size() && nextSelection == SIZE_MAX; i++)
	{
		if (dialog.listEntries[i].isSelected)
		{
			if (selection == SIZE_MAX)
				selection = i;
			else
				nextSelection = i;
		}
	}
}
AssetListDialog::ListEntrySelectionIterator& AssetListDialog::ListEntrySelectionIterator::operator++()
{
	selection = nextSelection;
	nextSelection = SIZE_MAX;
	if (selection != SIZE_MAX)
	{
		for (size_t i = selection + 1; i < dialog.listEntries.size() && nextSelection == SIZE_MAX; i++)
		{
			if (dialog.listEntries[i].isSelected)
			{
				nextSelection = i;
			}
		}
	}
	return *this;
}


std::vector<AssetUtilDesc> AssetListDialog::getSelectedAssets()
{
	ListEntrySelectionIterator selectionIter(*this);
	std::vector<AssetUtilDesc> selectedAssets;
	while (!selectionIter.isEnd())
	{
		selectedAssets.push_back(makeExportDescForSelection(*selectionIter));
		++selectionIter;
	}
	return selectedAssets;
}

template <class TaskGenerator>
requires std::invocable<const TaskGenerator&, std::vector<AssetUtilDesc>, std::vector<std::string>>
	&& std::convertible_to<std::invoke_result_t<const TaskGenerator&, std::vector<AssetUtilDesc>, std::vector<std::string>>,
		std::shared_ptr<AssetImportTask>>
void AssetListDialog::importAssetsBy(std::vector<AssetUtilDesc> assets,
	const TaskGenerator& taskGenerator, std::string _extension, std::string _extensionRegex, std::string _extensionFilter)
{
	std::vector<std::string> _importFilePaths = this->pContext->QueryAssetImportLocation(
		assets, std::move(_extension), std::move(_extensionRegex), std::move(_extensionFilter));
	if (_importFilePaths.size() == assets.size())
	{
		std::shared_ptr<AssetImportTask> pTask = taskGenerator(std::move(assets), std::move(_importFilePaths));
		this->pContext->taskManager.enqueue(pTask);
	}
}

template <class TaskGenerator>
requires std::invocable<const TaskGenerator&, std::vector<AssetUtilDesc>, std::string>
&& std::convertible_to<std::invoke_result_t<const TaskGenerator&, std::vector<AssetUtilDesc>, std::string>, std::shared_ptr<AssetExportTask>>
void AssetListDialog::exportAssetsBy(std::vector<AssetUtilDesc> assets, 
	const TaskGenerator& taskGenerator, std::string _extension, std::string _extensionFilter)
{
	std::string exportPath = this->pContext->QueryAssetExportLocation(assets, std::move(_extension), std::move(_extensionFilter));
	if (exportPath.empty())
		return;
	std::shared_ptr<AssetExportTask> pTask = taskGenerator(std::move(assets), std::move(exportPath));
	this->pContext->taskManager.enqueue(pTask);
}

void AssetListDialog::exportAssetsTextDump(std::vector<AssetUtilDesc> assets, bool stopOnError)
{
	return exportAssetsBy<AssetExportTextDumpTask>(std::move(assets), *this->pContext, ".txt", "*.txt|Text files:", "Export text dump", stopOnError);
}
void AssetListDialog::exportAssetsJSONDump(std::vector<AssetUtilDesc> assets, bool stopOnError)
{
	return exportAssetsBy<AssetExportJSONDumpTask>(std::move(assets), *this->pContext, ".json", "*.json|JSON text files:", "Export JSON dump", stopOnError);
}

void AssetListDialog::importAssetsRaw(std::vector<AssetUtilDesc> assets, bool stopOnError)
{
	return importAssetsBy<AssetImportRawTask>(std::move(assets), *this->pContext, ".dat", "(?:\\.dat)?", "*.*|Raw Unity asset:",
		"Import raw assets", stopOnError);
}
void AssetListDialog::importAssetsDump(std::vector<AssetUtilDesc> assets, bool stopOnError)
{
	return importAssetsBy<AssetImportDumpTask>(std::move(assets), *this->pContext, ".json", "\\.(?:json|txt)", "*.txt|UABE text dump:*.json|UABE json dump:",
		"Import asset dumps", stopOnError);
}
