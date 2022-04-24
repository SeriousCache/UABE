#include "stdafx.h"
#include "resource.h"
#include "TypeDbPackageEditor.h"

#include "Win32AppContext.h"
#include "FileDialog.h"

#include "../AssetsTools/ClassDatabaseFile.h"
#include "../AssetsTools/AssetsFileReader.h"

#include <WindowsX.h>

INT_PTR CALLBACK TypeDbPackageEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

struct TypePkEditDialog
{
	HINSTANCE hInst = NULL;
	int selectedDbIndex = -1;
	WCHAR *filePath = nullptr;
	ClassDatabasePackage classPackage;
} typePkEditDialog;
void OpenTypeDbPackageEditor(HINSTANCE hInstance, HWND hParent)
{
	typePkEditDialog.hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_EDITTYPEPAK), hParent, TypeDbPackageEditor);
}

static char *GetEditTextA(HWND hEdit)
{
	char *cNameBuf;
	#ifdef _UNICODE
		int wcTextLen = Edit_GetTextLength(hEdit);
		WCHAR *wcNameBuf = (WCHAR*)malloc((wcTextLen+1) * sizeof(WCHAR));
		__checkoutofmemory(wcNameBuf==NULL);
		Edit_GetText(hEdit, wcNameBuf, wcTextLen+1);
		wcNameBuf[wcTextLen] = 0;

		int cTextLen = WideCharToMultiByte(CP_UTF8, 0, wcNameBuf, wcTextLen, NULL, 0, NULL, NULL);
		cNameBuf = (char*)malloc((cTextLen+1) * sizeof(char));
		__checkoutofmemory(cNameBuf==NULL);
		WideCharToMultiByte(CP_UTF8, 0, wcNameBuf, wcTextLen, cNameBuf, cTextLen, NULL, NULL);
		cNameBuf[cTextLen] = 0;
		free(wcNameBuf);
	#else
		int cTextLen = Edit_GetTextLength(hEdit);
		cNameBuf = (char*)malloc(cTextLen+1);
		__checkoutofmemory(cNameBuf==NULL);
		Edit_GetText(hEdit, cNameBuf, cTextLen+1);
		cNameBuf[cTextLen] = 0;
	#endif
	return cNameBuf;
}
static void SetEditTextA(HWND hEdit, char *text)
{
	#ifdef _UNICODE
		size_t textLen = strlen(text);
		int wcharCount = MultiByteToWideChar(CP_UTF8, 0, text, (int)textLen, NULL, 0);
		WCHAR *wcTextBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
		__checkoutofmemory(wcTextBuf==NULL);
		MultiByteToWideChar(CP_UTF8, 0, text, (int)textLen, wcTextBuf, wcharCount);
		wcTextBuf[wcharCount] = 0;
		Edit_SetText(hEdit, wcTextBuf);
		free(wcTextBuf);
	#else
		Edit_SetText(hNameEdit, text);
	#endif
}

