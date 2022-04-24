#include "stdafx.h"
#include "resource.h"
#include "TypeDatabaseEditor.h"
#include "Win32AppContext.h"

#include "FileDialog.h"

#include "../AssetsTools/ClassDatabaseFile.h"
#include "../AssetsTools/AssetsFileReader.h"

#include <WindowsX.h>

INT_PTR CALLBACK AddTypeField(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TypeDbEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TypeDbVersionEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TypeEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

struct TypeDbEditDialog
{
	HINSTANCE hInst = NULL;
	int selectedTypeIndex = -1;
	char **originalUnityVersionList = nullptr;
	WCHAR *filePath = nullptr;
	ClassDatabaseFile classDatabase;
} typeDbEditDialog;
struct TypeDbVersionEditDialog
{
	bool isCustomAlloc[256] = {};
} typeDbVersionEditDialog;
struct TypeEditDialog
{
	ClassDatabaseType *pType = nullptr;
	ClassDatabaseType workCopy;
	int selectedFieldIndex = -1;
	std::vector<HTREEITEM> fields;
} typeEditDialog;
struct AddFieldDialog
{
	ClassDatabaseTypeField typeField;
	bool success = false;
} addFieldDialog;
void OpenTypeDatabaseEditor(HINSTANCE hInstance, HWND hParent)
{
	typeDbEditDialog.hInst = hInstance;
	typeDbEditDialog.originalUnityVersionList = NULL;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_EDITTYPEDB), hParent, TypeDbEditor);
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
static void SetEditTextA(HWND hEdit, const char *text)
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

