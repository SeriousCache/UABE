#include "stdafx.h"
#include "resource.h"
#include "SelectClassDbDialog.h"
#include "Win32AppContext.h"
#include "FileDialog.h"
#include "../libStringConverter/convert.h"
#include <WindowsX.h>

typedef std::unique_ptr<ClassDatabaseFile, void(*)(ClassDatabaseFile*)> ClassDatabaseFile_ptr;
static void ClassDatabaseDeleter_Dummy(ClassDatabaseFile*) {}
static void ClassDatabaseDeleter_delete(ClassDatabaseFile *pFile)
{
	delete pFile;
}

SelectClassDbDialog::SelectClassDbDialog(HINSTANCE hInstance, HWND hParentWnd, ClassDatabasePackage &classPackage)
	: hInstance(hInstance), hParentWnd(hParentWnd), hDialog(NULL),
		dialogReason_DbNotFound(true),
		pClassDatabaseResult(nullptr, ClassDatabaseDeleter_Dummy), rememberForVersion(true), rememberForAll(false), 
		doneParentMessage(0), classPackage(classPackage)
{}

INT_PTR CALLBACK SelectClassDbDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret = (INT_PTR)FALSE;
	SelectClassDbDialog *pThis = (SelectClassDbDialog*)(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	switch (message)
	{
		case WM_INITDIALOG:
			{
				SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
				pThis = (SelectClassDbDialog*)lParam;

				pThis->hDialog = hDlg;
				pThis->pClassDatabaseResult.reset();
				
				const char *engineVersion = pThis->version.c_str();
				std::string descText;
				if (pThis->dialogReason_DbNotFound)
					descText = "No type database matches the player version ";
				else
					descText = "The selected file has player version ";
				descText += engineVersion;
				descText += ".";
				size_t _len;
				TCHAR *descTextT = _MultiByteToTCHAR(descText.c_str(), _len);
				Static_SetText(GetDlgItem(hDlg, IDC_DESCLABEL), descTextT);
				_FreeTCHAR(descTextT);

				TCHAR *fileNameT = _MultiByteToTCHAR(pThis->fileName.c_str(), _len);
				Static_SetText(GetDlgItem(hDlg, IDC_FILELABEL), fileNameT);
				_FreeTCHAR(fileNameT);

				HWND hDatabaseList = GetDlgItem(hDlg, IDC_DBLIST);
				for (DWORD i = 0; i < pThis->classPackage.header.fileCount; i++)
				{
					char *databaseName = pThis->classPackage.header.files[i].name; size_t _len;
					TCHAR *databaseNameT = _MultiByteToTCHAR(databaseName, _len);
					ListBox_AddString(hDatabaseList, databaseNameT);
					_FreeTCHAR(databaseNameT);
				}

				if (pThis->rememberForAll)
					pThis->rememberForVersion = true;
				Button_SetCheck(GetDlgItem(hDlg, IDC_CKREMEMBERVERSION), pThis->rememberForVersion ? BST_CHECKED : BST_UNCHECKED);
				EnableWindow(GetDlgItem(hDlg, IDC_CKREMEMBERVERSION), pThis->rememberForAll ? FALSE : TRUE);
				Button_SetCheck(GetDlgItem(hDlg, IDC_CKREMEMBERALL), pThis->rememberForAll ? BST_CHECKED : BST_UNCHECKED);
			}
			return (INT_PTR)TRUE;
		case WM_APP+0:
			{
				if (!pThis)
					break;
				SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
				if (pThis->doneParentMessage != 0 && pThis->hParentWnd != NULL)
					PostMessageW(pThis->hParentWnd, pThis->doneParentMessage, reinterpret_cast<WPARAM>(pThis), 0);
			}
			EndDialog(hDlg, 1);
			return (INT_PTR)TRUE;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_CKREMEMBERALL:
				{
					bool isChecked = (Button_GetCheck((HWND)lParam) == BST_CHECKED);
					if (isChecked)
						Button_SetCheck(GetDlgItem(hDlg, IDC_CKREMEMBERVERSION), BST_CHECKED);
					EnableWindow(GetDlgItem(hDlg, IDC_CKREMEMBERVERSION), isChecked ? FALSE : TRUE);
				}
				break;
				case IDC_BTNLOAD:
				{
					HWND hLoadPathEdit = GetDlgItem(hDlg, IDC_ELOADPATH);
					std::unique_ptr<TCHAR[]> loadPathBuf;
					int loadPathLen = Edit_GetTextLength(hLoadPathEdit);
					if (loadPathLen > 0 && loadPathLen < INT_MAX)
					{
						std::unique_ptr<TCHAR[]> loadPathBuf(new TCHAR[loadPathLen + 1]);
						int actualLen = Edit_GetText(hLoadPathEdit, loadPathBuf.get(), loadPathLen + 1);
						loadPathBuf[loadPathLen] = 0;
						loadPathLen = actualLen;
					}

					wchar_t *filePath;
					HRESULT hr = ShowFileOpenDialog(hDlg, &filePath, L"*.dat|.dat files", nullptr, loadPathBuf.get(),
						L"Select a class database file",
						UABE_FILEDIALOG_CLDB_GUID);
					if (SUCCEEDED(hr))
					{
						SetWindowTextW(hLoadPathEdit, filePath);
						FreeCOMFilePathBuf(&filePath);
					}
				}
				break;
				case IDOK:
				{
					if (!pThis)
						break;
					HWND hLoadPathEdit = GetDlgItem(hDlg, IDC_ELOADPATH);
					std::unique_ptr<TCHAR[]> loadPathBuf;
					int loadPathLen = Edit_GetTextLength(hLoadPathEdit);
					if (loadPathLen > 0 && loadPathLen < INT_MAX)
					{
						loadPathBuf.reset(new TCHAR[loadPathLen + 1]);
						int actualLen = Edit_GetText(hLoadPathEdit, loadPathBuf.get(), loadPathLen + 1);
						loadPathBuf[loadPathLen] = 0;
						loadPathLen = actualLen;
					}
					if (loadPathLen > 0)
					{
						IAssetsReader_ptr pDatabaseReader(Create_AssetsReaderFromFile(loadPathBuf.get(), true, RWOpenFlags_Immediately), Free_AssetsReader);
						if (pDatabaseReader != nullptr)
						{
							pThis->pClassDatabaseResult = ClassDatabaseFile_ptr(new ClassDatabaseFile(), ClassDatabaseDeleter_delete);
							if (!pThis->pClassDatabaseResult->Read(pDatabaseReader.get()))
							{
								pThis->pClassDatabaseResult.reset();
								MessageBoxA(hDlg, "Unable to read or deserialize the given class database file!", "UABE", 16);
								ret = (INT_PTR)TRUE;
								break;
							}
						}
						else
						{
							MessageBoxA(hDlg, "Unable to open the given class database file!", "UABE", 16);
							ret = (INT_PTR)TRUE;
							break;
						}
					}
					else
					{
						HWND hVersionList = GetDlgItem(hDlg, IDC_DBLIST);
						int selection = ListBox_GetCurSel(hVersionList);
						if (selection >= 0 && (DWORD)selection < pThis->classPackage.header.fileCount)
						{
							ClassDatabaseFile *pSelectedDatabase = pThis->classPackage.files[selection];
							pThis->pClassDatabaseResult = ClassDatabaseFile_ptr(pSelectedDatabase, ClassDatabaseDeleter_Dummy);
						}
					}
				}
				case IDCANCEL:
				{
					if (!pThis)
						break;
					pThis->rememberForAll = (Button_GetCheck(GetDlgItem(hDlg, IDC_CKREMEMBERALL)) == BST_CHECKED);
					pThis->rememberForVersion = pThis->rememberForAll || (Button_GetCheck(GetDlgItem(hDlg, IDC_CKREMEMBERVERSION)) == BST_CHECKED);
					
					SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);

					if (pThis->doneParentMessage != 0 && pThis->hParentWnd != NULL)
						PostMessageW(pThis->hParentWnd, pThis->doneParentMessage, reinterpret_cast<WPARAM>(pThis), 0);
				}
				EndDialog(hDlg, 1);
				return (INT_PTR)TRUE;
			}
			break;
	}
	return ret;
}

bool SelectClassDbDialog::ShowModal()
{
	this->doneParentMessage = 0;
	if (DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_SELECTTYPEDB), hParentWnd, DlgProc, (LPARAM)this) != 1)
		return false;
	return true;
}

HWND SelectClassDbDialog::ShowModeless(UINT doneParentMessage)
{
	this->doneParentMessage = doneParentMessage;
	this->hDialog = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_SELECTTYPEDB), hParentWnd, DlgProc, (LPARAM)this);
	return hDialog;
}

void SelectClassDbDialog::ForceCancel(bool rememberForVersion, bool rememberForAll)
{
	this->rememberForVersion = rememberForVersion;
	this->rememberForAll = rememberForAll;
	this->pClassDatabaseResult.reset();
	PostMessage(this->hDialog, WM_APP+0, 0, 0);
}
