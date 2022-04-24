#include "stdafx.h"
#include "InstallDialog.h"
#include "resource.h"
#include "../libStringConverter/convert.h"
#include "../AssetsTools/InternalBundleReplacer.h"
#include "../UABE_Win32/FileDialog.h"
#include <ObjBase.h>

LRESULT CALLBACK InstallWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DescriptionDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK PrepareDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

ATOM RegisterInstallWindowClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= InstallWndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MODINSTALLER));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+0);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= L"UABE_ModInstaller";
	wcex.hIconSm		= NULL;

	return RegisterClassEx(&wcex);
}
LRESULT CALLBACK InstallWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;

	switch (message)
	{
		case WM_COMMAND:
			wmId    = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			return DefWindowProc(hWnd, message, wParam, lParam);
		case WM_SIZE:
			{
				uint16_t width = LOWORD(lParam); uint16_t height = HIWORD(lParam);
				InstallDialogsData *pData = (InstallDialogsData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
				for (int i = 0; i < InstallDialog_COUNT; i++)
				{
					MoveWindow(pData->hDialogs[i], 0, 0, width, height, TRUE);
				}
			}
			return (LRESULT)1;
		case WM_DESTROY:
			{
				InstallDialogsData *pData = (InstallDialogsData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
				pData->closeWindows = true;
				//PostQuitMessage(0);
			}
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}
void SetTitleFont(HWND hStatic)
{
	HFONT origFont = (HFONT)SendMessage(hStatic, WM_GETFONT, 0, 0);
	LOGFONT logfont = {};
	GetObject(origFont, sizeof(LOGFONT), &logfont);
	logfont.lfWeight = FW_BOLD;
	logfont.lfHeight = -16;
	HFONT newFont = CreateFontIndirect(&logfont);
	SendMessage(hStatic, WM_SETFONT, (WPARAM)newFont, (LPARAM)FALSE);
}
INT_PTR CALLBACK PrepareDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}
#include "Licences.h"
INT_PTR CALLBACK LicenceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hLicencesEdit = GetDlgItem(hDlg, IDC_LICENCES);
			Edit_SetText(hLicencesEdit, LicencesText);
		}
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
INT_PTR CALLBACK IntroductionDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			SetTitleFont(hSetupTitle);
		}
		return (INT_PTR)TRUE;
	case WM_DESTROY:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HFONT modifiedFont = (HFONT)SendMessage(hSetupTitle, WM_GETFONT, 0, 0);
			DeleteObject((HGDIOBJ)modifiedFont);
		}
		return (INT_PTR)TRUE;
	case WM_SIZE:
		{
			int width = LOWORD(lParam); int height = HIWORD(lParam);
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HWND hIntroduction = GetDlgItem(hDlg, IDC_SINTRODUCTION);
			HWND hLicencelink = GetDlgItem(hDlg, IDC_LICENCELINK);
			HWND hBack = GetDlgItem(hDlg, IDC_BACK);
			HWND hNext = GetDlgItem(hDlg, IDC_NEXT);
			HWND hCancel = GetDlgItem(hDlg, IDC_CANCEL);
			SetWindowPos(hSetupTitle, NULL, 0, 0, width - 2, 18, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hIntroduction, NULL, 0, 0, width - 8, height - 56, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hLicencelink, NULL, 5, height - 26, width / 5 - 10, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hBack, NULL, width / 5, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hNext, NULL, 9 * width / 20 + 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hCancel, NULL, width - width / 4 - 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_NEXT)
		{
			DialogController_Introduction *pController = (DialogController_Introduction*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() + 1));
		}
		else if (LOWORD(wParam) == IDC_CANCEL)
		{
			DialogController_Introduction *pController = (DialogController_Introduction*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			OnCancelPressed(pController->GetDialogsData());
		}
		else
			break;
		return (INT_PTR)TRUE;
	case WM_NOTIFY:
		if (((NMHDR*)lParam)->idFrom == IDC_LICENCELINK)
		{
			switch (((NMHDR*)lParam)->code)
			{
				case NM_CLICK:
				case NM_RETURN:
					DialogBox((HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE), MAKEINTRESOURCE(IDD_LICENCES), hDlg, LicenceDlgProc);
					break;
			}
		}
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK DescriptionDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			SetTitleFont(hSetupTitle);
		}
		return (INT_PTR)TRUE;
	case WM_DESTROY:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HFONT modifiedFont = (HFONT)SendMessage(hSetupTitle, WM_GETFONT, 0, 0);
			DeleteObject((HGDIOBJ)modifiedFont);
		}
		return (INT_PTR)TRUE;
	case WM_SIZE:
		{
			int width = LOWORD(lParam); int height = HIWORD(lParam);
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HWND hCancel = GetDlgItem(hDlg, IDC_CANCEL);
			HWND hAuthors = GetDlgItem(hDlg, IDC_AUTHORS);
			HWND hDescription = GetDlgItem(hDlg, IDC_DESCRIPTION);
			HWND hBack = GetDlgItem(hDlg, IDC_BACK);
			HWND hNext = GetDlgItem(hDlg, IDC_NEXT);
			SetWindowPos(hSetupTitle, NULL, 0, 0, width - 2, 18, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hAuthors, NULL, 0, 0, width - 4, 32, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hDescription, NULL, 0, 0, width - 8, height - 88, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hCancel, NULL, width - width / 4 - 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hBack, NULL, width / 5, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hNext, NULL, 9 * width / 20 + 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		{
			DialogController_Description *pController = (DialogController_Description*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			switch (LOWORD(wParam))
			{
				case IDC_BACK:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() - 1));
					break;
				case IDC_NEXT:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() + 1));
					break;
				case IDC_CANCEL:
					OnCancelPressed(pController->GetDialogsData());
					break;
				default:
					return (INT_PTR)FALSE;
			}
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK PathSelectDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			SetTitleFont(hSetupTitle);
		}
		return (INT_PTR)TRUE;
	case WM_DESTROY:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HFONT modifiedFont = (HFONT)SendMessage(hSetupTitle, WM_GETFONT, 0, 0);
			DeleteObject((HGDIOBJ)modifiedFont);
		}
		return (INT_PTR)TRUE;
	case WM_SIZE:
		{
			int width = LOWORD(lParam); int height = HIWORD(lParam);
			HWND hNext = GetDlgItem(hDlg, IDC_NEXT);
			HWND hBack = GetDlgItem(hDlg, IDC_BACK);
			HWND hCancel = GetDlgItem(hDlg, IDC_CANCEL);
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HWND hEditPath = GetDlgItem(hDlg, IDC_EDITPATH);
			HWND hBtnPathSelect = GetDlgItem(hDlg, IDC_BTNPATHSELECT);
			HWND hTreeMods = GetDlgItem(hDlg, IDC_TREEMODS);
			SetWindowPos(hNext, NULL, 9 * width / 20 + 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hBack, NULL, width / 5, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hCancel, NULL, width - width / 4 - 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hSetupTitle, NULL, 0, 0, width - 2, 18, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hEditPath, NULL, 5, 56, (int)((float)width / 10.0F * 8.0F) - 12, 26, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hBtnPathSelect, NULL, width - width / 5 - 5, 56, (width - 10) / 5 - 2, 24, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hTreeMods, NULL, 6, 86, width - 11, height - 116, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		{
			DialogController_PathSelect *pController = (DialogController_PathSelect*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			switch (LOWORD(wParam))
			{
				case IDC_BACK:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() - 1));
					break;
				case IDC_NEXT:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() + 1));
					break;
				case IDC_CANCEL:
					OnCancelPressed(pController->GetDialogsData());
					break;
				case IDC_BTNPATHSELECT:
					{
						HWND hEditPath = GetDlgItem(hDlg, IDC_EDITPATH);
						WCHAR *pFolder = NULL;
						if (hEditPath && ShowFolderSelectDialog(hDlg, &pFolder))
						{
							SetWindowTextW(hEditPath, pFolder);
						}
						if (pFolder)
							FreeCOMFilePathBuf(&pFolder);
					}
					break;
				default:
					return (INT_PTR)FALSE;
			}
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK ProgressDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			SetTitleFont(hSetupTitle);
		}
		return (INT_PTR)TRUE;
	case WM_DESTROY:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HFONT modifiedFont = (HFONT)SendMessage(hSetupTitle, WM_GETFONT, 0, 0);
			DeleteObject((HGDIOBJ)modifiedFont);
		}
		return (INT_PTR)TRUE;
	case WM_SIZE:
		{
			int width = LOWORD(lParam); int height = HIWORD(lParam);
			HWND hNext = GetDlgItem(hDlg, IDC_NEXT);
			HWND hCancel = GetDlgItem(hDlg, IDC_CANCEL);
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HWND hProgInstall = GetDlgItem(hDlg, IDC_PROGINSTALL);
			HWND hSCurFile = GetDlgItem(hDlg, IDC_SCURFILE);
			HWND hEditStatus = GetDlgItem(hDlg, IDC_EDITSTATUS);
			SetWindowPos(hNext, NULL, 9 * width / 20 + 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hCancel, NULL, width - width / 4 - 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hSetupTitle, NULL, 0, 0, width - 2, 18, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hProgInstall, NULL, 5, 70, width - 10, 20, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hSCurFile, NULL, 5, 90, width - 10, 18, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hEditStatus, NULL, 6, 108, width - 11, height - 134, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		{
			DialogController_Progress *pController = (DialogController_Progress*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			switch (LOWORD(wParam))
			{
				case IDC_NEXT:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() + 1));
					break;
				case IDC_CANCEL:
					{
						DWORD result = MessageBox(hDlg, 
							TEXT("Do you really want to cancel the mod installation?\r\n")\
							TEXT("The installer will finish the current operation and then revert the changes."), 
							TEXT("UABE Mod Installer"), MB_YESNO | MB_ICONINFORMATION);
						if (result == IDYES)
						{
							pController->SetCancelled(true);
						}
					}
					break;
				default:
					return (INT_PTR)FALSE;
			}
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK CompleteDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			SetTitleFont(hSetupTitle);
		}
		return (INT_PTR)TRUE;
	case WM_SIZE:
		{
			int width = LOWORD(lParam); int height = HIWORD(lParam);
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HWND hClose = GetDlgItem(hDlg, IDC_CLOSE);
			HWND hCompleteText = GetDlgItem(hDlg, IDC_COMPLETETEXT);
			HWND hModAuthors = GetDlgItem(hDlg, IDC_MODAUTHORS);
			HWND hInstAuthor = GetDlgItem(hDlg, IDC_INSTAUTHOR);
			HWND hBack = GetDlgItem(hDlg, IDC_BACK);
			SetWindowPos(hSetupTitle, NULL, 0, 0, width - 2, 18, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hCompleteText, NULL, 2, 20, width - 4, height - 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hModAuthors, NULL, 2, height - 100, width - 4, 40, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hInstAuthor, NULL, 2, height - 60, width - 4, 20, SWP_NOACTIVATE);
			SetWindowPos(hClose, NULL, width - width / 4 - 4, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
			SetWindowPos(hBack, NULL, width / 5, height - 26, (width - 10) / 4, 23, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return (INT_PTR)TRUE;
	case WM_DESTROY:
		{
			HWND hSetupTitle = GetDlgItem(hDlg, IDC_SETUPTITLE);
			HFONT modifiedFont = (HFONT)SendMessage(hSetupTitle, WM_GETFONT, 0, 0);
			DeleteObject((HGDIOBJ)modifiedFont);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		{
			DialogController_Complete *pController = (DialogController_Complete*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			switch (LOWORD(wParam))
			{
				case IDC_BACK:
					ShowInstallDialog(pController->GetDialogsData(), (EInstallDialogs)(pController->GetOwnDialogType() - 1));
					break;
				case IDC_CLOSE:
					pController->GetDialogsData()->closeWindows = true;
					break;
				default:
					return (INT_PTR)FALSE;
			}
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

typedef DialogController*(__cdecl *DialogControllerConstructor)(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData);
DialogController *ConstructPrepareDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_Prepare(hDialog, hParentWindow, dialogsData);
}
DialogController *ConstructIntroductionDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_Introduction(hDialog, hParentWindow, dialogsData);
}
DialogController *ConstructDescriptionDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_Description(hDialog, hParentWindow, dialogsData);
}
DialogController *ConstructPathSelectDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_PathSelect(hDialog, hParentWindow, dialogsData);
}
DialogController *ConstructProgressDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_Progress(hDialog, hParentWindow, dialogsData);
}
DialogController *ConstructCompleteDialogController(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	return new DialogController_Complete(hDialog, hParentWindow, dialogsData);
}
const struct {uint16_t id; DLGPROC proc; DialogControllerConstructor constr;} InstallDialogCreateInfo[] = {
	{ IDD_PREPARE, PrepareDlgProc, ConstructPrepareDialogController},
	{ IDD_INTRODUCTION, IntroductionDlgProc, ConstructIntroductionDialogController},
	{ IDD_DESCRIPTION, DescriptionDlgProc, ConstructDescriptionDialogController},
	{ IDD_PATHSELECT, PathSelectDlgProc, ConstructPathSelectDialogController},
	{ IDD_PROGRESS, ProgressDlgProc, ConstructProgressDialogController},
	{ IDD_COMPLETE, CompleteDlgProc, ConstructCompleteDialogController},
};

struct DialogMessageThreadParam
{
	InstallDialogsData *ret;
	HINSTANCE hInstance;
	HANDLE readyEvent;
};
DWORD WINAPI InstallDialogsMessageThread(PVOID _param)
{
	DialogMessageThreadParam *param = (DialogMessageThreadParam*)_param;
	InstallDialogsData *dialogsData = param->ret;
	HINSTANCE hInstance = param->hInstance;
	{
		InstallDialogsData *ret = param->ret;
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | 
			COINIT_DISABLE_OLE1DDE);
		if (!FAILED(hr))
		{
			INITCOMMONCONTROLSEX init;
			init.dwSize = sizeof(init);
			init.dwICC = ICC_PROGRESS_CLASS | ICC_LINK_CLASS;
			if (!InitCommonControlsEx(&init))
				hr = E_FAIL;
		}
		if (FAILED(hr))
		{
			TCHAR sprntTmp[100];
			_stprintf_s(sprntTmp, TEXT("Fatal error : Unable to initialize the COM (HRESULT %X)!"), hr);
			MessageBox(NULL, sprntTmp, TEXT("ERROR"), 16);
			CoUninitialize();
			delete ret;
			param->ret = NULL;
			SetEvent(param->readyEvent);
			return NULL;
		}
		ret->dialogChangedEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
		ret->freeDialogResourcesEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		ret->dialogThreadClosedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		RegisterInstallWindowClass(hInstance);
		ret->hWindow = CreateWindow(L"UABE_ModInstaller", L"UABE Mod Installer", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, 100, 100, NULL, NULL, hInstance, NULL);
		if (ret->hWindow == NULL)
		{
			CloseHandle(ret->dialogThreadClosedEvent);
			CloseHandle(ret->freeDialogResourcesEvent);
			CloseHandle(ret->dialogChangedEvent);
			CoUninitialize();
			delete ret;
			param->ret = NULL;
			SetEvent(param->readyEvent);
			return NULL;
		}
		SetWindowLongPtr(ret->hWindow, GWLP_USERDATA, (LONG_PTR)dialogsData);
		for (unsigned int i = 0; i < InstallDialog_COUNT; i++)
		{
			ret->hDialogs[i] = 
				CreateDialog(hInstance, MAKEINTRESOURCE(InstallDialogCreateInfo[i].id), ret->hWindow, InstallDialogCreateInfo[i].proc);
			if (!ret->hDialogs[i])
			{
				DWORD test = GetLastError();
				DestroyWindow(ret->hWindow);
				CloseHandle(ret->dialogThreadClosedEvent);
				CloseHandle(ret->freeDialogResourcesEvent);
				CloseHandle(ret->dialogChangedEvent);
				CoUninitialize();
				delete ret;
				param->ret = NULL;
				SetEvent(param->readyEvent);
				return 0;
			}
		}
		for (unsigned int i = 0; i < InstallDialog_COUNT; i++)
		{
			ret->pControllers[i] = InstallDialogCreateInfo[i].constr(ret->hDialogs[i], ret->hWindow, ret);
		}
		int borderWidth, borderHeight;
		{
			RECT oldWinRect;
			GetWindowRect(ret->hWindow, &oldWinRect);
			RECT oldClnRect;
			GetClientRect(ret->hWindow, &oldClnRect);
			borderWidth = (oldWinRect.right - oldWinRect.left) - (oldClnRect.right - oldClnRect.left);
			borderHeight = (oldWinRect.bottom - oldWinRect.top) - (oldClnRect.bottom - oldClnRect.top);
		}
		ret->activeDialogIndex = InstallDialog_Prepare;
		/*RECT prepareRect;
		GetClientRect(ret->hDialogs[InstallDialog_Prepare], &prepareRect);
		SetWindowPos(ret->hWindow, NULL, 0, 0,
			prepareRect.right - prepareRect.left + borderWidth,
			prepareRect.bottom - prepareRect.top + borderHeight,
			SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOZORDER);*/
		SetWindowPos(ret->hWindow, NULL, 0, 0,
			500 + borderWidth,
			360 + borderHeight,
			SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOZORDER);
		ShowWindow(ret->hWindow, SW_SHOWDEFAULT);
		UpdateWindow(ret->hWindow);
		
		SetEvent(param->readyEvent);
	}

	{
		int noMessageCounter = 0;
		while (!dialogsData->closeWindows)
		{
			MSG msg;
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				noMessageCounter = 0;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				if (!IsWindow(dialogsData->hWindow))
					break;
				if (noMessageCounter < 100)
				{
					noMessageCounter++;
					Sleep(1);
				}
				else
					Sleep(10);
			}
		}
		while (!dialogsData->closeWindows)
			Sleep(10);
		DestroyWindow(dialogsData->hWindow);
		dialogsData->hWindow = NULL;
		dialogsData->closeWindows = false;
		SetEvent(dialogsData->dialogChangedEvent);
		WaitForSingleObject(dialogsData->freeDialogResourcesEvent, INFINITE);
		CloseHandle(dialogsData->freeDialogResourcesEvent); dialogsData->freeDialogResourcesEvent = NULL;
		CloseHandle(dialogsData->dialogChangedEvent); dialogsData->dialogChangedEvent = NULL;
		for (unsigned int i = 0; i < InstallDialog_COUNT; i++)
		{
			delete dialogsData->pControllers[i];
		}
		dialogsData->dialogChangedEvent = NULL;
		dialogsData->isClosed = true;
		CoUninitialize();
		SetEvent(dialogsData->dialogThreadClosedEvent);
	}
	return 0;
}
InstallDialogsData *InitInstallDialogs(HINSTANCE hInstance)
{
	InstallDialogsData *ret = new InstallDialogsData();
	memset(ret, 0, sizeof(InstallDialogsData));
	DialogMessageThreadParam threadParam;
	threadParam.ret = ret;
	threadParam.hInstance = hInstance;
	threadParam.readyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	HANDLE hThread = CreateThread(NULL, 0, InstallDialogsMessageThread, &threadParam, 0, NULL);
	if (!hThread)
	{
		CloseHandle(threadParam.readyEvent);
		delete ret;
		return NULL;
	}
	CloseHandle(hThread);
	while (WaitForSingleObject(threadParam.readyEvent, INFINITE) != WAIT_OBJECT_0){}
	CloseHandle(threadParam.readyEvent);
	return threadParam.ret;
}

