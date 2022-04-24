#pragma once
#include "Win32AppContext.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include "FileModTree.h"
#include <vector>


class Win32ModTreeDialogBase
{
protected:
	Win32AppContext &appContext;
	std::vector<VisibleFileEntry> visibleFiles;

	HWND hDlg;
	HWND hTreeModifications;
	HTREEITEM bundleBaseEntry, assetsBaseEntry, resourcesBaseEntry;
	
	void updateTreeViewNode_File(HTREEITEM hParent, VisibleFileEntry &file, bool showReplacers);
	void addTreeViewNode_AssetsReplacer(HTREEITEM hParent, VisibleReplacerEntry &entry);
	void addTreeViewNode_BundleReplacer(HTREEITEM hParent, VisibleReplacerEntry &entry);
	void addTreeViewNode_ResourcesReplacer(HTREEITEM hParent, VisibleReplacerEntry& entry);
	void UpdateModsTree(bool showReplacers = true);

	inline Win32ModTreeDialogBase(Win32AppContext &appContext)
		: appContext(appContext), hDlg(NULL), hTreeModifications(NULL), bundleBaseEntry(NULL), assetsBaseEntry(NULL), resourcesBaseEntry(NULL)
	{}
};
