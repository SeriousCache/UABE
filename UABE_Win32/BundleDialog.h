#pragma once
#include "MainWindow2.h"
#include "Win32AppContext.h"

class BundleDialog : public IFileManipulateDialog, public MainWindowEventHandler
{
	MainWindowEventHandlerHandle eventHandlerHandle;
	class Win32AppContext *pContext;
	HWND hDialog;
	HWND hParentWnd;
	
	HWND hCurEditPopup;
	int iEditPopupItem;
	int iEditPopupSubItem;

	//Tracks LVN_ITEMCHANGED notifications (may potentially reach 2 temporarily).
	int nSelected; 

	FileEntryUIInfo *pCurFileEntry;
	std::map<unsigned int,FileEntryUIInfo*> fileEntries;
protected:
	static INT_PTR CALLBACK BundleDlgProc(HWND hWnd, UINT message, 
		WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK BundleListViewProc(HWND hWnd, UINT uMsg, 
		WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);
	static LRESULT CALLBACK EditPopupProc(HWND hWnd, UINT uMsg, 
		WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);

	void onUpdateCurrentFile();
	void doCloseEditPopup(bool applyChanges = true);
	void onOpenEditPopup();

	void onChangeSelection();

	void importItem(bool addNew);
	void exportItem();
	void removeItem();

public:
	//IFileManipulateDialog
	BundleDialog(class Win32AppContext *pContext, HWND hParentWnd);
	~BundleDialog();
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
	void onUpdateBundleEntry(BundleFileContextInfo *pFile, size_t index);
};