bool ShowInstallDialog(InstallDialogsData *data, EInstallDialogs dialog)
{
	if (!IsWindow(data->hWindow))
		return false;
	ShowWindow(data->hDialogs[data->activeDialogIndex], SW_HIDE);
	data->activeDialogIndex = (int)dialog;
	ShowWindow(data->hDialogs[dialog], SW_SHOW);
	SetEvent(data->dialogChangedEvent);
	return true;
}
EInstallDialogs WaitForDialogChanged(InstallDialogsData *data, DWORD timeout)
{
	if (data->hWindow != NULL && WaitForSingleObject(data->dialogChangedEvent, timeout) == WAIT_OBJECT_0)
		return data->pControllers[data->activeDialogIndex]->GetOwnDialogType();
	else
		return (EInstallDialogs)-1;
}
void OnCancelPressed(InstallDialogsData *data)
{
	DWORD result = MessageBox(data->hWindow, 
		TEXT("Do you really want to cancel the mod installation?"), 
		TEXT("UABE Mod Installer"), MB_YESNO | MB_ICONINFORMATION);
	if (result == IDYES)
	{
		data->closeWindows = true;
	}
}
void CloseDialogThread(InstallDialogsData *data)
{
	//if (WaitForSingleObject(data->dialogThreadClosedEvent, 0) != WAIT_OBJECT_0)
	{
		data->closeWindows = true;
		SetEvent(data->freeDialogResourcesEvent);
		WaitForSingleObject(data->dialogThreadClosedEvent, INFINITE);
		CloseHandle(data->dialogThreadClosedEvent); data->dialogThreadClosedEvent = NULL;
	}
}