INT_PTR CALLBACK TypeDbEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			memset(typeDbVersionEditDialog.isCustomAlloc, 0, 256);
			HWND hOptimizeFastCB = GetDlgItem(hDlg, IDC_CKOPTIMIZEFAST);
			Button_SetCheck(hOptimizeFastCB, true);
			HWND hOptimizeSlowCB = GetDlgItem(hDlg, IDC_CKOPTIMIZESLOW);
			Button_SetCheck(hOptimizeSlowCB, false);
			HWND hCompressLZ4CB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZ4);
			Button_SetCheck(hCompressLZ4CB, false);
			HWND hCompressLZMACB = GetDlgItem(hDlg, IDC_CKCOMPRESSLZMA);
			Button_SetCheck(hCompressLZMACB, true);

			WCHAR *filePathBuf;
			HRESULT hr = ShowFileOpenDialog(hDlg, &filePathBuf, L"*.dat|Type database:",
				nullptr, nullptr, nullptr,
				UABE_FILEDIALOG_CLDB_GUID);
			__checkoutofmemory(hr==E_OUTOFMEMORY);
			if (SUCCEEDED(hr))
			{
				IAssetsReader *pDbReader = Create_AssetsReaderFromFile(filePathBuf, true, RWOpenFlags_Immediately);

				if (pDbReader != NULL)
				{
					typeDbEditDialog.classDatabase.Read(pDbReader);
					Free_AssetsReader(pDbReader);
					//fclose(pDbFile);
					typeDbEditDialog.filePath = filePathBuf;
					
					HWND hAssetlist = GetDlgItem(hDlg, IDC_TYPELIST);
					for (DWORD i = 0; i < typeDbEditDialog.classDatabase.classes.size(); i++)
					{
						ClassDatabaseType *pType = &typeDbEditDialog.classDatabase.classes[i];
						const char *typeName = pType->name.GetString(&typeDbEditDialog.classDatabase);
						if (typeName != NULL)
						{
#ifdef _UNICODE
							size_t typeNameLen = strlen(typeName);
							int wcharCount = MultiByteToWideChar(CP_UTF8, 0, typeName, (int)typeNameLen, NULL, 0);
							WCHAR *wcNameBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
							__checkoutofmemory(wcNameBuf==NULL);
							MultiByteToWideChar(CP_UTF8, 0, typeName, (int)typeNameLen, wcNameBuf, wcharCount);
							wcNameBuf[wcharCount] = 0;
							ListBox_AddString(hAssetlist, wcNameBuf);
							free(wcNameBuf);
#else
							ListBox_AddString(hAssetlist, typeName);
#endif
						}
						else
							ListBox_AddString(hAssetlist, TEXT(""));
					}
					typeDbEditDialog.selectedTypeIndex = -1;
					ListBox_SetCurSel(hAssetlist, 0);
					goto DoUpdateTypeList;
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
		goto Free_TypeDbEditorDialog;
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
					DWORD compress = Button_GetCheck(hCompressLZ4CB) ? 1 : (Button_GetCheck(hCompressLZMACB) ? 2 : 0);
					IAssetsWriter *pDbWriter = Create_AssetsWriterToFile(typeDbEditDialog.filePath, true, true, RWOpenFlags_Immediately);
					//FILE *pDbFile = NULL;
					//_wfopen_s(&pDbFile, typeDbEditDialog.filePath, L"wb");
					if (pDbWriter != NULL)
					{
						DialogBox(typeDbEditDialog.hInst, MAKEINTRESOURCE(IDD_EDITTYPEDBVERSION), hDlg, TypeDbVersionEditor);
						typeDbEditDialog.classDatabase.Write(pDbWriter, 0, optimize, compress);
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
				goto Free_TypeDbEditorDialog;
			case IDC_BTNTYPEEDIT:
				{
					if (typeDbEditDialog.selectedTypeIndex >= 0 && typeDbEditDialog.selectedTypeIndex < (int)typeDbEditDialog.classDatabase.classes.size())
					{
						typeEditDialog.pType = &typeDbEditDialog.classDatabase.classes[typeDbEditDialog.selectedTypeIndex];
						DialogBox(typeDbEditDialog.hInst, MAKEINTRESOURCE(IDD_EDITTYPE), hDlg, TypeEditor);
					}
				}
				break;
			case IDC_BTNADD:
				{
					ClassDatabaseType newType;
					newType.name.fromStringTable = false;
					char *str = (char*)malloc(1);
					__checkoutofmemory(str==NULL);
					str[0] = 0;
					newType.name.str.string = str;
					newType.assemblyFileName.fromStringTable = false;
					char *str2 = (char*)malloc(1);
					__checkoutofmemory(str2==NULL);
					str2[0] = 0;
					newType.assemblyFileName.str.string = str2;

					newType.classId = -1;
					newType.baseClass = 0;
					typeDbEditDialog.classDatabase.classes.push_back(newType);
					HWND hTypeList = GetDlgItem(hDlg, IDC_TYPELIST);
					ListBox_AddString(hTypeList, TEXT(""));
					ListBox_SetCurSel(hTypeList, typeDbEditDialog.classDatabase.classes.size()-1);
					goto DoUpdateTypeList;
				}
				break;
			case IDC_BTNREMOVE:
				{
					if (typeDbEditDialog.selectedTypeIndex >= 0 && typeDbEditDialog.selectedTypeIndex < (int)typeDbEditDialog.classDatabase.classes.size())
					{
						int selection = typeDbEditDialog.selectedTypeIndex;
						ClassDatabaseType *pType = &typeDbEditDialog.classDatabase.classes[typeDbEditDialog.selectedTypeIndex];
						if (!pType->name.fromStringTable)
							free(const_cast<char*>(pType->name.str.string));
						if (!pType->assemblyFileName.fromStringTable)
							free(const_cast<char*>(pType->assemblyFileName.str.string));
						for (size_t k = 0; k < pType->fields.size(); k++)
						{
							ClassDatabaseTypeField *pField = &pType->fields[k];
							if (!pField->fieldName.fromStringTable)
								free(const_cast<char*>(pField->fieldName.str.string));
							if (!pField->typeName.fromStringTable)
								free(const_cast<char*>(pField->typeName.str.string));
						}
						typeDbEditDialog.classDatabase.classes.erase(typeDbEditDialog.classDatabase.classes.begin()+selection);
						HWND hTypeList = GetDlgItem(hDlg, IDC_TYPELIST);
						ListBox_DeleteString(hTypeList, selection);
						if (selection < (int)typeDbEditDialog.classDatabase.classes.size())
							ListBox_SetCurSel(hTypeList, selection);
						else if (selection > 0)
							ListBox_SetCurSel(hTypeList, selection-1);
						typeDbEditDialog.selectedTypeIndex = -1;
						goto DoUpdateTypeList;
					}
				}
				break;
			case IDC_EDITNAME:
				{
					if (typeDbEditDialog.selectedTypeIndex >= 0 && typeDbEditDialog.selectedTypeIndex < (int)typeDbEditDialog.classDatabase.classes.size())
					{
						ClassDatabaseType *pSelectedType = &typeDbEditDialog.classDatabase.classes[typeDbEditDialog.selectedTypeIndex];
						HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
						HWND hAssetlist = GetDlgItem(hDlg, IDC_TYPELIST);
						char *cNameBuf;
						#ifdef _UNICODE
							int wcTextLen = Edit_GetTextLength(hNameEdit);
							WCHAR *wcNameBuf = (WCHAR*)malloc((wcTextLen+1) * sizeof(WCHAR));
							__checkoutofmemory(wcNameBuf==NULL);
							Edit_GetText(hNameEdit, wcNameBuf, wcTextLen+1);
							wcNameBuf[wcTextLen] = 0;

							ListBox_DeleteString(hAssetlist, typeDbEditDialog.selectedTypeIndex);
							ListBox_InsertString(hAssetlist, typeDbEditDialog.selectedTypeIndex, wcNameBuf);

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
						ListBox_SetCurSel(hAssetlist, typeDbEditDialog.selectedTypeIndex);
						if (!pSelectedType->name.fromStringTable)
							free(const_cast<char*>(pSelectedType->name.str.string));
						pSelectedType->name.fromStringTable = false;
						pSelectedType->name.str.string = cNameBuf;
					}
				}
				break;
			case IDC_EDITTYPEID:
				{
					if (typeDbEditDialog.selectedTypeIndex >= 0 && typeDbEditDialog.selectedTypeIndex < (int)typeDbEditDialog.classDatabase.classes.size())
					{
						ClassDatabaseType *pSelectedType = &typeDbEditDialog.classDatabase.classes[typeDbEditDialog.selectedTypeIndex];
						
						HWND hTypeIdEdit = GetDlgItem(hDlg, IDC_EDITTYPEID);
						int tcTextLen = Edit_GetTextLength(hTypeIdEdit);
						TCHAR *tcNameBuf = (WCHAR*)malloc((tcTextLen+1) * sizeof(WCHAR));
						__checkoutofmemory(tcNameBuf==NULL);
						Edit_GetText(hTypeIdEdit, tcNameBuf, tcTextLen+1);
						*_errno() = 0;
						int typeId = _tcstol(tcNameBuf, NULL, 0);
						if (errno == ERANGE)
						{
							*_errno() = 0;
							typeId = (int)_tcstoul(tcNameBuf, NULL, 0);
						}
						if (errno != ERANGE)
						{
							pSelectedType->classId = typeId;
						}
						free(tcNameBuf);
						
					}
				}
				break;
			case IDC_TYPELIST:
			DoUpdateTypeList:
				{
					HWND hTypeList = GetDlgItem(hDlg, IDC_TYPELIST);
					unsigned int selection = (unsigned int)ListBox_GetCurSel(hTypeList);
					if ((selection != typeDbEditDialog.selectedTypeIndex) && selection < typeDbEditDialog.classDatabase.classes.size())
					{
						typeDbEditDialog.selectedTypeIndex = (int)selection;
						HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
						HWND hTypeIdEdit = GetDlgItem(hDlg, IDC_EDITTYPEID);
						ClassDatabaseType *pSelectedType = &typeDbEditDialog.classDatabase.classes[selection];
						const char *typeName = pSelectedType->name.GetString(&typeDbEditDialog.classDatabase);
						if (typeName != NULL)
						{
							#ifdef _UNICODE
								size_t typeNameLen = strlen(typeName);
								int wcharCount = MultiByteToWideChar(CP_UTF8, 0, typeName, (int)typeNameLen, NULL, 0);
								WCHAR *wcNameBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
								__checkoutofmemory(wcNameBuf==NULL);
								MultiByteToWideChar(CP_UTF8, 0, typeName, (int)typeNameLen, wcNameBuf, wcharCount);
								wcNameBuf[wcharCount] = 0;
								Edit_SetText(hNameEdit, wcNameBuf);
								free(wcNameBuf);
							#else
								Edit_SetText(hNameEdit, typeName);
							#endif
						}
						TCHAR sprntTmp[12];
						_stprintf(sprntTmp, TEXT("0x%08X"), pSelectedType->classId);
						Edit_SetText(hTypeIdEdit, sprntTmp);
					}
				}
				break;
		}
		break;
	}
	return (INT_PTR)FALSE;
Free_TypeDbEditorDialog:
	FreeCOMFilePathBuf(&typeDbEditDialog.filePath);
	for (size_t i = 0; i < typeDbEditDialog.classDatabase.classes.size(); i++)
	{
		ClassDatabaseType *pType = &typeDbEditDialog.classDatabase.classes[i];
		if (!pType->name.fromStringTable)
			free(const_cast<char*>(pType->name.str.string));
		if (!pType->assemblyFileName.fromStringTable)
			free(const_cast<char*>(pType->assemblyFileName.str.string));
		for (size_t k = 0; k < pType->fields.size(); k++)
		{
			ClassDatabaseTypeField *pField = &pType->fields[k];
			if (!pField->fieldName.fromStringTable)
				free(const_cast<char*>(pField->fieldName.str.string));
			if (!pField->typeName.fromStringTable)
				free(const_cast<char*>(pField->typeName.str.string));
		}
	}
	for (int i = 0; i < typeDbEditDialog.classDatabase.header.unityVersionCount; i++)
	{
		if (typeDbVersionEditDialog.isCustomAlloc[i])
		{
			free(typeDbEditDialog.classDatabase.header.pUnityVersions[i]);
		}
	}
	if (typeDbEditDialog.originalUnityVersionList && typeDbEditDialog.classDatabase.header.pUnityVersions &&
		typeDbEditDialog.classDatabase.header.pUnityVersions != typeDbEditDialog.originalUnityVersionList)
	{
		free(typeDbEditDialog.classDatabase.header.pUnityVersions);
		typeDbEditDialog.classDatabase.header.pUnityVersions = typeDbEditDialog.originalUnityVersionList;
	}
	typeDbEditDialog.classDatabase.~ClassDatabaseFile();
	return (INT_PTR)TRUE;
}

