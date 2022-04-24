#include "stdafx.h"
#include <WindowsX.h>

#include "BatchImportDialog.h"
#include "../libStringConverter/convert.h"
#include "../UABE_Generic/AssetPluginUtil.h"
#include "resource.h"
#include <regex>

bool CBatchImportDialog::SearchDirectory(const std::wstring &path, const std::string &relativePath, std::vector<std::regex> &regexs, bool searchSubDirs)
{
	WIN32_FIND_DATA findData;
	HANDLE hFind = FindFirstFileW((path + L"\\*").c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;
	//bool ret = false;
	do
	{
		if (0 == wcscmp(findData.cFileName, L".") || 0 == wcscmp(findData.cFileName, L".."))
			continue;
		auto pFileName = unique_WideToMultiByte(findData.cFileName);
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (searchSubDirs)
			{
				//ret |= 
					SearchDirectory(path + L"\\" + findData.cFileName, relativePath + "\\" + pFileName.get(), regexs, true);
			}
		}
		else
		{
			for (size_t i = 0; i < regexs.size(); i++)
			{
				std::cmatch match;
				if (!std::regex_match(pFileName.get(), match, regexs[0]))
					continue;
				std::vector<std::string> capturingGroupsStr;
				std::vector<const char*> capturingGroups;
				//Retrieve the capturing groups as UTF-8 char*.
				if (match.size() > 1)
				{
					capturingGroupsStr.resize(match.size() - 1);
					capturingGroups.resize(match.size() - 1);
					for (size_t j = 0; (j+1) < match.size(); j++)
					{
						capturingGroupsStr[j] = match[j+1].str();
						capturingGroups[j] = capturingGroupsStr[j].c_str();
					}
				}

				size_t matchIndex = (size_t)-1;
				if (this->pDesc->GetFilenameMatchInfo(pFileName.get(), capturingGroups, matchIndex) && (matchIndex < this->assetInfo.size()))
				{
					std::vector<AssetInfo::FileListEntry> &fileList = this->assetInfo[matchIndex].fileList;
					size_t targetIndex = fileList.size();
					fileList.resize(fileList.size() + 1);

					fileList[targetIndex].isRelative = true;
					fileList[targetIndex].path = (relativePath + "\\" + pFileName.get());

					//ret = true;
					break;
				}
			}
		}
	} while (FindNextFileW(hFind, &findData));
	FindClose(hFind);
	return true;
}

bool CBatchImportDialog::GenerateFileLists()
{
	std::vector<const char*> regexStrings; bool checkSubdirs = false;
	this->pDesc->GetFilenameMatchStrings(regexStrings, checkSubdirs);
	if (regexStrings.size() == 0) return false;

	std::vector<std::regex> regexs(regexStrings.size());
	for (size_t i = 0; i < regexStrings.size(); i++)
	{
		regexs[i].assign(regexStrings[i], std::regex_constants::ECMAScript);
	}
	
	size_t basePathWLen = 0;
	wchar_t *basePathW = _MultiByteToWide(basePath.c_str(), basePathWLen);
	if (!basePathW) return false;

	this->SearchDirectory(std::wstring(basePathW), std::string("."), regexs, checkSubdirs);

	_FreeWCHAR(basePathW);
	return true;
}

size_t CBatchImportDialog::GetCurAssetInfoIndex(unsigned int selection)
{
	HWND hAssetlist = GetDlgItem(this->hWnd, IDC_ASSETLIST);
	if (hAssetlist != NULL)
	{
		if (selection == (unsigned int)-1)
			selection = (unsigned int)ListView_GetNextItem(hAssetlist, -1, LVNI_SELECTED);
		if ((selection >= 0) && (selection < assetInfo.size()))
		{
			LVITEM item;
			memset(&item, 0, sizeof(LVITEM));
			item.iItem = selection;
			item.iSubItem = 0;
			item.mask = LVIF_PARAM;
			ListView_GetItem(hAssetlist, &item);
			size_t ret = (size_t)item.lParam;
			if (ret >= assetInfo.size()) return (size_t)-1;
			return ret;
		}
	}
	return (size_t)-1;
}