InstallDialogsData::~InstallDialogsData()
{
	//if (IsWindow(this->hWindow))
	if (this->hWindow != NULL)
	{
		this->closeWindows = true;
		while (!this->isClosed) Sleep(0);
	}
}

DialogController::~DialogController(){}
DialogControllerBase::DialogControllerBase(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
{
	this->hDialog = hDialog;
	this->hParentWindow = hParentWindow;
	this->dialogsData = dialogsData;
	SetWindowLongPtr(hDialog, GWLP_USERDATA, (LONG_PTR)this);
}
DialogControllerBase::~DialogControllerBase(){}
HWND DialogControllerBase::GetDialogHandle() { return hDialog; }
HWND DialogControllerBase::GetParentWindow() { return hParentWindow; }
InstallDialogsData *DialogControllerBase::GetDialogsData() { return dialogsData; }

DialogController_Prepare::DialogController_Prepare(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerBase(hDialog, hParentWindow, dialogsData)
{
	hProcessCheckboxes[0] = GetDlgItem(hDialog, IDC_CKLOADDATA);
}
DialogController_Prepare::~DialogController_Prepare(){}
EInstallDialogs DialogController_Prepare::GetOwnDialogType() { return InstallDialog_Prepare; }
void DialogController_Prepare::SetStatus(EInstallDialogPrepareStatus stat, EInstallDialogPrepareStatusValue value)
{
	if (stat < InstallDialogPrepareStatus_SIZE)
	{
		switch (value)
		{
			case InstallPrepareStatus_Inactive:
				Button_SetCheck(hProcessCheckboxes[stat], BST_UNCHECKED);
				break;
			case InstallPrepareStatus_Active:
				Button_SetCheck(hProcessCheckboxes[stat], BST_INDETERMINATE);
				break;
			case InstallPrepareStatus_Completed:
				Button_SetCheck(hProcessCheckboxes[stat], BST_CHECKED);
				break;
			case InstallPrepareStatus_Error:
				{
					Button_SetCheck(hProcessCheckboxes[stat], BST_INDETERMINATE);
					int textLen = Button_GetTextLength(hProcessCheckboxes[stat]);
					TCHAR *newButtonText = new TCHAR[textLen + 10];
					int copied = Button_GetText(hProcessCheckboxes[stat], newButtonText, textLen+1);
					if (copied > textLen) copied = textLen;
					_tcscpy(&newButtonText[copied], TEXT(" (failed)"));
					Button_SetText(hProcessCheckboxes[stat], newButtonText);
					delete[] newButtonText;
				}
				break;
		}
	}
}

DialogControllerTitled::DialogControllerTitled(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData, bool appendSetup)
	: DialogControllerBase(hDialog, hParentWindow, dialogsData)
{
	hStaticTitle = GetDlgItem(hDialog, IDC_SETUPTITLE);
	bAppendTitle = appendSetup;
}
DialogControllerTitled::~DialogControllerTitled(){}
void DialogControllerTitled::SetModName(const char *modName)
{
	if (hStaticTitle)
	{
#ifdef _UNICODE
		int modNameLenA = (int)(strlen(modName) & 0x7FFFFFFF);
		int modNameLenW = MultiByteToWideChar(CP_UTF8, 0, modName, modNameLenA, NULL, 0);
		wchar_t *title = new wchar_t[modNameLenW + 7];
		MultiByteToWideChar(CP_UTF8, 0, modName, modNameLenA, title, modNameLenW);
		wcsncpy(&title[modNameLenW], bAppendTitle ? L" Setup" : L"", 7);
#else
		size_t modNameLen = strlen(modName);
		char *title = new char[modNameLen + 7];
		strncpy(title, modName, modNameLen);
		strncpy(&title[modNameLen], bAppendTitle ? " Setup" : "", 7);
#endif
		SetWindowText(hStaticTitle, title);
		delete[] title;
	}
}


DialogController_Introduction::DialogController_Introduction(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerTitled(hDialog, hParentWindow, dialogsData, true)
{
}
DialogController_Introduction::~DialogController_Introduction()
{
}
EInstallDialogs DialogController_Introduction::GetOwnDialogType()
{
	return InstallDialog_Introduction;
}

DialogController_Description::DialogController_Description(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerTitled(hDialog, hParentWindow, dialogsData, false)
{
	hStaticAuthors = GetDlgItem(hDialog, IDC_AUTHORS);
	hStaticDescription = GetDlgItem(hDialog, IDC_DESCRIPTION);
}
DialogController_Description::~DialogController_Description(){}
EInstallDialogs DialogController_Description::GetOwnDialogType() { return InstallDialog_Description; }
void DialogController_Description::SetAuthors(const char *authors)
{
	if (hStaticAuthors)
	{
		uint16_t nullAuthor = 0;
		TCHAR *displayText;
		if (!authors)
		{
			displayText = (TCHAR*)&nullAuthor;
		}
		else
		{
#ifdef _UNICODE
			int authorsLenA = (int)(strlen(authors) & 0x7FFFFFFF);
			int authorsLenW = MultiByteToWideChar(CP_UTF8, 0, authors, authorsLenA, NULL, 0);
			displayText = new wchar_t[authorsLenW + 4];
			wcsncpy(displayText, L"By ", 3);
			MultiByteToWideChar(CP_UTF8, 0, authors, authorsLenA, &displayText[3], authorsLenW);
			displayText[authorsLenW + 3] = 0;
#else
			size_t authorsLen = strlen(authors);
			displayText = new char[authorsLen + 4];
			strncpy(displayText, "By ", 3);
			strncpy(&displayText[3], authors, authorsLen);
			displayText[authorsLen + 3] = 0;
#endif
		}
		SetWindowText(hStaticAuthors, displayText);
		if (displayText != (TCHAR*)&nullAuthor)
			delete[] displayText;
	}
}
void DialogController_Description::SetDescription(const char *description)
{
	if (hStaticDescription)
	{
#ifdef _UNICODE
		int descriptionLenA = (int)(strlen(description) & 0x7FFFFFFF);
		int descriptionLenW = MultiByteToWideChar(CP_UTF8, 0, description, descriptionLenA, NULL, 0);
		wchar_t *displayText = new wchar_t[descriptionLenW + 1];
		MultiByteToWideChar(CP_UTF8, 0, description, descriptionLenA, displayText, descriptionLenW);
		displayText[descriptionLenW] = 0;
		SetWindowText(hStaticDescription, displayText);
		delete[] displayText;
#else
		SetWindowText(hStaticDescription, description);
#endif
	}
}

DialogController_PathSelect::DialogController_PathSelect(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerTitled(hDialog, hParentWindow, dialogsData, true)
{
	hEditPath = GetDlgItem(hDialog, IDC_EDITPATH);
	hTreeModifications = GetDlgItem(hDialog, IDC_TREEMODS);
}
DialogController_PathSelect::~DialogController_PathSelect()
{
}
EInstallDialogs DialogController_PathSelect::GetOwnDialogType()
{
	return InstallDialog_PathSelect;
}

TCHAR *DialogController_PathSelect::GetPath(size_t &pathLen)
{
	if (hEditPath)
	{
		size_t textLen = (size_t)Edit_GetTextLength(hEditPath);
		TCHAR *ret = new TCHAR[textLen + 1];
		Edit_GetText(hEditPath, ret, textLen + 1);
		ret[textLen] = 0;
		pathLen = textLen;
		return ret;
	}
	pathLen = 0;
	return NULL;
}
void DialogController_PathSelect::SetPath(const TCHAR *path)
{
	if (hEditPath)
		Edit_SetText(hEditPath, path);
}

void FillModsTree_AssetsReplacers(HWND hTree, HTREEITEM base, std::vector<AssetsReplacer*>& replacers)
{
	TCHAR sprntTmp[256];

	TVINSERTSTRUCT insert = {};
	insert.hParent = base;
	insert.hInsertAfter = TVI_FIRST;//hInsertAfter?hInsertAfter:TVI_ROOT;
	insert.itemex.hItem = NULL;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.pszText = sprntTmp;
	insert.itemex.cchTextMax = 256;
	insert.itemex.hwnd = NULL;
	insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
	insert.itemex.cChildren = 0;
	for (size_t i = replacers.size(); i > 0; i--)
	{
		AssetsReplacer *pReplacer = (AssetsReplacer*)replacers[i - 1];
		if (!pReplacer)
			continue;
		switch (pReplacer->GetType())
		{
			case AssetsReplacement_AddOrModify:
			case AssetsReplacement_Remove:
			{
				AssetsEntryReplacer *pEntryReplacer = reinterpret_cast<AssetsEntryReplacer*>(pReplacer);
				const TCHAR* opType =
					(pReplacer->GetType() == AssetsReplacement_AddOrModify) ? TEXT("Replace") : (
						(pReplacer->GetType() == AssetsReplacement_Remove) ? TEXT("Remove") : TEXT("<invalid>"));
				_stprintf_s(sprntTmp, TEXT("%s PathId %lld"), opType, (long long int)pEntryReplacer->GetPathID());
				TreeView_InsertItem(hTree, &insert);
			}
			break;
			case AssetsReplacement_Dependencies:
			{
				_stprintf_s(sprntTmp, TEXT("Modify dependencies"));
				TreeView_InsertItem(hTree, &insert);
			}
			break;
		}
	}
}
void DialogController_PathSelect::FillModsTree(InstallerPackageFile *pInstallFile)
{
	if (!hTreeModifications)
		return;
	size_t numBundle = 0, numAssets = 0, numResources = 0;
	for (size_t i = 0; i < pInstallFile->affectedAssets.size(); i++)
	{
		switch (pInstallFile->affectedAssets[i].type)
		{
		case InstallerPackageAssetsType::Assets:
			numAssets++;
			break;
		case InstallerPackageAssetsType::Bundle:
			numBundle++;
			break;
		case InstallerPackageAssetsType::Resources:
			numResources++;
			break;
		}
	}

	TVINSERTSTRUCT insert = {};
	insert.hParent = NULL;
	insert.hInsertAfter = TVI_FIRST;//hInsertAfter?hInsertAfter:TVI_ROOT;
	insert.itemex.hItem = NULL;
	insert.itemex.state = 0;
	insert.itemex.stateMask = 0xFF;
	insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected bundles"));
	insert.itemex.hwnd = NULL;
	insert.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
	insert.itemex.cChildren = numBundle ? 1 : 0;
	HTREEITEM hBundleBaseItem = TreeView_InsertItem(hTreeModifications, &insert);

	insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected assets files"));
	insert.hInsertAfter = TVI_LAST;
	insert.itemex.cChildren = numAssets ? 1 : 0;
	HTREEITEM hAssetsBaseItem = TreeView_InsertItem(hTreeModifications, &insert);

	insert.itemex.pszText = const_cast<TCHAR*>(TEXT("Affected resource files"));
	insert.hInsertAfter = TVI_LAST;
	insert.itemex.cChildren = numResources ? 1 : 0;
	HTREEITEM hResourcesBaseItem = TreeView_InsertItem(hTreeModifications, &insert);
	
	for (size_t _i = pInstallFile->affectedAssets.size(); _i > 0; _i--)
	{
		size_t i = _i - 1;
		InstallerPackageAssetsDesc &desc = pInstallFile->affectedAssets[i];
		size_t pathLen;
		TCHAR *tcPath = _MultiByteToTCHAR(desc.path.c_str(), pathLen);
		insert.itemex.pszText = tcPath;
		insert.hInsertAfter = TVI_FIRST;
		switch (desc.type)
		{
		case InstallerPackageAssetsType::Assets:
			insert.hParent = hAssetsBaseItem;
			break;
		case InstallerPackageAssetsType::Bundle:
			insert.hParent = hBundleBaseItem;
			break;
		case InstallerPackageAssetsType::Resources:
			insert.hParent = hResourcesBaseItem;
			break;
		default: insert.hParent = NULL;
		}
		insert.itemex.cChildren = desc.replacers.size() ? 1 : 0;
		HTREEITEM hCurBaseDesc = TreeView_InsertItem(hTreeModifications, &insert);
		_FreeTCHAR(tcPath);
		switch (desc.type)
		{
		case InstallerPackageAssetsType::Assets:
			{
				std::vector<AssetsReplacer*> replacerVector(desc.replacers.size());
				for (size_t i = 0; i < desc.replacers.size(); ++i)
					replacerVector[i] = reinterpret_cast<AssetsReplacer*>(desc.replacers[i].get());
				FillModsTree_AssetsReplacers(hTreeModifications, hCurBaseDesc, replacerVector);
			}
			break;
		case InstallerPackageAssetsType::Bundle:
			{
				insert.hParent = hCurBaseDesc;
				for (size_t _i = desc.replacers.size(); _i > 0; _i--)
				{
					size_t i = _i - 1;
					TCHAR sprntTmp[1024];
					insert.itemex.pszText = sprntTmp;
					insert.itemex.cChildren = 0;

					BundleReplacer* pReplacer = (BundleReplacer*)desc.replacers[i].get();
					BundleEntryModifierFromAssets* pModifierFromAssets =
						dynamic_cast<BundleEntryModifierFromAssets*>(pReplacer);
					size_t origEntryNameLen; size_t newEntryNameLen;
					TCHAR* tcOrigEntry = _MultiByteToTCHAR(pReplacer->GetOriginalEntryName(), origEntryNameLen);
					TCHAR* tcNewEntry = _MultiByteToTCHAR(pReplacer->GetEntryName(), newEntryNameLen);
					if (pReplacer->GetType() == BundleReplacement_Rename)
					{
						_stprintf_s(sprntTmp, TEXT("Rename %s to %s"), tcOrigEntry, tcNewEntry);
					}
					else if (pReplacer->GetType() == BundleReplacement_AddOrModify)
					{
						if (pReplacer->GetOriginalEntryName() == NULL && pReplacer->GetEntryName() != NULL)
						{
							_stprintf_s(sprntTmp, TEXT("Modify/add %s"), tcNewEntry);
						}
						else if (pReplacer->GetOriginalEntryName() != NULL && pReplacer->GetEntryName() != NULL &&
							strcmp(pReplacer->GetOriginalEntryName(), pReplacer->GetEntryName()))
						{
							_stprintf_s(sprntTmp, TEXT("Modify and rename %s to %s"), tcOrigEntry, tcNewEntry);
						}
						else
						{
							_stprintf_s(sprntTmp, TEXT("Modify %s"), tcOrigEntry);
						}
						if (pModifierFromAssets)
						{
							insert.itemex.cChildren = 1;
						}
					}
					else if (pReplacer->GetType() == BundleReplacement_Remove)
					{
						_stprintf_s(sprntTmp, TEXT("Remove %s"), tcOrigEntry);
					}
					else
					{
						_stprintf_s(sprntTmp, TEXT("<invalid> %s"), tcOrigEntry);
					}
					HTREEITEM curTreeItem = TreeView_InsertItem(hTreeModifications, &insert);
					_FreeTCHAR(tcOrigEntry);
					_FreeTCHAR(tcNewEntry);
					if (pModifierFromAssets)
					{
						size_t assetReplacerCount;
						AssetsReplacer** pAssetReplacers = pModifierFromAssets->GetReplacers(assetReplacerCount);
						std::vector<AssetsReplacer*> replacerVector(pAssetReplacers, &pAssetReplacers[assetReplacerCount]);
						FillModsTree_AssetsReplacers(hTreeModifications, curTreeItem, replacerVector);
					}
				}
			}
			break;
		case InstallerPackageAssetsType::Resources:
			if (desc.replacers.size() == 1)
			{
				BundleReplacer* pReplacer = reinterpret_cast<BundleReplacer*>(desc.replacers[0].get());
				auto* pResourcesReplacer = dynamic_cast<BundleEntryModifierByResources*>(pReplacer);
				if (pResourcesReplacer != nullptr)
				{
					insert.hParent = hCurBaseDesc;
					insert.itemex.pszText = const_cast<TCHAR*>(
						pResourcesReplacer->RequiresEntryReader() ? TEXT("Modify") : TEXT("Add or replace")
					);
					insert.itemex.cChildren = 0;
					TreeView_InsertItem(hTreeModifications, &insert);
				}
			}
			break;
		}
	}
}


DialogController_Progress::DialogController_Progress(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerTitled(hDialog, hParentWindow, dialogsData, true)
{
	hInstallingText = GetDlgItem(hDialog, IDC_SINSTALLING);
	hProgressBar = GetDlgItem(hDialog, IDC_PROGINSTALL);
	hCurFileText = GetDlgItem(hDialog, IDC_SCURFILE);
	hLogEdit = GetDlgItem(hDialog, IDC_EDITSTATUS);
	hBtnNext = GetDlgItem(hDialog, IDC_NEXT);
	hBtnCancel = GetDlgItem(hDialog, IDC_CANCEL);
	cancelled = false;

	if (hProgressBar)
	{
		SendMessage(hProgressBar, PBM_SETRANGE, NULL, MAKELPARAM(0,10000));
		SendMessage(hProgressBar, PBM_SETPOS, NULL, NULL);
	}
}
DialogController_Progress::~DialogController_Progress()
{
}
EInstallDialogs DialogController_Progress::GetOwnDialogType()
{
	return InstallDialog_Progress;
}

bool DialogController_Progress::GetCancelled()
{
	return cancelled;
}
void DialogController_Progress::SetCancelled(bool cancelled)
{
	this->cancelled = cancelled;
}
void DialogController_Progress::SetPaused(bool paused)
{
	if (hProgressBar)
	{
		if (paused)
			SendMessage(hProgressBar, PBM_SETSTATE, PBST_PAUSED, NULL);
		else
			SendMessage(hProgressBar, PBM_SETSTATE, PBST_NORMAL, NULL);
	}
}
void DialogController_Progress::SetProgress(float percent, const char *curFileName)
{
	if (hProgressBar)
	{
		SendMessage(hProgressBar, PBM_SETPOS, (WPARAM)((int)(abs(percent) * 100.0F)), NULL);
		if (percent < 0)
			SendMessage(hProgressBar, PBM_SETSTATE, PBST_ERROR, NULL);
		else
			SendMessage(hProgressBar, PBM_SETSTATE, PBST_NORMAL, NULL);
	}
	if (hCurFileText)
	{
		size_t fileNameLen;
		TCHAR *tcFileName = _MultiByteToTCHAR(curFileName, fileNameLen);
		SetWindowText(hCurFileText, tcFileName);
		_FreeTCHAR(tcFileName);
	}
}
void DialogController_Progress::AddToLog(const wchar_t *logText)
{
	if (hLogEdit)
	{
		size_t logTextLen;
		TCHAR *tcLogText = _WideToTCHAR(logText, logTextLen);
		int editLen = Edit_GetTextLength(hLogEdit);
		int oldSelStart = editLen, oldSelEnd = editLen;
		SendMessage(hLogEdit, EM_GETSEL, (WPARAM)&oldSelStart, (LPARAM)&oldSelEnd);
		Edit_SetSel(hLogEdit, editLen, editLen);
		Edit_ReplaceSel(hLogEdit, logText);
		if ((oldSelEnd != editLen) || (oldSelStart != oldSelEnd))
			Edit_SetSel(hLogEdit, oldSelStart, oldSelEnd);
		else
			Edit_SetSel(hLogEdit, editLen + logTextLen, editLen + logTextLen);
		_FreeTCHAR(tcLogText);
	}
}
void DialogController_Progress::AddToLog(const char *logText)
{
	if (hLogEdit)
	{
		size_t logTextLen;
		TCHAR *tcLogText = _MultiByteToTCHAR(logText, logTextLen);
		int editLen = Edit_GetTextLength(hLogEdit);
		int oldSelStart = editLen, oldSelEnd = editLen;
		SendMessage(hLogEdit, EM_GETSEL, (WPARAM)&oldSelStart, (LPARAM)&oldSelEnd);
		Edit_SetSel(hLogEdit, editLen, editLen);
		Edit_ReplaceSel(hLogEdit, tcLogText);
		if ((oldSelEnd != editLen) || (oldSelStart != oldSelEnd))
			Edit_SetSel(hLogEdit, oldSelStart, oldSelEnd);
		else
			Edit_SetSel(hLogEdit, editLen + logTextLen, editLen + logTextLen);
		_FreeTCHAR(tcLogText);
	}
}
void DialogController_Progress::EnableContinue()
{
	if (hBtnNext)
	{
		Button_Enable(hBtnNext, TRUE);
		if (hInstallingText)
		{
			Edit_SetText(hInstallingText, TEXT(""));
		}
	}
}
void DialogController_Progress::DisableCancel()
{
	if (hBtnCancel)
	{
		Button_Enable(hBtnCancel, FALSE);
	}
}

DialogController_Complete::DialogController_Complete(HWND hDialog, HWND hParentWindow, InstallDialogsData *dialogsData)
	: DialogControllerTitled(hDialog, hParentWindow, dialogsData, true)
{
	hStaticCompleteText = GetDlgItem(hDialog, IDC_COMPLETETEXT);
	hStaticAuthors = GetDlgItem(hDialog, IDC_MODAUTHORS);
}
DialogController_Complete::~DialogController_Complete()
{
}
EInstallDialogs DialogController_Complete::GetOwnDialogType()
{
	return InstallDialog_Complete;
}
void DialogController_Complete::SetCompleteText(const TCHAR *text)
{
	if (hStaticCompleteText)
	{
		SetWindowText(hStaticCompleteText, text);
	}
}
void DialogController_Complete::SetAuthors(const char *authors)
{
	if (hStaticAuthors)
	{
		uint16_t nullAuthor = 0;
		TCHAR *displayText;
		if (!authors)
		{
			displayText = (TCHAR*)&nullAuthor;
		}
		else
		{
#ifdef _UNICODE
			int authorsLenA = (int)(strlen(authors) & 0x7FFFFFFF);
			int authorsLenW = MultiByteToWideChar(CP_UTF8, 0, authors, authorsLenA, NULL, 0);
			displayText = new wchar_t[authorsLenW + 4];
			wcsncpy(displayText, L"By ", 3);
			MultiByteToWideChar(CP_UTF8, 0, authors, authorsLenA, &displayText[3], authorsLenW);
			displayText[authorsLenW + 3] = 0;
#else
			size_t authorsLen = strlen(authors);
			displayText = new char[authorsLen + 4];
			strncpy(displayText, "By ", 3);
			strncpy(&displayText[3], authors, authorsLen);
			displayText[authorsLen + 3] = 0;
#endif
		}
		SetWindowText(hStaticAuthors, displayText);
		if (displayText != (TCHAR*)&nullAuthor)
			delete[] displayText;
	}
}