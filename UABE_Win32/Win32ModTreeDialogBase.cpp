#include "stdafx.h"
#include "Win32ModTreeDialogBase.h"
#include "../libStringConverter/convert.h"
#include <InternalBundleReplacer.h>
#include <WindowsX.h>

static std::string MakeAssetsReplacerDescription(AssetsReplacer *pReplacer)
{
	switch (pReplacer->GetType())
	{
	case AssetsReplacement_AddOrModify:
		return std::string("Add/Replace PathID ") + std::to_string((int64_t)reinterpret_cast<AssetsEntryReplacer*>(pReplacer)->GetPathID());
	case AssetsReplacement_Remove:
		return std::string("Remove PathID ") + std::to_string((int64_t)reinterpret_cast<AssetsEntryReplacer*>(pReplacer)->GetPathID());
	case AssetsReplacement_Dependencies:
		return std::string("Modify dependencies");
	default:
		assert(false);
		return std::string("(??)");
	}
}
void Win32ModTreeDialogBase::addTreeViewNode_AssetsReplacer(HTREEITEM hParent, VisibleReplacerEntry &entry)
{
	TVINSERTSTRUCT insert = {};
	insert.hParent = hParent;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.hwnd = NULL;
	insert.itemex.hItem = NULL;
	insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
	insert.hInsertAfter = TVI_FIRST;
	std::string replacerDesc = MakeAssetsReplacerDescription(reinterpret_cast<AssetsReplacer*>(entry.pReplacer.get()));
	auto replacerDescT = unique_MultiByteToTCHAR(replacerDesc.c_str());
	insert.itemex.pszText = replacerDescT.get();
	insert.itemex.cchTextMax = 0;
	insert.itemex.cChildren = 0;
	entry.treeItem = (uintptr_t)TreeView_InsertItem(hTreeModifications, &insert);
}
static std::string MakeBundleReplacerDescription(BundleReplacer *pReplacer)
{
	const char *_origName = pReplacer->GetOriginalEntryName();
	const char *_newName = pReplacer->GetEntryName();
	std::string origName(_origName ? _origName : "");
	std::string newName(_newName ? _newName : "");
	switch (pReplacer->GetType())
	{
	case BundleReplacement_Rename:
		return std::string("Rename ") + origName + " to " + newName;
	case BundleReplacement_AddOrModify:
		if (newName.empty())
			return std::string("Modify and rename ") + origName;
		if (origName.empty())
			return std::string("Modify/create ") + newName;
		return std::string("Modify and rename ") + origName + " to " + newName;
	case BundleReplacement_Remove:
		return std::string("Remove ") + origName;
	default:
		assert(false);
		return std::string("(??) ") + origName;
	}
}
void Win32ModTreeDialogBase::addTreeViewNode_BundleReplacer(HTREEITEM hParent, VisibleReplacerEntry &entry)
{
	TVINSERTSTRUCT insert = {};
	insert.hParent = hParent;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.hwnd = NULL;
	insert.itemex.hItem = NULL;
	insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
	insert.hInsertAfter = TVI_FIRST;
	std::string replacerDesc = MakeBundleReplacerDescription(reinterpret_cast<BundleReplacer*>(entry.pReplacer.get()));
	auto replacerDescT = unique_MultiByteToTCHAR(replacerDesc.c_str());
	insert.itemex.pszText = replacerDescT.get();
	insert.itemex.cchTextMax = 0;
	insert.itemex.cChildren = 0;
	entry.treeItem = (uintptr_t)TreeView_InsertItem(hTreeModifications, &insert);
}
void Win32ModTreeDialogBase::addTreeViewNode_ResourcesReplacer(HTREEITEM hParent, VisibleReplacerEntry& entry)
{
	BundleReplacer* pReplacer = reinterpret_cast<BundleReplacer*>(entry.pReplacer.get());
	auto* pResourcesReplacer = dynamic_cast<BundleEntryModifierByResources*>(pReplacer);
	if (pResourcesReplacer == nullptr)
	{
		assert(false);
		return;
	}
	TVINSERTSTRUCT insert = {};
	insert.hParent = hParent;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.hwnd = NULL;
	insert.itemex.hItem = NULL;
	insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
	insert.hInsertAfter = TVI_FIRST;
	insert.itemex.pszText = const_cast<TCHAR*>(
		pResourcesReplacer->RequiresEntryReader() ? TEXT("Modify") : TEXT("Add or replace")
		);
	insert.itemex.cchTextMax = 0;
	insert.itemex.cChildren = 0;
	entry.treeItem = (uintptr_t)TreeView_InsertItem(hTreeModifications, &insert);
}
void Win32ModTreeDialogBase::updateTreeViewNode_File(HTREEITEM hParent, VisibleFileEntry &file, bool showReplacers)
{
	if (file.treeViewEntry == NULL)
	{
		size_t pathLen;
		auto tcPath = unique_MultiByteToTCHAR(file.pathNull ? file.newName.c_str() : file.pathOrName.c_str(), pathLen);
		TVINSERTSTRUCT insert = {};
		insert.hParent = NULL;
		insert.itemex.state = 0;
		insert.itemex.stateMask = 0xFF;
		insert.itemex.hwnd = NULL;
		insert.itemex.hItem = NULL;
		insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
		insert.itemex.pszText = tcPath.get();
		insert.itemex.cchTextMax = (int)((pathLen + 1) & 0x7FFFFFFF);
		insert.hInsertAfter = TVI_FIRST;
		insert.hParent = hParent;
		insert.itemex.cChildren = ((showReplacers && file.replacers.size() > 0) || (file.fileType == FileContext_Bundle && file.subFiles.size() > 0))
			                                   ? 1
			                                   : 0;
		file.treeViewEntry = (uintptr_t)TreeView_InsertItem(hTreeModifications, &insert);
	}
	switch (file.fileType)
	{
	case FileContext_Bundle:
		for (size_t _i = file.subFiles.size(); _i > 0; --_i)
		{
			size_t i = _i - 1;
			updateTreeViewNode_File((HTREEITEM)file.treeViewEntry, file.subFiles[i], showReplacers);
		}
		if (showReplacers)
			for (size_t _i = file.replacers.size(); _i > 0; --_i)
			{
				size_t i = _i - 1;
				if (file.replacers[i].treeItem == NULL)
					addTreeViewNode_BundleReplacer((HTREEITEM)file.treeViewEntry, file.replacers[i]);
			}
		break;
	case FileContext_Assets:
		if (showReplacers)
			for (size_t _i = file.replacers.size(); _i > 0; --_i)
			{
				size_t i = _i - 1;
				if (file.replacers[i].treeItem == NULL)
					addTreeViewNode_AssetsReplacer((HTREEITEM)file.treeViewEntry, file.replacers[i]);
			}
		break;
	case FileContext_Resources:
		if (showReplacers && file.replacers.size() == 1 && file.replacers[0].treeItem == NULL)
		{
			addTreeViewNode_ResourcesReplacer((HTREEITEM)file.treeViewEntry, file.replacers[0]);
		}
		break;
	default:
		assert(false);
		break;
	}
}