void CBatchImportDialog::UpdateDialogFileList(size_t assetInfoIndex)
{
	this->updatingAssetList = true;

	HWND hFileList = GetDlgItem(this->hWnd, IDC_FILELIST);
	ListBox_ResetContent(hFileList);
	
	int extent = 0;
	if (assetInfoIndex < this->assetInfo.size())
	{
		std::vector<AssetInfo::FileListEntry> &fileList = this->assetInfo[assetInfoIndex].fileList;
		for (size_t i = 0; i < fileList.size(); i++)
		{
			size_t filePathTLen = 0;
			TCHAR *filePathT = _MultiByteToTCHAR(fileList[i].path.c_str(), filePathTLen);
			ListBox_AddString(hFileList, filePathT ? filePathT : L"");

			if (filePathT)
			{
				HDC hListDC = GetDC(hFileList);
				HGDIOBJ hOrigObject = SelectObject(hListDC, GetWindowFont(hFileList));
				RECT textRect = {};
				DrawText(hListDC, filePathT, -1, &textRect, DT_SINGLELINE | DT_CALCRECT);
				SelectObject(hListDC, hOrigObject);
				ReleaseDC(hFileList, hListDC);

				extent = std::max<long>(extent, textRect.right-textRect.left + 4);
			}

			_FreeTCHAR(filePathT);
		}
	}
	ListBox_SetHorizontalExtent(hFileList, extent);

	this->updatingAssetList = false;
}