INT_PTR CALLBACK TypeDbPackageEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	int moveDirection = 0; //only used for the Up/Down buttons
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hOptimizeFastCB = GetDlgItem(hDlg, IDC_CKOPTIMIZEFAST);
			Button_SetCheck(hOptimizeFastCB, true);
			HWND hOptimizeSlowCB = GetDlgItem(hDlg, IDC_CKOPTIMIZESLOW);
			Button_SetCheck(hOptimizeSlowCB, false);
			HWND hCompressLZ4CB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZ4);
			Button_SetCheck(hCompressLZ4CB, false);
			HWND hCompressLZMACB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZMA);
			Button_SetCheck(hCompressLZMACB, true);

			WCHAR *filePathBuf;
			HRESULT hr = ShowFileOpenDialog(hDlg, &filePathBuf, L"*.tpk|Type database package:",
				nullptr, nullptr, nullptr,
				UABE_FILEDIALOG_CLDB_GUID);
			__checkoutofmemory(hr==E_OUTOFMEMORY);
			if (SUCCEEDED(hr))
			{
				IAssetsReader *pDbReader = Create_AssetsReaderFromFile(filePathBuf, true, RWOpenFlags_Immediately);
				//FILE *pDbFile = NULL;
				//_wfopen_s(&pDbFile, filePathBuf, L"rb");
				if (pDbReader != NULL)
				{
					if (!typePkEditDialog.classPackage.Read(pDbReader))
					{
						//treat it like an empty package
						memset(&typePkEditDialog.classPackage.header, 0, sizeof(ClassDatabasePackageHeader));
						typePkEditDialog.classPackage.files = NULL;
						typePkEditDialog.classPackage.stringTable = NULL;
					}
					Free_AssetsReader(pDbReader);
					//fclose(pDbFile);
					typePkEditDialog.filePath = filePathBuf;
					
					HWND hDblist = GetDlgItem(hDlg, IDC_DBLIST);
					for (DWORD i = 0; i < (DWORD)typePkEditDialog.classPackage.header.files.size(); i++)
					{
						ClassDatabaseFileRef *pFileRef = &typePkEditDialog.classPackage.header.files[i];
						const char *dbName = pFileRef->name;
						if (dbName != NULL)
						{
#ifdef _UNICODE
							size_t dbNameLen = strlen(dbName);
							int wcharCount = MultiByteToWideChar(CP_UTF8, 0, dbName, (int)dbNameLen, NULL, 0);
							WCHAR *wcNameBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
							__checkoutofmemory(wcNameBuf==NULL);
							MultiByteToWideChar(CP_UTF8, 0, dbName, (int)dbNameLen, wcNameBuf, wcharCount);
							wcNameBuf[wcharCount] = 0;
							ListBox_AddString(hDblist, wcNameBuf);
							free(wcNameBuf);
#else
							ListBox_AddString(hDblist, typeName);
#endif
						}
						else
							ListBox_AddString(hDblist, TEXT(""));
					}
					typePkEditDialog.selectedDbIndex = -1;
					ListBox_SetCurSel(hDblist, 0);
					goto DoUpdateDbList;
				}
				else
				{
					FreeCOMFilePathBuf(&filePathBuf);
					MessageBox(hDlg, TEXT("Unable to open the file!"), TEXT("ERROR"), 16);
					EndDialog(hDlg, LOWORD(wParam));
				}
			}
			else
				EndDialog(hDlg, LOWORD(wParam));
		}
		return (INT_PTR)TRUE;
		
	case WM_CLOSE:
	case WM_DESTROY:
		EndDialog(hDlg, LOWORD(wParam));
		goto Free_TypePkEditorDialog;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDC_CKCOMPRESSLZ4:
				{
					HWND hCompressLZ4CB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZ4);
					HWND hCompressLZMACB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZMA);
					if (Button_GetCheck(hCompressLZ4CB))
						Button_SetCheck(hCompressLZMACB, 0);
				}
				break;
			case IDC_CKCOMPRESSLZMA:
				{
					HWND hCompressLZ4CB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZ4);
					HWND hCompressLZMACB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZMA);
					if (Button_GetCheck(hCompressLZMACB))
						Button_SetCheck(hCompressLZ4CB, 0);
				}
				break;
			case IDC_CKOPTIMIZEFAST:
				{
					HWND hOptimizeFastCB = GetDlgItem(hDlg, IDC_CKOPTIMIZEFAST);
					HWND hOptimizeSlowCB = GetDlgItem(hDlg, IDC_CKOPTIMIZESLOW);
					if (Button_GetCheck(hOptimizeFastCB))
						Button_SetCheck(hOptimizeSlowCB, 0);
				}
				break;
			case IDC_CKOPTIMIZESLOW:
				{
					HWND hOptimizeFastCB = GetDlgItem(hDlg, IDC_CKOPTIMIZEFAST);
					HWND hOptimizeSlowCB = GetDlgItem(hDlg, IDC_CKOPTIMIZESLOW);
					if (Button_GetCheck(hOptimizeSlowCB))
						Button_SetCheck(hOptimizeFastCB, 0);
				}
				break;
			case IDOK:
				{
					HWND hOptimizeFastCB = GetDlgItem(hDlg, IDC_CKOPTIMIZEFAST);
					HWND hOptimizeSlowCB = GetDlgItem(hDlg, IDC_CKOPTIMIZESLOW);
					bool optimizePlacebo = Button_GetCheck(hOptimizeSlowCB)?true:false;
					bool optimizeFast = (Button_GetCheck(hOptimizeFastCB)?true:false);
					int optimize = optimizePlacebo ? 2 : (optimizeFast ? 1 : 0);
					HWND hCompressLZ4CB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZ4);
					HWND hCompressLZMACB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZMA);
					DWORD compress = (Button_GetCheck(hCompressLZ4CB) ? 1 : (Button_GetCheck(hCompressLZMACB) ? 2 : 0)) | 0x80;
					IAssetsWriter *pDbWriter = Create_AssetsWriterToFile(typePkEditDialog.filePath, true, true, RWOpenFlags_Immediately);
					//FILE *pDbFile = NULL;
					//_wfopen_s(&pDbFile, typePkEditDialog.filePath, L"wb");
					if (pDbWriter != NULL)
					{
						typePkEditDialog.classPackage.Write(pDbWriter, 0, optimize, compress);
						Free_AssetsWriter(pDbWriter);
						//fclose(pDbFile);
					}
					else
					{
						MessageBox(hDlg, TEXT("Unable to open the file for writing!"), TEXT("ERROR"), 16);
						break;
					}
				}
			case IDCANCEL:
				EndDialog(hDlg, LOWORD(wParam));
				goto Free_TypePkEditorDialog;
			case IDC_BTNDBEXPORT:
				{
					if (typePkEditDialog.selectedDbIndex >= 0 && 
						typePkEditDialog.selectedDbIndex < (int)typePkEditDialog.classPackage.header.fileCount)
					{
						
						WCHAR *filePathBuf;
						HRESULT hr = ShowFileSaveDialog(hDlg, &filePathBuf, L"*.dat|Type database:",
							nullptr, nullptr, nullptr,
							UABE_FILEDIALOG_CLDB_GUID);
						__checkoutofmemory(hr==E_OUTOFMEMORY);
						if (SUCCEEDED(hr))
						{
							IAssetsWriter *pDbWriter = Create_AssetsWriterToFile(filePathBuf, true, true, RWOpenFlags_Immediately);
							//FILE *pDbFile = NULL;
							//_wfopen_s(&pDbFile, filePathBuf, L"wb");
							FreeCOMFilePathBuf(&filePathBuf);
							if (pDbWriter != NULL)
							{
								typePkEditDialog.classPackage.files[typePkEditDialog.selectedDbIndex]->Write(
									pDbWriter, 0
								);
								Free_AssetsWriter(pDbWriter);
								//fclose(pDbFile);
							}
							else
								MessageBox(hDlg, TEXT("Unable to open the file for writing!"), TEXT("ERROR"), 16);
						}
						//typeEditDialog.pType = &typeDbEditDialog.classDatabase.classes[typeDbEditDialog.selectedTypeIndex];
						//DialogBox(typeDbEditDialog.hInst, MAKEINTRESOURCE(IDD_EDITTYPE), hDlg, TypeEditor);
					}
				}
				break;
			case IDC_BTNDBIMPORT:
				{
					WCHAR *filePathBuf;
					HRESULT hr = ShowFileOpenDialog(hDlg, &filePathBuf, L"*.dat|Type database:",
						nullptr, nullptr, nullptr,
						UABE_FILEDIALOG_CLDB_GUID);
					__checkoutofmemory(hr==E_OUTOFMEMORY);
					if (SUCCEEDED(hr))
					{
						IAssetsReader *pDbReader = Create_AssetsReaderFromFile(filePathBuf, true, RWOpenFlags_Immediately);
						//FILE *pDbFile = NULL;
						//_wfopen_s(&pDbFile, filePathBuf, L"rb");
						FreeCOMFilePathBuf(&filePathBuf);
						if (pDbReader != NULL)
						{
							if (typePkEditDialog.classPackage.ImportFile(pDbReader))
							{
								HWND hDbList = GetDlgItem(hDlg, IDC_DBLIST);
								ListBox_AddString(hDbList, TEXT(""));
								ListBox_SetCurSel(hDbList, typePkEditDialog.classPackage.header.fileCount - 1);
							}
							else
								MessageBox(hDlg, TEXT("Unable to import the type database!"), TEXT("ERROR"), 16);
							Free_AssetsReader(pDbReader);
							//fclose(pDbFile);
						}
						else
							MessageBox(hDlg, TEXT("Unable to open the file for reading!"), TEXT("ERROR"), 16);
					}
					goto DoUpdateDbList;
				}
				break;
			case IDC_BTNREMOVE:
				{
					if (typePkEditDialog.selectedDbIndex >= 0 && 
						typePkEditDialog.selectedDbIndex < (int)typePkEditDialog.classPackage.header.fileCount)
					{
						int selection = typePkEditDialog.selectedDbIndex;
						HWND hDbList = GetDlgItem(hDlg, IDC_DBLIST);
						if (typePkEditDialog.classPackage.RemoveFile((DWORD)selection))
						{
							ListBox_DeleteString(hDbList, selection);
							typePkEditDialog.selectedDbIndex = -1;
						}
						if (selection < (int)typePkEditDialog.classPackage.header.fileCount)
							ListBox_SetCurSel(hDbList, selection);
						else if (selection > 0)
							ListBox_SetCurSel(hDbList, selection-1);
						goto DoUpdateDbList;
					}
				}
				break;
			case IDC_EDITNAME:
				{
					if (typePkEditDialog.selectedDbIndex >= 0 && 
						typePkEditDialog.selectedDbIndex < (int)typePkEditDialog.classPackage.header.fileCount)
					{
						ClassDatabaseFileRef *pSelectedRef = &typePkEditDialog.classPackage.header.files[typePkEditDialog.selectedDbIndex];
						HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
						HWND hDblist = GetDlgItem(hDlg, IDC_DBLIST);
						char *cNameBuf;
						#ifdef _UNICODE
							int wcTextLen = Edit_GetTextLength(hNameEdit);
							WCHAR *wcNameBuf = (WCHAR*)malloc((wcTextLen+1) * sizeof(WCHAR));
							__checkoutofmemory(wcNameBuf==NULL);
							Edit_GetText(hNameEdit, wcNameBuf, wcTextLen+1);
							wcNameBuf[wcTextLen] = 0;

							ListBox_DeleteString(hDblist, typePkEditDialog.selectedDbIndex);
							ListBox_InsertString(hDblist, typePkEditDialog.selectedDbIndex, wcNameBuf);

							int cTextLen = WideCharToMultiByte(CP_UTF8, 0, wcNameBuf, wcTextLen, NULL, 0, NULL, NULL);
							cNameBuf = (char*)malloc((cTextLen+1) * sizeof(char));
							__checkoutofmemory(cNameBuf==NULL);
							WideCharToMultiByte(CP_UTF8, 0, wcNameBuf, wcTextLen, cNameBuf, cTextLen, NULL, NULL);
							cNameBuf[cTextLen] = 0;
							free(wcNameBuf);
						#else
							int cTextLen = Edit_GetTextLength(hNameEdit);
							cNameBuf = (char*)malloc(cTextLen+1);
							__checkoutofmemory(cNameBuf==NULL);
							Edit_GetText(hNameEdit, cNameBuf, cTextLen+1);
							cNameBuf[cTextLen] = 0;

							ListBox_DeleteString(hAssetlist, typeDbEditDialog.selectedTypeIndex);
							ListBox_InsertString(hAssetlist, typeDbEditDialog.selectedTypeIndex, cNameBuf);
						#endif
						strncpy(pSelectedRef->name, cNameBuf, 15);
						pSelectedRef->name[15] = 0;
						ListBox_SetCurSel(hDblist, typePkEditDialog.selectedDbIndex);
						free(cNameBuf);
					}
				}
				break;
			case IDC_DBLIST:
			DoUpdateDbList:
				{
					HWND hDbList = GetDlgItem(hDlg, IDC_DBLIST);
					unsigned int selection = (unsigned int)ListBox_GetCurSel(hDbList);
					if ((selection != typePkEditDialog.selectedDbIndex) && 
						selection < typePkEditDialog.classPackage.header.fileCount)
					{
						typePkEditDialog.selectedDbIndex = (int)selection;
						HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
						ClassDatabaseFileRef *pSelectedRef = &typePkEditDialog.classPackage.header.files[typePkEditDialog.selectedDbIndex];
						const char *dbName = pSelectedRef->name;
						{
							#ifdef _UNICODE
								size_t dbNameLen = strlen(pSelectedRef->name);
								int wcharCount = MultiByteToWideChar(CP_UTF8, 0, dbName, (int)dbNameLen, NULL, 0);
								WCHAR *wcNameBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
								__checkoutofmemory(wcNameBuf==NULL);
								MultiByteToWideChar(CP_UTF8, 0, dbName, (int)dbNameLen, wcNameBuf, wcharCount);
								wcNameBuf[wcharCount] = 0;
								Edit_SetText(hNameEdit, wcNameBuf);
								free(wcNameBuf);
							#else
								Edit_SetText(hNameEdit, dbName);
							#endif
						}
					}
				}
				break;
			case IDC_BTNMOVEUP:
				{
					moveDirection = -1;
					GOTO_HANDLEMOVEBTN:
					if ((typePkEditDialog.selectedDbIndex + moveDirection) >= 0 && 
						(typePkEditDialog.selectedDbIndex + moveDirection) < (int)typePkEditDialog.classPackage.header.fileCount)
					{
						ClassDatabaseFileRef *pSelectedRef = &typePkEditDialog.classPackage.header.files[typePkEditDialog.selectedDbIndex];
						HWND hDblist = GetDlgItem(hDlg, IDC_DBLIST);

						int tTextLen = ListBox_GetTextLen(hDblist, typePkEditDialog.selectedDbIndex);
						TCHAR *tNameBuf = (WCHAR*)malloc((tTextLen+1) * sizeof(TCHAR));
						__checkoutofmemory(tNameBuf==NULL);
						ListBox_GetText(hDblist, typePkEditDialog.selectedDbIndex, tNameBuf);
						tNameBuf[tTextLen] = 0;

						ListBox_DeleteString(hDblist, typePkEditDialog.selectedDbIndex);
						ListBox_InsertString(hDblist, typePkEditDialog.selectedDbIndex + moveDirection, tNameBuf);
						ClassDatabaseFileRef tmpRef = typePkEditDialog.classPackage.header.files[(DWORD)typePkEditDialog.selectedDbIndex];
						typePkEditDialog.classPackage.header.files.erase(
							typePkEditDialog.classPackage.header.files.begin() + (DWORD)typePkEditDialog.selectedDbIndex);
						typePkEditDialog.classPackage.header.files.insert(
							typePkEditDialog.classPackage.header.files.begin() + (DWORD)(typePkEditDialog.selectedDbIndex + moveDirection), tmpRef);

						free(tNameBuf);

						ListBox_SetCurSel(hDblist, typePkEditDialog.selectedDbIndex + moveDirection);
						typePkEditDialog.selectedDbIndex = typePkEditDialog.selectedDbIndex + moveDirection;
					}
				}
				break;
			case IDC_BTNMOVEDOWN:
				{
					moveDirection = 1;
					goto GOTO_HANDLEMOVEBTN;
				}
				break;
		}
		break;
	}
	return (INT_PTR)FALSE;
Free_TypePkEditorDialog:
	typePkEditDialog.classPackage.Clear();
	FreeCOMFilePathBuf(&typePkEditDialog.filePath);
	return (INT_PTR)TRUE;
}