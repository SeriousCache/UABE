#pragma once
#include "Win32AppContext.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include "FileModTree.h"
#include "Win32ModTreeDialogBase.h"

enum EModDataSaveType
{
	ModDataSaveType_Installer,
	ModDataSaveType_PackageFile
};

class Win32ModInstallerEditor : public Win32ModTreeDialogBase
{
	EModDataSaveType saveType;
	bool changedFlag;
protected:
	std::vector<uint8_t> iconData;

	ClassDatabaseFile typesToExport;
	
private:
	static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	void UpdateDisplayedRelPaths();
	void SelectAndLoadIcon();
	void SelectAndImportPackage();
	void RemoveChange(HTREEITEM treeEntry);
	bool SaveChanges();
	void AskSave();

	bool removeChangesBy(VisibleFileEntry &file, HTREEITEM treeEntry);

	void MergeInstallerData(InstallerPackageFile &newFile);
public:
	Win32ModInstallerEditor(Win32AppContext &appContext,
		std::vector<std::shared_ptr<FileContextInfo>> &contextInfo,
		EModDataSaveType saveType);

	void open();

};