void Win32ModTreeDialogBase::UpdateModsTree(bool showReplacers)
{
	size_t numBundle = 0, numAssets = 0, numResources = 0;
	for (size_t i = 0; i < this->visibleFiles.size(); i++)
	{
		switch (this->visibleFiles[i].fileType)
		{
		case FileContext_Bundle:
			numBundle++;
			break;
		case FileContext_Assets:
			numAssets++;
			break;
		case FileContext_Resources:
			numResources++;
			break;
		default:
			assert(false);
		}
	}

	TVINSERTSTRUCT insert = {};
	insert.hParent = NULL;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.hwnd = NULL;
	if (this->bundleBaseEntry == NULL)
	{
		insert.itemex.hItem = NULL;
		insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
		insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected bundles"));
		insert.hInsertAfter = TVI_FIRST;
		insert.itemex.cchTextMax = (int)_tcslen(insert.itemex.pszText);
		insert.itemex.cChildren = numBundle ? 1 : 0;
		this->bundleBaseEntry = TreeView_InsertItem(hTreeModifications, &insert);
	}
	else
	{
		insert.itemex.hItem = this->bundleBaseEntry;
		insert.itemex.mask = TVIF_CHILDREN;
		insert.itemex.cChildren = numBundle ? 1 : 0;
		TreeView_SetItem(hTreeModifications, &insert.itemex);
	}
	
	if (this->assetsBaseEntry == NULL)
	{
		insert.itemex.hItem = NULL;
		insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
		insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected assets files"));
		insert.hInsertAfter = TVI_LAST;
		insert.itemex.cchTextMax = (int)_tcslen(insert.itemex.pszText);
		insert.itemex.cChildren = numAssets ? 1 : 0;
		this->assetsBaseEntry = TreeView_InsertItem(hTreeModifications, &insert);
	}
	else
	{
		insert.itemex.hItem = this->assetsBaseEntry;
		insert.itemex.mask = TVIF_CHILDREN;
		insert.itemex.cChildren = numAssets ? 1 : 0;
		TreeView_SetItem(hTreeModifications, &insert.itemex);
	}

	if (this->resourcesBaseEntry == NULL)
	{
		insert.itemex.hItem = NULL;
		insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
		insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected resource files"));
		insert.hInsertAfter = TVI_LAST;
		insert.itemex.cchTextMax = (int)_tcslen(insert.itemex.pszText);
		insert.itemex.cChildren = numResources ? 1 : 0;
		this->resourcesBaseEntry = TreeView_InsertItem(hTreeModifications, &insert);
	}
	else
	{
		insert.itemex.hItem = this->resourcesBaseEntry;
		insert.itemex.mask = TVIF_CHILDREN;
		insert.itemex.cChildren = numResources ? 1 : 0;
		TreeView_SetItem(hTreeModifications, &insert.itemex);
	}

	
	for (size_t _i = this->visibleFiles.size(); _i > 0; --_i)
	{
		size_t i = _i - 1;
		switch (this->visibleFiles[i].fileType)
		{
		case FileContext_Bundle:
			updateTreeViewNode_File(this->bundleBaseEntry, this->visibleFiles[i], showReplacers);
			break;
		case FileContext_Assets:
			updateTreeViewNode_File(this->assetsBaseEntry, this->visibleFiles[i], showReplacers);
			break;
		case FileContext_Resources:
			updateTreeViewNode_File(this->resourcesBaseEntry, this->visibleFiles[i], showReplacers);
			break;
		default:
			assert(false);
		}
	}
}