int CALLBACK CBatchImportDialog::AssetlistSortCallback(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	size_t assetDescIndex1 = (size_t)lParam1;
	size_t assetDescIndex2 = (size_t)lParam2;
	CBatchImportDialog *pThis = (CBatchImportDialog*)lParamSort;
	if (assetDescIndex1 < pThis->assetInfo.size() && assetDescIndex2 < pThis->assetInfo.size())
	{
		bool swap = pThis->dialogSortDirReverse;
		int retGreater = swap ? 1 : -1;
		int retSmaller = swap ? -1 : 1;
		switch (pThis->dialogSortColumnIdx)
		{
			case 0:
			{
				std::string &s1 = pThis->assetInfo[assetDescIndex1].description;
				std::string &s2 = pThis->assetInfo[assetDescIndex2].description;

				if ( s1 > s2 ) return retGreater;
				if ( s1 < s2 ) return retSmaller;
				return 0;
			}
			case 1:
			{
				std::string &s1 = pThis->assetInfo[assetDescIndex1].assetsFileName;
				std::string &s2 = pThis->assetInfo[assetDescIndex2].assetsFileName;

				if ( s1 > s2 ) return retGreater;
				if ( s1 < s2 ) return retSmaller;
				return 0;
			}
			case 2:
			{
				long long int pathId1 = pThis->assetInfo[assetDescIndex1].pathId;
				long long int pathId2 = pThis->assetInfo[assetDescIndex2].pathId;

				if ( pathId1 > pathId2 ) return retGreater;
				if ( pathId1 < pathId2 ) return retSmaller;
				return 0;
			}
		}
	}
	return 0;
}
INT_PTR CALLBACK CBatchImportDialog::WindowHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	CBatchImportDialog* pThis = (CBatchImportDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	INT_PTR ret = (INT_PTR)FALSE;
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_CLOSE:
		if (pThis && pThis->modeless)
		{
			pThis->hWnd = NULL;
			DestroyWindow(hDlg);
		}
		ret = (INT_PTR)TRUE;
		break;
	case WM_DESTROY:
		if (pThis)
		{
			pThis->assetInfo.clear();
			pThis->hWnd = NULL;
			if (pThis->closeCallback)
			{
				pThis->closeCallback(false);
			}
			SetWindowLongPtr(hDlg, GWLP_USERDATA, NULL);
		}
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (CBatchImportDialog*)lParam;
			pThis->hWnd = hDlg;
			
			pThis->dialogSortColumnIdx = 0;
			pThis->dialogSortDirReverse = true;
			pThis->updatingAssetList = false;

			HWND hAssetList = GetDlgItem(hDlg, IDC_ASSETLIST);
			HWND hFileList = GetDlgItem(hDlg, IDC_FILELIST);
			HWND hEditAssetButton = GetDlgItem(hDlg, IDC_EDITASSETBTN);
		
			{
				LVCOLUMN column;
				ZeroMemory(&column, sizeof(LVCOLUMN));
				column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
				column.cx = 180;
				column.pszText = const_cast<TCHAR*>(TEXT("Description"));
				ListView_InsertColumn(hAssetList, 0, &column);
				column.cx = 140;
				column.pszText = const_cast<TCHAR*>(TEXT("File"));
				ListView_InsertColumn(hAssetList, 1, &column);
				column.cx = 80;
				column.pszText = const_cast<TCHAR*>(TEXT("Path ID"));
				ListView_InsertColumn(hAssetList, 2, &column);
			}
			{
				if (pThis->pDescWin32 == nullptr || !pThis->pDescWin32->ShowAssetSettings((size_t)-1, hDlg))
					ShowWindow(hEditAssetButton, SW_HIDE);
			}
			//Create a tooltip for the file list.
			{
				//https://msdn.microsoft.com/en-us/library/windows/desktop/hh298368(v=vs.85).aspx
				HWND hWndTooltip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
					WS_POPUP |TTS_ALWAYSTIP | TTS_BALLOON,
					CW_USEDEFAULT, CW_USEDEFAULT,
					CW_USEDEFAULT, CW_USEDEFAULT,
					hDlg, NULL, 
					pThis->hInstance, NULL);
				if (hWndTooltip)
				{
					TOOLINFO toolInfo = {};
					toolInfo.cbSize = sizeof(toolInfo);
					toolInfo.hwnd = hDlg;
					toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
					toolInfo.uId = (uintptr_t)hFileList;
					toolInfo.lpszText = const_cast<TCHAR*>(TEXT("Double click an item to move it up and use it for import."));
					SendMessage(hWndTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
				}
			}

			std::vector<IAssetBatchImportDesc::AssetDesc> newAssetDescs;
			if (pThis->pDesc->GetImportableAssetDescs(newAssetDescs))
			{
				pThis->assetInfo.resize(newAssetDescs.size());

				if (pThis->GenerateFileLists())
				{
					LVITEM item;
					ZeroMemory(&item, sizeof(LVITEM));
					item.iSubItem = 0;
					item.cchTextMax = 255;
					for (size_t i = 0; i < std::min<size_t>(INT_MAX, newAssetDescs.size()); i++)
					{
						const std::string &curDescription = newAssetDescs[i].description;
						pThis->assetInfo[i].description = curDescription;

						size_t _strLen = 0;
						TCHAR *tcName = _MultiByteToTCHAR(curDescription.c_str(), _strLen);

						item.mask = (tcName ? LVIF_TEXT : 0) | LVIF_PARAM;
						item.lParam = i;
						item.iItem = (int)i;
						item.iSubItem = 0;
						item.pszText = tcName;

						ListView_InsertItem(hAssetList, &item);

						_FreeTCHAR(tcName);
						
						const std::string &curAssetsFileName = newAssetDescs[i].assetsFileName;
						pThis->assetInfo[i].assetsFileName = curAssetsFileName;

						TCHAR *tcAssetsFileName = _MultiByteToTCHAR(curAssetsFileName.c_str(), _strLen);

						item.mask = (tcAssetsFileName ? LVIF_TEXT : 0);
						item.iSubItem = 1;
						item.pszText = tcAssetsFileName;
						ListView_SetItem(hAssetList, &item);

						_FreeTCHAR(tcAssetsFileName);
						
						pThis->assetInfo[i].pathId = newAssetDescs[i].pathID;
						TCHAR pathIdBuf[32];
						_stprintf_s(pathIdBuf, _T("%lli"), newAssetDescs[i].pathID);

						item.mask = LVIF_TEXT;
						item.iSubItem = 2;
						item.pszText = pathIdBuf;
						ListView_SetItem(hAssetList, &item);
					}

					ListView_SetItemState(hAssetList, 0, 0, LVIS_SELECTED);
				}
				else
				{
					pThis->modeless ? DestroyWindow(hDlg) : EndDialog(hDlg, FALSE);
				}
			}
			else
			{
				pThis->modeless ? DestroyWindow(hDlg) : EndDialog(hDlg, FALSE);
			}

			ret = (INT_PTR)TRUE;
			goto CASE_WM_SIZE;
		}
	case WM_NOTIFY:
		if (pThis != nullptr)
		{
			NMLISTVIEW *pNotifyLV = (NMLISTVIEW*)lParam;
			switch (pNotifyLV->hdr.code)
			{
				case LVN_ITEMCHANGED:
					if (pNotifyLV->uNewState & LVIS_SELECTED)
					{
						if (!pThis->updatingAssetList)
							pThis->UpdateDialogFileList(pThis->GetCurAssetInfoIndex((unsigned int)pNotifyLV->iItem));
					}
					break;
				case LVN_COLUMNCLICK:
					if (pThis->dialogSortColumnIdx == pNotifyLV->iSubItem)
					{
						pThis->dialogSortDirReverse ^= true;
					}
					else
					{
						pThis->dialogSortColumnIdx = pNotifyLV->iSubItem;
						pThis->dialogSortDirReverse = false;
					}

					pThis->updatingAssetList = true;
					ListView_SortItems(pNotifyLV->hdr.hwndFrom, AssetlistSortCallback, (LPARAM)pThis);
					pThis->updatingAssetList = false;

					pThis->UpdateDialogFileList(pThis->GetCurAssetInfoIndex());
					break;
			}
		}
		break;
	case WM_COMMAND:
		if (pThis != nullptr)
		{
			wmId    = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			switch (wmId)
			{
				case IDC_FILELIST:
					{
						switch (wmEvent)
						{
							case LBN_DBLCLK:
							{
								HWND hFileList = (HWND)lParam;
								unsigned int fileListSel = 0;
								size_t curAssetInfoIndex = pThis->GetCurAssetInfoIndex();
								if (curAssetInfoIndex < pThis->assetInfo.size())
								{
									fileListSel = (unsigned int)ListBox_GetCurSel(hFileList);

									std::vector<AssetInfo::FileListEntry> &fileList = pThis->assetInfo[curAssetInfoIndex].fileList;
									if (fileListSel > 0 && fileListSel < fileList.size())
									{
										AssetInfo::FileListEntry entry = fileList[fileListSel];
										fileList.erase(fileList.begin() + fileListSel);
										fileList.insert(fileList.begin(), entry);
										fileListSel = 0;
									}
								}
								pThis->UpdateDialogFileList(pThis->GetCurAssetInfoIndex());
								ListBox_SetCurSel(hFileList, (int)fileListSel);
							}
						}
					}
					break;
				case IDC_EDITASSETBTN:
					{
						HWND hFileList = GetDlgItem(hDlg, IDC_FILELIST);

						size_t curAssetInfoIndex = pThis->GetCurAssetInfoIndex();
						if (curAssetInfoIndex < pThis->assetInfo.size())
						{
							if (pThis->pDescWin32 != nullptr
								&& pThis->pDescWin32->ShowAssetSettings(curAssetInfoIndex, pThis->modeless ? pThis->hParentWnd : hDlg))
							{
								AssetInfo::FileListEntry overrideEntry;
								overrideEntry.isRelative = false;
								if (pThis->pDesc->HasFilenameOverride(curAssetInfoIndex, overrideEntry.path, overrideEntry.isRelative))
								{
									std::vector<AssetInfo::FileListEntry> &fileList = pThis->assetInfo[curAssetInfoIndex].fileList;
									bool entryExists = false;
									for (size_t i = 0; i < fileList.size(); i++)
									{
										if (fileList[i].isRelative == overrideEntry.isRelative && !fileList[i].path.compare(overrideEntry.path))
										{
											entryExists = true;
											break;
										}
									}
									if (!entryExists)
									{
										fileList.insert(fileList.begin(), overrideEntry);
										pThis->UpdateDialogFileList(curAssetInfoIndex);
										ListBox_SetCurSel(hFileList, (int)0);
									}
								}
							}
						}
					}
					break;
				case IDOK:
					{
						for (size_t i = 0; i < pThis->assetInfo.size(); i++)
						{
							std::string fullFilePathStr;
							const char *fullFilePathCStr = nullptr;
							if (pThis->assetInfo[i].fileList.size() > 0)
							{
								if (pThis->assetInfo[i].fileList[0].isRelative)
								{
									if (!pThis->basePath.empty())
									{
										fullFilePathStr = pThis->basePath;
										fullFilePathStr += "\\";
									}
									fullFilePathStr += pThis->assetInfo[i].fileList[0].path;
									fullFilePathCStr = fullFilePathStr.c_str();
								}
								else
									fullFilePathCStr = pThis->assetInfo[i].fileList[0].path.c_str();
							}
							pThis->pDesc->SetInputFilepath(i, fullFilePathCStr);
						}
						bool modeless = pThis->modeless;
						if (pThis->closeCallback)
						{
							pThis->closeCallback(true);
						}
						modeless ? DestroyWindow(hDlg) : EndDialog(hDlg, TRUE);
					}
					return (INT_PTR)TRUE;
				case IDCANCEL:
					pThis->modeless ? DestroyWindow(hDlg) : EndDialog(hDlg, FALSE);
					return (INT_PTR)TRUE;
			}
		}
		break;
	case WM_SIZE:
	CASE_WM_SIZE:
		{
			RECT client;
			GetClientRect(hDlg, &client);
			LONG clientWidth = client.right-client.left;
			LONG clientHeight = client.bottom-client.top;
			MoveWindow(GetDlgItem(hDlg, IDC_ASSETSSTATIC), 19, 10, (3*clientWidth / 5) + 7, 15, true);
			MoveWindow(GetDlgItem(hDlg, IDC_ASSETLIST), 19, 30, (3*clientWidth / 5) + 7, clientHeight - 72, true);
			MoveWindow(GetDlgItem(hDlg, IDC_EDITASSETBTN), 19, clientHeight - 33, 75, 26, true);
			MoveWindow(GetDlgItem(hDlg, IDC_FILESSTATIC), (3*clientWidth / 5) + 40, 10, (2*clientWidth / 5) - 56, 15, true);
			MoveWindow(GetDlgItem(hDlg, IDC_FILELIST), (3*clientWidth / 5) + 40, 30, (2*clientWidth / 5) - 56, clientHeight - 72, true);
			MoveWindow(GetDlgItem(hDlg, IDCANCEL), clientWidth - 91, clientHeight - 33, 75, 26, true);
			MoveWindow(GetDlgItem(hDlg, IDOK), (3*clientWidth / 5) + 40, clientHeight - 33, 75, 26, true);
			UpdateWindow(hDlg);
			break;
		}
	}
	return ret;
}