INT_PTR CALLBACK TypeDbVersionEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			memset(typeDbVersionEditDialog.isCustomAlloc, 0, 256 * sizeof(bool));
			typeDbEditDialog.originalUnityVersionList = NULL;
			HWND hVersionList = GetDlgItem(hDlg, IDC_VERSIONLIST);
			for (DWORD i = 0; i < typeDbEditDialog.classDatabase.header.unityVersionCount; i++)
			{
				const char *versionName = typeDbEditDialog.classDatabase.header.pUnityVersions[i];
				if (versionName != NULL)
				{
#ifdef _UNICODE
					size_t versionNameLen = strlen(versionName);
					int wcharCount = MultiByteToWideChar(CP_UTF8, 0, versionName, (int)versionNameLen, NULL, 0);
					WCHAR *wcNameBuf = (WCHAR*)malloc((wcharCount+1) * sizeof(WCHAR));
					__checkoutofmemory(wcNameBuf==NULL);
					MultiByteToWideChar(CP_UTF8, 0, versionName, (int)versionNameLen, wcNameBuf, wcharCount);
					wcNameBuf[wcharCount] = 0;
					ListBox_AddString(hVersionList, wcNameBuf);
					free(wcNameBuf);
#else
					ListBox_AddString(hVersionList, versionName);
#endif
				}
				else
					ListBox_AddString(hVersionList, TEXT(""));
			}
			ListBox_SetCurSel(hVersionList, 0);
		}
		return (INT_PTR)TRUE;
		
	case WM_CLOSE:
	case WM_DESTROY:
		EndDialog(hDlg, LOWORD(wParam));
		goto Free_TypeDbVersionEditorDialog;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDOK:
				{
					EndDialog(hDlg, LOWORD(wParam));
				}
				goto Free_TypeDbVersionEditorDialog;
			case IDC_BTNREMOVE:
				{
					HWND hVersionList = GetDlgItem(hDlg, IDC_VERSIONLIST);
					int selection = ListBox_GetCurSel(hVersionList);
					if (selection >= 0 && selection < (int)typeDbEditDialog.classDatabase.header.unityVersionCount)
					{
						char **pNewUnityVersions = (char**)malloc(sizeof(char*) * (typeDbEditDialog.classDatabase.header.unityVersionCount - 1));
						__checkoutofmemory(pNewUnityVersions==NULL);
						memcpy(pNewUnityVersions, 
							typeDbEditDialog.classDatabase.header.pUnityVersions, 
							selection * sizeof(char*));
						memcpy(&pNewUnityVersions[selection], 
							&typeDbEditDialog.classDatabase.header.pUnityVersions[selection+1], 
							(typeDbEditDialog.classDatabase.header.unityVersionCount - 1 - selection) * sizeof(char*));

						memcpy(&typeDbVersionEditDialog.isCustomAlloc[selection], 
							&typeDbVersionEditDialog.isCustomAlloc[selection+1], 
							255 - selection - 1);

						if (typeDbEditDialog.originalUnityVersionList == NULL)
							typeDbEditDialog.originalUnityVersionList = typeDbEditDialog.classDatabase.header.pUnityVersions;
						else if (typeDbEditDialog.originalUnityVersionList != typeDbEditDialog.classDatabase.header.pUnityVersions)
							free(typeDbEditDialog.classDatabase.header.pUnityVersions);
						typeDbEditDialog.classDatabase.header.pUnityVersions = pNewUnityVersions;
						typeDbEditDialog.classDatabase.header.unityVersionCount--;

						ListBox_DeleteString(hVersionList, selection);
						if (selection < (int)typeDbEditDialog.classDatabase.header.unityVersionCount)
							ListBox_SetCurSel(hVersionList, selection);
						else if (selection > 0)
							ListBox_SetCurSel(hVersionList, selection-1);
					}
				}
				break;
			case IDC_BTNADD:
				{
					if (typeDbEditDialog.classDatabase.header.unityVersionCount >= 254)
						break;
					HWND hVersionEdit = GetDlgItem(hDlg, IDC_VERSIONEDIT);
					HWND hVersionList = GetDlgItem(hDlg, IDC_VERSIONLIST);
					//int selection = ListBox_GetCurSel(hVersionList);
					//if (selection >= 0 && selection < (int)typeDbEditDialog.classDatabase.header.unityVersionCount)
					{
						char *cNameBuf;
						#ifdef _UNICODE
							int wcTextLen = Edit_GetTextLength(hVersionEdit);
							WCHAR *wcNameBuf = (WCHAR*)malloc((wcTextLen+1) * sizeof(WCHAR));
							__checkoutofmemory(wcNameBuf==NULL);
							Edit_GetText(hVersionEdit, wcNameBuf, wcTextLen+1);
							wcNameBuf[wcTextLen] = 0;

							ListBox_AddString(hVersionList, wcNameBuf);

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

							ListBox_AddString(hVersionList, cNameBuf);
						#endif
						ListBox_SetCurSel(hVersionList, typeDbEditDialog.classDatabase.header.unityVersionCount);
						char **pNewUnityVersions = (char**)malloc(sizeof(char*) * (typeDbEditDialog.classDatabase.header.unityVersionCount + 1));
						__checkoutofmemory(pNewUnityVersions==NULL);
						memcpy(&pNewUnityVersions[0], 
							typeDbEditDialog.classDatabase.header.pUnityVersions, 
							typeDbEditDialog.classDatabase.header.unityVersionCount * sizeof(char*));
						pNewUnityVersions[typeDbEditDialog.classDatabase.header.unityVersionCount] = cNameBuf;
						if (typeDbEditDialog.originalUnityVersionList == NULL)
							typeDbEditDialog.originalUnityVersionList = typeDbEditDialog.classDatabase.header.pUnityVersions;
						else if (typeDbEditDialog.originalUnityVersionList != typeDbEditDialog.classDatabase.header.pUnityVersions)
							free(typeDbEditDialog.classDatabase.header.pUnityVersions);
						typeDbEditDialog.classDatabase.header.pUnityVersions = pNewUnityVersions;
						typeDbVersionEditDialog.isCustomAlloc[typeDbEditDialog.classDatabase.header.unityVersionCount] = true;
						typeDbEditDialog.classDatabase.header.unityVersionCount++;
					}
				}
				break;
		}
		break;
	}
	return (INT_PTR)FALSE;
