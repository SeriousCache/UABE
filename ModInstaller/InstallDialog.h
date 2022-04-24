#pragma once
#include "InstallerDataFormat.h"

enum EInstallDialogs
{
	InstallDialog_Prepare,
	InstallDialog_Introduction,
	InstallDialog_Description,
	InstallDialog_PathSelect,
	InstallDialog_Progress,
	InstallDialog_Complete,
	InstallDialog_COUNT
};

class InstallDialogsData
{
public:
	HWND hWindow; //parent window with borders that contains the dialogs
	HWND hDialogs[InstallDialog_COUNT];
	class DialogController *pControllers[InstallDialog_COUNT];
	int activeDialogIndex;

	HANDLE dialogChangedEvent;
	HANDLE freeDialogResourcesEvent;
	HANDLE dialogThreadClosedEvent;
	bool closeWindows; bool isClosed;
	~InstallDialogsData();
};
InstallDialogsData *InitInstallDialogs(HINSTANCE hInstance);
bool ShowInstallDialog(InstallDialogsData *data, EInstallDialogs dialog);
void OnCancelPressed(InstallDialogsData *data);
EInstallDialogs WaitForDialogChanged(InstallDialogsData *data, DWORD timeout = INFINITE);
void CloseDialogThread(InstallDialogsData *data);

class DialogController
{
public:
	virtual ~DialogController();
	virtual EInstallDialogs GetOwnDialogType() = 0;
	virtual HWND GetDialogHandle() = 0;
	virtual HWND GetParentWindow() = 0;
	virtual InstallDialogsData *GetDialogsData() = 0;
};
enum EInstallDialogPrepareStatus
{
	InstallDialogPrepareStatus_LoadInstData,
	InstallDialogPrepareStatus_SIZE
};
enum EInstallDialogPrepareStatusValue
{
	InstallPrepareStatus_Inactive,
	InstallPrepareStatus_Active,
	InstallPrepareStatus_Completed,
	InstallPrepareStatus_Error
};
class DialogControllerBase : public DialogController
{
protected:
	HWND hDialog;
	HWND hParentWindow;
	InstallDialogsData *dialogsData;
public:
	DialogControllerBase(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogControllerBase();
	HWND GetDialogHandle();
	HWND GetParentWindow();
	InstallDialogsData *GetDialogsData();
};
class DialogControllerTitled : public DialogControllerBase
{
protected:
	HWND hStaticTitle; bool bAppendTitle;
public:
	DialogControllerTitled(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData, bool appendSetup);
	~DialogControllerTitled();

	void SetModName(const char *modName);
};

class DialogController_Prepare : public DialogControllerBase
{
	HWND hProcessCheckboxes[InstallDialogPrepareStatus_SIZE];
public:
	DialogController_Prepare(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_Prepare();
	EInstallDialogs GetOwnDialogType();

	void SetStatus(EInstallDialogPrepareStatus stat, EInstallDialogPrepareStatusValue value);
};
class DialogController_Introduction : public DialogControllerTitled
{
protected:
public:
	DialogController_Introduction(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_Introduction();
	EInstallDialogs GetOwnDialogType();
};
class DialogController_Description : public DialogControllerTitled
{
protected:
	HWND hStaticAuthors;
	HWND hStaticDescription;
public:
	DialogController_Description(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_Description();
	EInstallDialogs GetOwnDialogType();
	
	void SetAuthors(const char *authors);
	void SetDescription(const char *description);
};
class DialogController_PathSelect : public DialogControllerTitled
{
protected:
	HWND hEditPath;
	HWND hTreeModifications;
public:
	DialogController_PathSelect(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_PathSelect();
	EInstallDialogs GetOwnDialogType();
	
	TCHAR *GetPath(size_t &pathLen);
	void SetPath(const TCHAR *path);
	void FillModsTree(InstallerPackageFile *pInstallFile);
};
class DialogController_Progress : public DialogControllerTitled
{
protected:
	HWND hInstallingText;
	HWND hProgressBar;
	HWND hCurFileText;
	HWND hLogEdit;
	HWND hBtnNext;
	HWND hBtnCancel;
	bool cancelled;
public:
	DialogController_Progress(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_Progress();
	EInstallDialogs GetOwnDialogType();

	bool GetCancelled();
	void SetCancelled(bool cancelled);

	void SetPaused(bool paused);
	//negative percent -> error
	void SetProgress(float percent, const char *curFileName);
	void AddToLog(const char *logText);
	void AddToLog(const wchar_t *logText);
	void EnableContinue();
	void DisableCancel();
};
class DialogController_Complete : public DialogControllerTitled
{
protected:
	HWND hStaticCompleteText;
	HWND hStaticAuthors;
public:
	DialogController_Complete(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
	~DialogController_Complete();
	EInstallDialogs GetOwnDialogType();
	
	void SetAuthors(const char *authors);
	void SetCompleteText(const TCHAR *text);
};