CBatchImportDialog::CBatchImportDialog(HINSTANCE hInstance,
		IAssetBatchImportDesc* pDesc, IWin32AssetBatchImportDesc* pDescWin32,
		std::string _basePath)
	: hParentWnd(hParentWnd), hWnd(NULL), modeless(false), hInstance(hInstance),
	dialogSortColumnIdx(0), dialogSortDirReverse(false), updatingAssetList(false),
	pDesc(pDesc), pDescWin32(pDescWin32), basePath(std::move(_basePath))
{
}
CBatchImportDialog::~CBatchImportDialog()
{
	if (this->hWnd)
		SendMessage(this->hWnd, WM_CLOSE, 0, 0);
}
void CBatchImportDialog::Hide()
{
	if (this->hWnd)
	{
		ShowWindow(this->hWnd, SW_HIDE);
		SetParent(this->hWnd, NULL);
	}
}
bool CBatchImportDialog::ShowModal(HWND hParentWnd)
{
	this->hParentWnd = hParentWnd;
	if (this->hWnd)
		return false;
	this->modeless = false;
	return (DialogBoxParam(this->hInstance, MAKEINTRESOURCE(IDD_BATCHIMPORT), this->hParentWnd, WindowHandler, (LPARAM)this) == TRUE);
}
bool CBatchImportDialog::ShowModeless(HWND hParentWnd)
{
	this->hParentWnd = hParentWnd;
	if (this->hWnd)
	{
		SetParent(this->hWnd, hParentWnd);
		ShowWindow(this->hWnd, SW_SHOW);
		return true;
	}
	else
	{
		//Modify the dialog style before creating the dialog
		//-> Load the dialog resource in memory and change the style flags,
		//   then create the dialog using CreateDialogIndirectParam.
		//https://docs.microsoft.com/en-us/windows/win32/dlgbox/dlgtemplateex
		//https://devblogs.microsoft.com/oldnewthing/20040623-00/?p=38753
		struct _DLGTEMPLATEEX_HEADER {
			uint16_t dlgVer;
			uint16_t signature;
			DWORD helpID;
			DWORD exStyle;
			DWORD style;
			uint16_t cDlgItems;
			short x;
			short y;
			short cx;
			short cy;
		};

		bool ret = false;
		std::vector<uint8_t> modelessResource;
		{
			HRSRC hResource = FindResourceExW(hInstance, RT_DIALOG, MAKEINTRESOURCE(IDD_BATCHIMPORT), 0);
			if (hResource == NULL)
				return false;
			HGLOBAL hLoadedResource = LoadResource(hInstance, hResource);
			if (hLoadedResource == NULL)
				return false;
			std::unique_ptr<void, decltype(FreeResource)*> _raii_hLoadedResource(hLoadedResource, FreeResource);
			LPVOID pResourceData = LockResource(hLoadedResource);
			if (pResourceData == NULL)
				return false;
			DWORD size = SizeofResource(hInstance, hResource);
			modelessResource.assign((uint8_t*)pResourceData, (uint8_t*)pResourceData + size);
		}
		_DLGTEMPLATEEX_HEADER* pDlgTemplateHeader = reinterpret_cast<_DLGTEMPLATEEX_HEADER*>(modelessResource.data());
		if (modelessResource.size() < sizeof(_DLGTEMPLATEEX_HEADER)
			|| pDlgTemplateHeader->signature != 0xFFFF || pDlgTemplateHeader->dlgVer != 1)
			return false;
		pDlgTemplateHeader->style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
		pDlgTemplateHeader->style |= WS_CHILD | WS_SYSMENU;
		this->modeless = true;
		this->hWnd = CreateDialogIndirectParam(this->hInstance, (DLGTEMPLATE*)pDlgTemplateHeader, this->hParentWnd, WindowHandler, (LPARAM)this);
		return (this->hWnd != NULL);
	}
}