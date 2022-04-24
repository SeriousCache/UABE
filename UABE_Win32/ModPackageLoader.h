#pragma once
#include "Win32AppContext.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include "FileModTree.h"
#include "Win32ModTreeDialogBase.h"

class Win32ModPackageLoader : public Win32ModTreeDialogBase
{
protected:
private:
	static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	void FillModifiedFilesTree();
	void OnCheck(HTREEITEM item, bool isChecked);
	void RemoveUnchecked();
	void OnClose();
	void OnChangeBaseFolderEdit();
public:
	Win32ModPackageLoader(Win32AppContext &appContext);

	void open();
};
