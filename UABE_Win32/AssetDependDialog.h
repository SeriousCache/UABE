#pragma once
#include "MainWindow2.h"
#include "Win32AppContext.h"
#include <map>


class AssetDependDialog : public IFileManipulateDialog, public MainWindowEventHandler
{
	MainWindowEventHandlerHandle eventHandlerHandle;
	class Win32AppContext *pContext;
	HWND hDialog;
	HWND hParentWnd;

	HWND hCurEditPopup;
	int iEditPopupItem;
	int iEditPopupSubItem;

	FileEntryUIInfo *pCurFileEntry;
	std::map<unsigned int,FileEntryUIInfo*> fileEntries;
protected:
	static INT_PTR CALLBACK AssetDependProc(HWND hWnd, UINT message, 
		WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK AssetDependListViewProc(HWND hWnd, UINT uMsg, 
		WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);
	static LRESULT CALLBACK EditPopupProc(HWND hWnd, UINT uMsg, 
		WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);

	void list_updateAssetPath(HWND hList, int iItem, const AssetsFileDependency *pDependency);
	void list_updateTargetAsset(HWND hList, int iItem, unsigned int reference);
	void onUpdateCurrentFile();
	void doCloseEditPopup(bool applyChanges = true);
	void onOpenEditPopup();

public:
	//IFileManipulateDialog
	AssetDependDialog(class Win32AppContext *pContext, HWND hParentWnd);
	~AssetDependDialog();
	void addFileContext(const std::pair<FileEntryUIInfo*,uintptr_t> &context);
	void removeFileContext(FileEntryUIInfo *pContext);
	EFileManipulateDialogType getType();
	HWND getWindowHandle();
	void onHotkey(ULONG message, DWORD keyCode); //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
	bool onCommand(WPARAM wParam, LPARAM lParam); //Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
	void onShow();
	void onHide();
	bool hasUnappliedChanges(bool *applyable=nullptr);
	bool applyChanges();
	bool doesPreferNoAutoclose();

	//MainWindowEventHandler
	void onUpdateContainers(AssetsFileContextInfo *pFile);
	void onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved);
	void onUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to);
};