Free_TypeDbVersionEditorDialog:
	//nothing to free here, the TypeDbEditorDialog does all this
	return (INT_PTR)TRUE;
}

WCHAR *_TypeEditor_MakeListViewName(ClassDatabaseTypeField *pTypeField)
{
	const char *fieldName = pTypeField->fieldName.GetString(&typeDbEditDialog.classDatabase);
	const char *typeName = pTypeField->typeName.GetString(&typeDbEditDialog.classDatabase);
	int fieldNameMbLen = (int)strlen(fieldName);
	int fieldNameWcLen = MultiByteToWideChar(CP_UTF8, 0, fieldName, fieldNameMbLen, NULL, 0);
	int typeNameMbLen = (int)strlen(typeName);
	int typeNameWcLen = MultiByteToWideChar(CP_UTF8, 0, typeName, typeNameMbLen, NULL, 0);

	WCHAR *treeViewText = (WCHAR*)malloc((typeNameWcLen + 1 + fieldNameWcLen + 1) * sizeof(WCHAR));
	__checkoutofmemory(treeViewText==NULL);
	MultiByteToWideChar(CP_UTF8, 0, typeName, typeNameMbLen, treeViewText, typeNameWcLen);
	treeViewText[typeNameWcLen] = L' ';
	MultiByteToWideChar(CP_UTF8, 0, fieldName, fieldNameMbLen, &treeViewText[typeNameWcLen+1], fieldNameWcLen);
	treeViewText[typeNameWcLen + 1 + fieldNameWcLen] = 0;
	return treeViewText;
}
void _TypeEditor_BuildTreeView(HWND hFieldTree)
{
	size_t fieldCount = typeEditDialog.workCopy.fields.size();
	for (size_t i = 0; i < fieldCount; i++)
	{
		ClassDatabaseTypeField *pTypeField = &typeEditDialog.workCopy.fields[i];
		TVINSERTSTRUCT is;is.hParent = NULL;
		for (size_t _k = i; _k > 0; _k--)
		{
			size_t k = _k - 1;
			if (typeEditDialog.workCopy.fields[k].depth < pTypeField->depth)
			{
				is.hParent = typeEditDialog.fields[k];
				break;
			}
		}
		
		WCHAR *treeViewText = _TypeEditor_MakeListViewName(pTypeField);

		is.itemex.pszText = treeViewText;
		is.itemex.cchTextMax = (int)wcslen(treeViewText)+1;
		is.itemex.state = 0;
		is.itemex.stateMask = 0xFF;
		is.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
		//WCHAR *treeViewString = (WCHAR*)
		if (i == 0)
			is.hInsertAfter = TVI_ROOT;
		else
			is.hInsertAfter = typeEditDialog.fields[i-1];
		if (((i+1) < fieldCount) && (typeEditDialog.workCopy.fields[i+1].depth > pTypeField->depth))
			is.itemex.cChildren = 1;
		else
			is.itemex.cChildren = 0;
		typeEditDialog.fields.push_back(TreeView_InsertItem(hFieldTree, &is));
		free(treeViewText);
	}
}
INT_PTR CALLBACK TypeEditor(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			typeEditDialog.selectedFieldIndex = -1;
			typeEditDialog.workCopy = ClassDatabaseType(*typeEditDialog.pType);
			/*if (!typeEditDialog.workCopy.name.fromStringTable)
			{
				size_t strLen = strlen(typeEditDialog.workCopy.name.str.string);
				char *nameCopy = (char*)malloc(strLen+1);
				__checkoutofmemory(nameCopy==NULL);
				memcpy(nameCopy, typeEditDialog.workCopy.name.str.string, strLen+1);
				typeEditDialog.workCopy.name.str.string = nameCopy;
			}*/ //the type's name can't be changed during the TypeEditor
			HWND hFieldTree = GetDlgItem(hDlg, IDC_TYPETREE);
			size_t fieldCount = typeEditDialog.workCopy.fields.size();
			for (size_t i = 0; i < fieldCount; i++)
			{
				ClassDatabaseTypeField *pTypeField = &typeEditDialog.workCopy.fields[i];
				if (!pTypeField->fieldName.fromStringTable)
				{
					size_t strLen = strlen(pTypeField->fieldName.str.string);
					char *nameCopy = (char*)malloc(strLen+1);
					__checkoutofmemory(nameCopy==NULL);
					memcpy(nameCopy, pTypeField->fieldName.str.string, strLen+1);
					pTypeField->fieldName.str.string = nameCopy;
				}
				if (!pTypeField->typeName.fromStringTable)
				{
					size_t strLen = strlen(pTypeField->typeName.str.string);
					char *nameCopy = (char*)malloc(strLen+1);
					__checkoutofmemory(nameCopy==NULL);
					memcpy(nameCopy, pTypeField->typeName.str.string, strLen+1);
					pTypeField->typeName.str.string = nameCopy;
				}
			}
			_TypeEditor_BuildTreeView(hFieldTree);
		}
		return (INT_PTR)TRUE;
		
	case WM_CLOSE:
	case WM_DESTROY:
		//free the copied strings 
		for (size_t i = 0; i < typeEditDialog.workCopy.fields.size(); i++)
		{
			ClassDatabaseTypeField *pCopiedTypeField = &typeEditDialog.workCopy.fields[i];
			if (!pCopiedTypeField->fieldName.fromStringTable)
				free(const_cast<char*>(pCopiedTypeField->fieldName.str.string));
			if (!pCopiedTypeField->typeName.fromStringTable)
				free(const_cast<char*>(pCopiedTypeField->typeName.str.string));
			//typeEditDialog.workCopy.fields.erase(typeEditDialog.workCopy.fields.begin()+i);
			//i--;
		}
		if (typeEditDialog.workCopy.fields.size() > 0)
			typeEditDialog.workCopy.fields.clear(); //to make sure that the memory only is freed once!
		if (typeEditDialog.fields.size() > 0)
			typeEditDialog.fields.clear(); //to make sure that the memory only is freed once!
		EndDialog(hDlg, LOWORD(wParam));
		goto Free_TypeEditorDialog;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDOK:
				{
					//free the original strings and replace the field's data with the work copy's field data
					for (size_t i = 0; i < typeEditDialog.pType->fields.size(); i++)
					{
						ClassDatabaseTypeField *pOriginalTypeField = &typeEditDialog.pType->fields[i];
						if (!pOriginalTypeField->fieldName.fromStringTable)
							free(const_cast<char*>(pOriginalTypeField->fieldName.str.string));
						if (!pOriginalTypeField->typeName.fromStringTable)
							free(const_cast<char*>(pOriginalTypeField->typeName.str.string));
					}
					typeEditDialog.pType->fields.erase(typeEditDialog.pType->fields.begin(), typeEditDialog.pType->fields.end());
					//typeEditDialog.pType->fields.clear();
					typeEditDialog.pType->fields.reserve(typeEditDialog.workCopy.fields.size());
					for (size_t i = 0; i < typeEditDialog.workCopy.fields.size(); i++)
					{
						ClassDatabaseTypeField *pCopiedTypeField = &typeEditDialog.workCopy.fields[i];
						typeEditDialog.pType->fields.push_back(*pCopiedTypeField);
					}
					typeEditDialog.workCopy.fields.clear();
					typeEditDialog.fields.clear();
					EndDialog(hDlg, LOWORD(wParam));
					goto Free_TypeEditorDialog;
				}
			case IDCANCEL:
				{
					//free the copied strings 
					for (size_t i = 0; i < typeEditDialog.workCopy.fields.size(); i++)
					{
						ClassDatabaseTypeField *pCopiedTypeField = &typeEditDialog.workCopy.fields[i];
						if (!pCopiedTypeField->fieldName.fromStringTable)
							free(const_cast<char*>(pCopiedTypeField->fieldName.str.string));
						if (!pCopiedTypeField->typeName.fromStringTable)
							free(const_cast<char*>(pCopiedTypeField->typeName.str.string));
					}
					typeEditDialog.workCopy.fields.erase(typeEditDialog.workCopy.fields.begin(),typeEditDialog.workCopy.fields.end());
					typeEditDialog.fields.erase(typeEditDialog.fields.begin(),typeEditDialog.fields.end());
					EndDialog(hDlg, LOWORD(wParam));
					goto Free_TypeEditorDialog;
				}
			case IDC_BTNADD:
				{
					HWND hFieldTree = GetDlgItem(hDlg, IDC_TYPETREE);
					if (typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						//initialize the template field
						addFieldDialog.typeField.fieldName.fromStringTable = false;
						addFieldDialog.typeField.typeName.fromStringTable = false;
						char *str = (char*)malloc(1);
						__checkoutofmemory(str==NULL);
						str[0] = 0;
						addFieldDialog.typeField.fieldName.str.string = str;
						str = (char*)malloc(1);
						__checkoutofmemory(str==NULL);
						str[0] = 0;
						addFieldDialog.typeField.typeName.str.string = str;
						addFieldDialog.typeField.depth = 0;
						addFieldDialog.typeField.flags2 = 0;
						addFieldDialog.typeField.isArray = 0;
						addFieldDialog.typeField.size = 0;
						addFieldDialog.success = false;

						if (typeEditDialog.selectedFieldIndex == -1 && typeEditDialog.fields.size() == 0)
						{
							DialogBox(typeDbEditDialog.hInst, MAKEINTRESOURCE(IDD_ADDFIELD), hDlg, AddTypeField);
							if (addFieldDialog.success)
							{
								typeEditDialog.workCopy.fields.push_back(addFieldDialog.typeField);
								_TypeEditor_BuildTreeView(hFieldTree);
							}
						}
						else if (typeEditDialog.selectedFieldIndex >= 0)
						{
							ClassDatabaseTypeField *pField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
							addFieldDialog.typeField.depth = pField->depth;
							DialogBox(typeDbEditDialog.hInst, MAKEINTRESOURCE(IDD_ADDFIELD), hDlg, AddTypeField);
							if (addFieldDialog.success) //if not, AddTypeField frees the strings
							{
								int targetIndex = typeEditDialog.selectedFieldIndex+1;
								TVINSERTSTRUCT is;
								if (addFieldDialog.typeField.depth > pField->depth) //is the new field a child of the selected one?
								{
									//make the selected field expandable if it isn't already
									is.itemex.hItem = typeEditDialog.fields[typeEditDialog.selectedFieldIndex];
									is.itemex.mask = TVIF_HANDLE | TVIF_CHILDREN;
									if (TreeView_GetItem(hFieldTree, &is.itemex) && is.itemex.cChildren != 1)
									{
										is.itemex.cChildren = 1;
										is.itemex.mask = TVIF_HANDLE | TVIF_CHILDREN;
										TreeView_SetItem(hFieldTree, &is.itemex);
									}
									//set the parent field
									is.hParent = typeEditDialog.fields[typeEditDialog.selectedFieldIndex];
								}
								else
								{
									targetIndex = -1;
									for (int i = typeEditDialog.selectedFieldIndex+1; i < (int)typeEditDialog.fields.size(); i++)
									{
										if (typeEditDialog.workCopy.fields[i].depth <= pField->depth)
										{
											targetIndex = i;
											break;
										}
									}
									if (targetIndex == -1)
										targetIndex = (int)typeEditDialog.fields.size();
									is.hParent = NULL;
									for (int i = typeEditDialog.selectedFieldIndex-1; i >= 0; i--)
									{
										if (typeEditDialog.workCopy.fields[i].depth < pField->depth)
										{
											is.hParent = typeEditDialog.fields[i];
											break;
										}
									}
								}
								if (true)
								//if ((addFieldDialog.typeField.depth > pField->depth) && (typeEditDialog.fields.size() > (targetIndex+1)) &&
								//	(typeEditDialog.workCopy.fields[targetIndex+1].depth == addFieldDialog.typeField.depth))
								{
									//rebuild the tree view because it would otherwise append the new field after the parent's last field
									typeEditDialog.workCopy.fields.insert(
										typeEditDialog.workCopy.fields.begin()+targetIndex,
										addFieldDialog.typeField);
								
									//make a backup of the states (rebuilding sets it to 0)
									uint8_t *stateList = (uint8_t*)malloc(typeEditDialog.fields.size() * sizeof(uint8_t));
									__checkoutofmemory(stateList==NULL);
									for (size_t i = 0; i < typeEditDialog.fields.size(); i++)
										stateList[i] = TreeView_GetItemState(hFieldTree, typeEditDialog.fields[i], 0x000D); //all item states except TVIS_SELECTED

									TreeView_DeleteAllItems(hFieldTree);
									typeEditDialog.fields.clear();
									_TypeEditor_BuildTreeView(hFieldTree);

									//backup the expanded state
									int _i = 0;
									for (size_t i = 0; i < typeEditDialog.fields.size(); i++)
									{
										if (i == targetIndex)
											continue;
										TreeView_SetItemState(hFieldTree, typeEditDialog.fields[i], stateList[_i], 0x000D);
										_i++;
									}
									free(stateList);

									TreeView_SelectItem(hFieldTree, typeEditDialog.fields[targetIndex]);
								}
								else
								{
									typeEditDialog.workCopy.fields.insert(
										typeEditDialog.workCopy.fields.begin()+targetIndex,
										addFieldDialog.typeField);

									WCHAR *treeViewText = _TypeEditor_MakeListViewName(&addFieldDialog.typeField);

									//prepare the ListView
									is.itemex.pszText = treeViewText;
									is.itemex.cchTextMax = (int)wcslen(treeViewText);
									is.itemex.state = 0;
									is.itemex.stateMask = 0xFF;
									is.itemex.mask = TVIF_CHILDREN | TVIF_STATE | TVIF_TEXT;
									is.hInsertAfter = typeEditDialog.fields[targetIndex-1];
									is.itemex.cChildren = 0;
									HTREEITEM treeItem = TreeView_InsertItem(hFieldTree, &is);
									free(treeViewText);
									typeEditDialog.fields.insert(
										typeEditDialog.fields.begin()+targetIndex,
										treeItem);
									TreeView_SelectItem(hFieldTree, treeItem);
								}
							}
						}
						else
						{
							free(const_cast<char*>(addFieldDialog.typeField.typeName.str.string));
							free(const_cast<char*>(addFieldDialog.typeField.fieldName.str.string));
						}
					}
				}
				break;
			case IDC_BTNREMOVE:
				{
					HWND hFieldTree = GetDlgItem(hDlg, IDC_TYPETREE);
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						uint8_t oldDepth = pSelField->depth; //after removing an item, it is better not to use pSelField anymore
						int oldSel = typeEditDialog.selectedFieldIndex;
						typeEditDialog.selectedFieldIndex = -1;
						bool forceRemove = true;
						for (int i = oldSel; i < (int)typeEditDialog.fields.size(); i++)
						{
							ClassDatabaseTypeField *pField = &typeEditDialog.workCopy.fields[i];
							//if the current field is (no child of)/(not) the field to delete, select it and break 
							if (!forceRemove && (pField->depth <= oldDepth))
							{
								TreeView_SelectItem(hFieldTree, typeEditDialog.fields[i]);
								typeEditDialog.selectedFieldIndex = i;
								break;
							}
							forceRemove = false;
							if (!pField->fieldName.fromStringTable)
								free(const_cast<char*>(pField->fieldName.str.string));
							if (!pField->typeName.fromStringTable)
								free(const_cast<char*>(pField->typeName.str.string));
							TreeView_DeleteItem(hFieldTree, typeEditDialog.fields[i]);
							typeEditDialog.workCopy.fields.erase(typeEditDialog.workCopy.fields.begin()+i);
							typeEditDialog.fields.erase(typeEditDialog.fields.begin()+i);
							i--;
						}
						//if the last field was the field to delete or a child of it, select the field before it
						if ((typeEditDialog.selectedFieldIndex == -1) && typeEditDialog.fields.size() > 0)
						{
							typeEditDialog.selectedFieldIndex = (int)typeEditDialog.fields.size()-1;
							TreeView_SelectItem(hFieldTree, typeEditDialog.fields[typeEditDialog.selectedFieldIndex]);
						}
					}
				}
				break;
			case IDC_EDITNAME:
				{
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
						char *cNameBuf = GetEditTextA(hNameEdit);
						if (!pSelField->fieldName.fromStringTable)
							free(const_cast<char*>(pSelField->fieldName.str.string));
						pSelField->fieldName.fromStringTable = false;
						pSelField->fieldName.str.string = cNameBuf;

						HWND hFieldTree = GetDlgItem(hDlg, IDC_TYPETREE);
						WCHAR *treeViewText = _TypeEditor_MakeListViewName(pSelField);
						TVITEMEX itemex;
						itemex.hItem = typeEditDialog.fields[typeEditDialog.selectedFieldIndex];
						itemex.mask = TVIF_HANDLE | TVIF_TEXT;
						itemex.pszText = treeViewText;
						itemex.cchTextMax = (int)wcslen(treeViewText);
						TreeView_SetItem(hFieldTree, &itemex);
						free(treeViewText);
					}
				}
				break;
			case IDC_EDITTYPE:
				{
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hTypeEdit = GetDlgItem(hDlg, IDC_EDITTYPE);
						char *cNameBuf = GetEditTextA(hTypeEdit);
						if (!pSelField->typeName.fromStringTable && pSelField->typeName.str.string != NULL)
							free(const_cast<char*>(pSelField->typeName.str.string));
						pSelField->typeName.fromStringTable = false;
						pSelField->typeName.str.string = cNameBuf;

						HWND hFieldTree = GetDlgItem(hDlg, IDC_TYPETREE);
						WCHAR *treeViewText = _TypeEditor_MakeListViewName(pSelField);
						TVITEMEX itemex;
						itemex.hItem = typeEditDialog.fields[typeEditDialog.selectedFieldIndex];
						itemex.mask = TVIF_HANDLE | TVIF_TEXT;
						itemex.pszText = treeViewText;
						itemex.cchTextMax = (int)wcslen(treeViewText);
						TreeView_SetItem(hFieldTree, &itemex);
						free(treeViewText);
					}
				}
				break;
			case IDC_EDITSIZE:
				{
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hSizeEdit = GetDlgItem(hDlg, IDC_EDITSIZE);
						int tcTextLen = Edit_GetTextLength(hSizeEdit);
						TCHAR *tcNameBuf = (WCHAR*)malloc((tcTextLen+1) * sizeof(WCHAR));
						__checkoutofmemory(tcNameBuf==NULL);
						Edit_GetText(hSizeEdit, tcNameBuf, tcTextLen+1);
						*_errno() = 0;
						int size = _tcstol(tcNameBuf, NULL, 0);
						if (errno == ERANGE)
						{
							*_errno() = 0;
							size = (int)_tcstoul(tcNameBuf, NULL, 0);
						}
						if (errno != ERANGE)
						{
							pSelField->size = size;
						}
						free(tcNameBuf);
					}
				}
				break;
			case IDC_EDITVERSION:
				{
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hVersionEdit = GetDlgItem(hDlg, IDC_EDITVERSION);
						int tcTextLen = Edit_GetTextLength(hVersionEdit);
						TCHAR *tcNameBuf = (WCHAR*)malloc((tcTextLen+1) * sizeof(WCHAR));
						__checkoutofmemory(tcNameBuf==NULL);
						Edit_GetText(hVersionEdit, tcNameBuf, tcTextLen+1);
						*_errno() = 0;
						int version = _tcstol(tcNameBuf, NULL, 0);
						if (errno == ERANGE)
						{
							*_errno() = 0;
							version = (int)_tcstoul(tcNameBuf, NULL, 0);
						}
						if (errno != ERANGE)
						{
							pSelField->version = (uint16_t)version;
						}
						free(tcNameBuf);
					}
				}
				break;
			case IDC_CHECKARRAY:
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hArrayCb = GetDlgItem(hDlg, IDC_CHECKARRAY);
						if (Button_GetCheck(hArrayCb) == BST_CHECKED)
							pSelField->isArray = 1;
						else
							pSelField->isArray = 0;
					}
				break;
			case IDC_CHECKALIGN:
					if (typeEditDialog.selectedFieldIndex >= 0 && typeEditDialog.selectedFieldIndex < (int)typeEditDialog.fields.size())
					{
						ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[typeEditDialog.selectedFieldIndex];
						HWND hAlignCb = GetDlgItem(hDlg, IDC_CHECKALIGN);
						if (Button_GetCheck(hAlignCb) == BST_CHECKED)
							pSelField->flags2 = pSelField->flags2 | 0x4000;
						else
							pSelField->flags2 = pSelField->flags2 & (~0x4000);
					}
				break;
		}
		break;
	case WM_NOTIFY:
		{
			switch (((LPNMHDR)lParam)->code)
			{
				case TVN_SELCHANGED:
				{
					LPNMTREEVIEW info = ((LPNMTREEVIEW)lParam);
					if (info->hdr.idFrom == IDC_TYPETREE)
					{
						//DoUpdateFieldList:
						HWND hFieldList = GetDlgItem(hDlg, IDC_TYPETREE);
						HTREEITEM selTreeItem = TreeView_GetSelection(hFieldList);//info->itemNew.hItem
						int selection = -1;
						for (int i = 0; i < (int)typeEditDialog.fields.size(); i++)
						{
							if (typeEditDialog.fields[i] == selTreeItem)
							{
								selection = i;
								break;
							}
						}
						if ((selection != typeEditDialog.selectedFieldIndex) && (selection < (int)typeEditDialog.fields.size()) && (selection >= 0))
						{
							typeEditDialog.selectedFieldIndex = selection;
							HWND hFieldTypeEdit = GetDlgItem(hDlg, IDC_EDITTYPE);
							HWND hFieldNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
							HWND hSizeEdit = GetDlgItem(hDlg, IDC_EDITSIZE);
							HWND hVersionEdit = GetDlgItem(hDlg, IDC_EDITVERSION);
							HWND hArrayCb = GetDlgItem(hDlg, IDC_CHECKARRAY);
							HWND hAlignCb = GetDlgItem(hDlg, IDC_CHECKALIGN);
							ClassDatabaseTypeField *pSelField = &typeEditDialog.workCopy.fields[selection];

							const char *fieldType = pSelField->typeName.GetString(&typeDbEditDialog.classDatabase);
							SetEditTextA(hFieldTypeEdit, (fieldType == NULL) ? "" : fieldType);
							const char *fieldName = pSelField->fieldName.GetString(&typeDbEditDialog.classDatabase);
							SetEditTextA(hFieldNameEdit, (fieldName == NULL) ? "" : fieldName);

							TCHAR sprntTmp[12];
							_stprintf(sprntTmp, TEXT("%d"), (int)pSelField->size);
							Edit_SetText(hSizeEdit, sprntTmp);

							_stprintf(sprntTmp, TEXT("%d"), (int)pSelField->version);
							Edit_SetText(hVersionEdit, sprntTmp);

							if (pSelField->isArray & 1)
								Button_SetCheck(hArrayCb, TRUE);
							else
								Button_SetCheck(hArrayCb, FALSE);

							if (pSelField->flags2 & 0x4000)
								Button_SetCheck(hAlignCb, TRUE);
							else
								Button_SetCheck(hAlignCb, FALSE);
						}
					}
				}
			}
		}
		break;
	}
	return (INT_PTR)FALSE;
Free_TypeEditorDialog:
	//typeEditDialog.workCopy.~ClassDatabaseType();
	return (INT_PTR)TRUE;
}

INT_PTR CALLBACK AddTypeField(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			addFieldDialog.success = false;
			if (addFieldDialog.typeField.depth == 0)
			{
				HWND hIsChildCB = GetDlgItem(hDlg, IDC_CHECKISCHILD);
				Button_SetCheck(hIsChildCB, BST_CHECKED);
				EnableWindow(hIsChildCB, FALSE);
			}
		}
		return (INT_PTR)TRUE;
		
	case WM_CLOSE:
	case WM_DESTROY:
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDOK:
				{
					HWND hIsChildCB = GetDlgItem(hDlg, IDC_CHECKISCHILD);
					HWND hTypeEdit = GetDlgItem(hDlg, IDC_EDITTYPE);
					HWND hNameEdit = GetDlgItem(hDlg, IDC_EDITNAME);
					HWND hSizeEdit = GetDlgItem(hDlg, IDC_EDITSIZE);
					HWND hArrayCB = GetDlgItem(hDlg, IDC_CHECKARRAY);
					HWND hAlignCB = GetDlgItem(hDlg, IDC_CHECKALIGN);
					
					//is child
					if (Button_GetCheck(hIsChildCB) == BST_CHECKED)
						addFieldDialog.typeField.depth++;
					else if (addFieldDialog.typeField.depth == 0)
						break; //Shouldn't (normally) happen, since the button is supposed to be stuck to 'checked' in this case.
					//is array
					addFieldDialog.typeField.isArray = (Button_GetCheck(hArrayCB)==BST_CHECKED) ? 1 : 0;
					//align
					if (Button_GetCheck(hAlignCB)==BST_CHECKED)
						addFieldDialog.typeField.flags2 = addFieldDialog.typeField.flags2 | 0x4000;
					else
						addFieldDialog.typeField.flags2 = addFieldDialog.typeField.flags2 & (~0x4000);
					//size
					int tcTextLen = Edit_GetTextLength(hSizeEdit);
					TCHAR *tcNameBuf = (WCHAR*)malloc((tcTextLen+1) * sizeof(WCHAR));
					__checkoutofmemory(tcNameBuf==NULL);
					Edit_GetText(hSizeEdit, tcNameBuf, tcTextLen+1);
					int size = _tcstol(tcNameBuf, NULL, 0);
					if (errno != ERANGE)
						addFieldDialog.typeField.size = (DWORD)size;
					else
						addFieldDialog.typeField.size = (DWORD)-1;
					//version
					addFieldDialog.typeField.version = 1;
					
					char *cTypeBuf = GetEditTextA(hTypeEdit);
					if (!addFieldDialog.typeField.typeName.fromStringTable && addFieldDialog.typeField.typeName.str.string != NULL)
						free(const_cast<char*>(addFieldDialog.typeField.typeName.str.string));
					addFieldDialog.typeField.typeName.fromStringTable = false;
					addFieldDialog.typeField.typeName.str.string = cTypeBuf;
					char *cNameBuf = GetEditTextA(hNameEdit);
					if (!addFieldDialog.typeField.fieldName.fromStringTable && addFieldDialog.typeField.typeName.str.string != NULL)
						free(const_cast<char*>(addFieldDialog.typeField.fieldName.str.string));
					addFieldDialog.typeField.fieldName.fromStringTable = false;
					addFieldDialog.typeField.fieldName.str.string = cNameBuf;

					addFieldDialog.success = true;
					EndDialog(hDlg, LOWORD(wParam));
					return (INT_PTR)TRUE;
				}
			case IDCANCEL:
				//free the strings allocated by the caller
				if (!addFieldDialog.typeField.typeName.fromStringTable && addFieldDialog.typeField.typeName.str.string != NULL)
					free(const_cast<char*>(addFieldDialog.typeField.typeName.str.string));
				if (!addFieldDialog.typeField.fieldName.fromStringTable && addFieldDialog.typeField.typeName.str.string != NULL)
					free(const_cast<char*>(addFieldDialog.typeField.fieldName.str.string));
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}