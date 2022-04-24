#include "stdafx.h"
#include "AddAssetDialog.h"
#include "resource.h"
#include "../libStringConverter/convert.h"
#include "MonoBehaviourManager.h"
#include "CreateEmptyValueField.h"
#include <WindowsX.h>
#include <unordered_map>

void AddAssetDialog::open()
{
	DialogBoxParam(appContext.getMainWindow().getHInstance(),
		MAKEINTRESOURCE(IDD_ADDASSET),
		appContext.getMainWindow().getWindow(),
		DlgProc, (LPARAM)this);
}

void AddAssetDialog::EnumScriptIndices_HandleMonoScript(AssetsFileContextInfo *pSourceFile, unsigned int scriptFileRefIdx,
	long long int scriptPathID, std::vector<ScriptIdxDescriptor> &descriptors)
{
	bool exists = false;
	for (size_t i = 0; i < descriptors.size(); i++)
	{
		if (descriptors[i].monoScriptFileIDRel == scriptFileRefIdx
			&& descriptors[i].monoScriptPathID == scriptPathID)
		{
			exists = true;
			break;
		}
	}
	if (!exists)
	{
		ScriptIdxDescriptor descriptor = {};
		descriptor.monoClassID = 0xFFFF;
		descriptor.isNewClassID = false;
		descriptor.monoScriptFileIDRel = scriptFileRefIdx;
		descriptor.monoScriptPathID = scriptPathID;
		descriptors.push_back(descriptor);
	}
}

unsigned int AddAssetDialog::findRelFileID(AssetsFileContextInfo *pSourceFile, unsigned int targetFileID)
{
	if (targetFileID == pSourceFile->getFileID())
		return 0;

	unsigned int targetFileIDRel = (unsigned int)-1;
	auto refLock = pSourceFile->lockReferencesRead();
	const std::vector<unsigned int> &sourceReferences = pSourceFile->getReferencesRead(refLock);

	auto targetFileIDRefIt = std::find(sourceReferences.begin(), sourceReferences.end(), targetFileID);
	if (targetFileIDRefIt != sourceReferences.end())
		targetFileIDRel = (unsigned int)(std::distance(sourceReferences.begin(), targetFileIDRefIt) + 1);

	return targetFileIDRel;
}

void AddAssetDialog::EnumScriptIndices_HandleMonoBehaviour(AssetsFileContextInfo *pSourceFile,
	AssetIdentifier &behaviourAsset,
	AssetTypeTemplateField *pBehaviourBase, std::vector<ScriptIdxDescriptor> &descriptors)
{
	if (!behaviourAsset.resolve(appContext))
		return;
	IAssetsReader_ptr pReader = behaviourAsset.makeReader();
	if (pReader == nullptr)
		return;
	
	AssetTypeInstance instance(1, &pBehaviourBase, behaviourAsset.getDataSize(), pReader.get(), behaviourAsset.isBigEndian());
	AssetTypeValueField *pBaseField = instance.GetBaseField();
	if (pBaseField == nullptr)
		return;

	AssetTypeValue *pFileIDValue = pBaseField->Get("m_Script")->Get("m_FileID")->GetValue();
	AssetTypeValue *pPathIDValue = pBaseField->Get("m_Script")->Get("m_PathID")->GetValue();
	if (pFileIDValue == nullptr || pPathIDValue == nullptr)
		return;

	unsigned int scriptFileID_RelBehaviour = pFileIDValue->AsUInt();
	unsigned int scriptFileID = behaviourAsset.pFile->resolveRelativeFileID(pFileIDValue->AsUInt());
	long long int scriptPathID = pPathIDValue->AsInt64();
	if (scriptFileID == 0 || scriptPathID == 0)
		return;

	unsigned int scriptFileIDRel = findRelFileID(pSourceFile, scriptFileID);
	if (scriptFileIDRel == (unsigned int)-1)
		return;

	struct {
		bool operator()(AssetsFileContextInfo *pSourceFile, AppContext &appContext,
			AssetIdentifier &behaviourAsset, AssetTypeTemplateField *pPureBehaviourBase)
		{
			AssetTypeTemplateField fullBehaviourBase;
			if (pSourceFile->MakeTemplateField(&fullBehaviourBase, appContext,
					behaviourAsset.getClassID(appContext), behaviourAsset.getMonoScriptID(appContext), &behaviourAsset)
				&& fullBehaviourBase.children.size() > pPureBehaviourBase->children.size())
			{
				return true;
			}
			return false;
		}
	} getExtendedTypeInfoPresent;

	bool exists = false;
	for (size_t i = 0; i < descriptors.size(); i++)
	{
		if (descriptors[i].monoScriptFileIDRel == scriptFileIDRel
			&& descriptors[i].monoScriptPathID == scriptPathID)
		{
			if (behaviourAsset.fileID == pSourceFile->getFileID())
			{
				if (descriptors[i].monoClassID == (decltype(descriptors[i].monoClassID))-1)
					descriptors[i].extendedTypeInfoPresent = getExtendedTypeInfoPresent(pSourceFile, appContext, behaviourAsset, pBehaviourBase);
				descriptors[i].monoClassID = behaviourAsset.getMonoScriptID(appContext);
			}
			exists = true;
			break;
		}
		else if (descriptors[i].monoClassID == behaviourAsset.getMonoScriptID(appContext))
		{
			exists = true;
			break;
		}
	}
	if (!exists)
	{
		ScriptIdxDescriptor descriptor = {};
		descriptor.monoClassID = behaviourAsset.getMonoScriptID(appContext);
		descriptor.isNewClassID = false;
		descriptor.monoScriptFileIDRel = scriptFileIDRel;
		descriptor.monoScriptPathID = scriptPathID;
descriptor.extendedTypeInfoPresent = getExtendedTypeInfoPresent(pSourceFile, appContext, behaviourAsset, pBehaviourBase);

		descriptors.push_back(descriptor);
	}
}

int AddAssetDialog::GetTextExtent(HWND hComboBox, const TCHAR *text)
{
	HDC hListDC = GetDC(hComboBox);
	HGDIOBJ hOrigObject = SelectObject(hListDC, GetWindowFont(hComboBox));
	RECT textRect = {};
	DrawText(hListDC, text, -1, &textRect, DT_SINGLELINE | DT_CALCRECT);
	SelectObject(hListDC, hOrigObject);
	ReleaseDC(hComboBox, hListDC);

	return textRect.right-textRect.left + 4;
}

bool AddAssetDialog::EnumScriptIndices(AssetsFileContextInfo *pFile, std::vector<ScriptIdxDescriptor> &descriptors)
{
	descriptors.clear();

	uint16_t maxScriptIndex = 0xFFFF; //max script index of fileID
	const std::vector<unsigned int> references = pFile->getReferences();
	
	//Enumerate the MonoScript assets reachable from pFile (i.e. inside pFile or in a referenced file).
	// -> Store the relative file ID and path ID.
	// -> Retrieve the script index inside pFile, if a MonoBehavior asset exists for that script (otherwise 0xFFFF).
	//Store the result in descriptors.
	bool useLongPathID = false;
	{
		for (unsigned int i = 0; i <= references.size(); i++)
		{
			unsigned int targetFileID = 0;
			if (i == 0) targetFileID = pFile->getFileID();
			else targetFileID = references[i-1];
			FileContextInfo_ptr pTargetFileAny; 
			if (targetFileID == 0
				|| !(pTargetFileAny = appContext.getContextInfo(targetFileID))
				|| pTargetFileAny->getFileContext() == nullptr
				|| pTargetFileAny->getFileContext()->getType() != FileContext_Assets)
				continue;
			AssetsFileContextInfo *pTargetFile = reinterpret_cast<AssetsFileContextInfo*>(pTargetFileAny.get());

			int scriptClassId = pTargetFile->GetClassByName("MonoScript");
			int behaviourClassId = pTargetFile->GetClassByName("MonoBehaviour");

			AssetTypeTemplateField behaviourBase;
			if (behaviourClassId >= 0)
				pTargetFile->MakeTemplateField(&behaviourBase, appContext, behaviourClassId);

			for (AssetIterator iter(pTargetFile); !iter.isEnd(); ++iter)
			{
				AssetIdentifier curAsset;
				iter.get(curAsset);
				curAsset.resolve(appContext);
				if (curAsset.getClassID() == scriptClassId)
				{
					EnumScriptIndices_HandleMonoScript(pFile, i, (long long)curAsset.pathID, descriptors);
				}
				else if (targetFileID == pFile->getFileID() && curAsset.getMonoScriptID() != 0xFFFF)
				{
					//MonoBehaviours are used to retrieve the expected script index, which is different for each .assets file.
					EnumScriptIndices_HandleMonoBehaviour(pFile, curAsset, &behaviourBase, descriptors);
					if (curAsset.getMonoScriptID() > maxScriptIndex || maxScriptIndex == 0xFFFF)
						maxScriptIndex = curAsset.getMonoScriptID();
				}
			}
		}
	}

	//Cache the temporarily (but 'randomly') needed MonoScript deserialization templates.
	//-> Retrievable through getScriptTemplate below.
	std::unordered_map<unsigned int, std::unique_ptr<AssetTypeTemplateField>> scriptTemplatesByFileID;
	//Ancient pre-C++11 lambda equivalent (TODO: modernize this).
	struct _Lambda_GetScriptTemplate {
		std::unordered_map<unsigned int, std::unique_ptr<AssetTypeTemplateField>> &scriptTemplatesByFileID;
		AppContext &appContext;
		_Lambda_GetScriptTemplate(decltype(scriptTemplatesByFileID) &scriptTemplatesByFileID, AppContext &appContext)
			: scriptTemplatesByFileID(scriptTemplatesByFileID), appContext(appContext)
		{}
		AssetTypeTemplateField *operator()(unsigned int fileID)
		{
			auto entryIt = scriptTemplatesByFileID.find(fileID);
			if (entryIt != scriptTemplatesByFileID.end())
				return entryIt->second.get();
			FileContextInfo_ptr pContextInfo = appContext.getContextInfo(fileID);
			if (pContextInfo == nullptr
				|| pContextInfo->getFileContext() == nullptr
				|| pContextInfo->getFileContext()->getType() != FileContext_Assets)
				return nullptr;
			AssetsFileContextInfo *pFile = reinterpret_cast<AssetsFileContextInfo*>(pContextInfo.get());
			int32_t scriptClassID = pFile->GetClassByName("MonoScript");
			if (scriptClassID < 0)
				return nullptr;
			std::unique_ptr<AssetTypeTemplateField> pTemplate(new AssetTypeTemplateField());
			if (pFile->MakeTemplateField(pTemplate.get(), appContext, scriptClassID) && pTemplate->children.size() > 0)
			{
				return (scriptTemplatesByFileID[fileID] = std::move(pTemplate)).get();
			}
			return nullptr;
		}
	} getScriptTemplate(scriptTemplatesByFileID, appContext);

	//For each enumerated MonoScript:
	// -> Retrieve the hash for the script properties
	// -> Generate a human-facing identifier from the strings inside the MonoScript.
	for (size_t i = 0; i < descriptors.size(); i++)
	{
		ScriptIdxDescriptor &descriptor = descriptors[i];
		descriptor.propertiesHash.qValue[0] = descriptor.propertiesHash.qValue[1] = 0;
		if (descriptor.monoClassID == 0xFFFF)
		{
			//Assign the new script index.
			//-> The user will only select one from the descriptor vector,
			//   i.e. the potential future script index is the same across all new ones.
			descriptor.monoClassID = maxScriptIndex + 1;
			descriptor.isNewClassID = true;
		}
		
		unsigned int scriptFileID = 0;
		if (descriptor.monoScriptFileIDRel == 0) scriptFileID = pFile->getFileID();
		else scriptFileID = references[descriptor.monoScriptFileIDRel-1];
		FileContextInfo_ptr pScriptFileAny; 
		if (scriptFileID == 0
			|| !(pScriptFileAny = appContext.getContextInfo(scriptFileID))
			|| pScriptFileAny->getFileContext() == nullptr
			|| pScriptFileAny->getFileContext()->getType() != FileContext_Assets)
			continue;
		AssetsFileContextInfo *pScriptFile = reinterpret_cast<AssetsFileContextInfo*>(pScriptFileAny.get());

		int scriptClassId = pScriptFile->GetClassByName("MonoScript");
		AssetTypeTemplateField *pScriptTemplate = getScriptTemplate(scriptFileID);
		if (pScriptTemplate == nullptr)
			continue;
		AssetIdentifier scriptAsset(std::shared_ptr<AssetsFileContextInfo>(pScriptFileAny, pScriptFile), (pathid_t)descriptor.monoScriptPathID);
		if (!scriptAsset.resolve(appContext))
		{
			assert(false);
			continue;
		}
		IAssetsReader_ptr pReader = scriptAsset.makeReader(appContext);
		if (pReader == nullptr)
		{
			assert(false);
			continue;
		}
		AssetTypeInstance scriptInstance = AssetTypeInstance(1, &pScriptTemplate, scriptAsset.getDataSize(),
			pReader.get(), scriptAsset.isBigEndian());

		AssetTypeValueField *pScriptBase = scriptInstance.GetBaseField();
		AssetTypeValueField *pScriptClassNameField; const char *scriptClassName;
		AssetTypeValueField *pScriptNamespaceField; const char *scriptNamespace;
		AssetTypeValueField *pScriptAssemblyNameField; const char *scriptAssemblyName;
		if ((pScriptBase != NULL) && 
			(pScriptClassNameField = pScriptBase->Get("m_ClassName"))->GetValue()
				&& (scriptClassName = pScriptClassNameField->GetValue()->AsString()) &&
			(pScriptNamespaceField = pScriptBase->Get("m_Namespace"))->GetValue()
				&& (scriptNamespace = pScriptNamespaceField->GetValue()->AsString()))
		{
			AssetTypeValueField *pScriptPropertiesHashField = pScriptBase->Get("m_PropertiesHash");
			if (!pScriptPropertiesHashField->IsDummy() && pScriptPropertiesHashField->GetChildrenCount() == 16)
			{
				for (int j = 0; j < 16; j++)
				{
					AssetTypeValueField *pCurField = pScriptPropertiesHashField->Get(j);
					AssetTypeValue *pCurByte = pCurField->GetValue();
					if (!pCurByte || pCurByte->GetType() != ValueType_UInt8)
					{
						assert(false); //Class database invalid / unsupported here? Not critical.
						descriptor.propertiesHash.qValue[0] = descriptor.propertiesHash.qValue[1] = 0;
						break;
					}
					else
						descriptor.propertiesHash.bValue[j] = (uint8_t)pCurByte->AsUInt();
				}
			}
			std::string scriptDesc(scriptNamespace);
			if (scriptDesc.size() > 0)
				scriptDesc += ".";
			scriptDesc += scriptClassName;
			if ((pScriptAssemblyNameField = pScriptBase->Get("m_AssemblyName"))->GetValue()
				&& (scriptAssemblyName = pScriptAssemblyNameField->GetValue()->AsString()))
			{
				scriptDesc += " (";
				scriptDesc += scriptAssemblyName;
				scriptDesc += ")";
			}
			else
				scriptAssemblyName = "";
			descriptor.scriptDescText = std::move(scriptDesc);
		}
	}
	
	return true;
}

pathid_t AddAssetDialog::getFreePathID(AssetsFileContextInfo *pFile)
{
	pathid_t pathID = 1;
	size_t numAssets = 0;
	for (AssetIterator iter(pFile); !iter.isEnd(); ++iter)
	{
		AssetIdentifier asset;
		iter.get(asset);
		++numAssets;
		if (asset.pathID >= pathID)
			pathID = asset.pathID + 1;
	}
	if (pathID == 0 || pathID == (unsigned long long)UINT_MAX+1)
	{
		std::vector<pathid_t> allPathIDs;
		allPathIDs.reserve(numAssets);
		for (AssetIterator iter(pFile); !iter.isEnd(); ++iter)
		{
			AssetIdentifier asset;
			iter.get(asset);
			allPathIDs.push_back(asset.pathID);
			assert(asset.pathID > 0);
		}
		std::sort(allPathIDs.begin(), allPathIDs.end());
		if (!allPathIDs.empty() && allPathIDs[0] > 1)
			return 1; //Path ID 1 is empty.
		for (size_t i = 1; i < allPathIDs.size(); ++i)
			if (allPathIDs[i] - allPathIDs[i-1] > 1)
				return allPathIDs[i-1] + 1;
	}
	else
		return pathID;
	return 0;
}

INT_PTR CALLBACK AddAssetDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	AddAssetDialog *pThis = (AddAssetDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	static const char *addAsset_MonoBehavMessage 
		= "Additional MonoBehaviour type information can possibly be retrieved in order to generate valid assets.\n"
		"Do you want to do that now? Otherwise, the new asset may be invalid.";
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	INT_PTR ret = (INT_PTR)FALSE;
	switch (message)
	{
	case WM_CLOSE:
	case WM_DESTROY:
		//OnPluginListCancel(pMainWindow, hDlg);
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (AddAssetDialog*)lParam;
			pThis->scriptDescriptors.clear();

			HWND hWndOptions = GetDlgItem(hDlg, IDC_COMBOOPTIONLIST);
			HWND hWndFileId = GetDlgItem(hDlg, IDC_COMBOFILEID);
			HWND hWndPathId = GetDlgItem(hDlg, IDC_EDITPATHID);
			HWND hWndClassId = GetDlgItem(hDlg, IDC_EDITCLASSID);
			HWND hWndMonoClassId = GetDlgItem(hDlg, IDC_EDITMONOCLASSID);
			HWND hWndValidAsset = GetDlgItem(hDlg, IDC_CKVALIDASSET);
			HWND hWndStaticScriptClass = GetDlgItem(hDlg, IDC_STATICSCRIPTCLASS);
			HWND hWndScriptClass = GetDlgItem(hDlg, IDC_COMBOSCRIPTCLASS);

			COMBOBOXINFO scriptClassInfo = {};
			scriptClassInfo.cbSize = sizeof(COMBOBOXINFO);
			if (GetComboBoxInfo(hWndScriptClass, &scriptClassInfo))
			{
				//Allow horizontal scroll in the script combo box.
				SetWindowLong(scriptClassInfo.hwndList, GWL_STYLE, GetWindowLong(scriptClassInfo.hwndList, GWL_STYLE) | WS_HSCROLL);
			}

			ComboBox_AddString(hWndOptions, _T("Custom"));
			ComboBox_AddString(hWndOptions, _T("MonoBehaviour"));
			//Initialize the plugin list using an empty asset interface list (gather the supported plugins).
			//InitPluginList(pMainWindow, hDlg, std::vector<CAssetInterface>(), PluginAction_CREATE);
			ComboBox_SetCurSel(hWndOptions, 0);
			
			ComboBox_ResetContent(hWndFileId);
			int cbFileIdIdx = 0;
			long long int pathId = 1;
			auto &fileEntries = pThis->appContext.getMainWindow().getFileEntries();
			for (auto fileIt = fileEntries.begin(); fileIt != fileEntries.end(); ++fileIt)
			{
				if (fileIt->pContextInfo && 
					fileIt->pContextInfo->getFileContext() && 
					fileIt->pContextInfo->getFileContext()->getType() == FileContext_Assets)
				{
					char referenceFileIDTmp[32];
					sprintf_s(referenceFileIDTmp, "%u - ", fileIt->pContextInfo->getFileID());
					std::string targetName8 = std::string(referenceFileIDTmp) + fileIt->pContextInfo->getFileName();
					auto upTargetNameT = unique_MultiByteToTCHAR(targetName8.c_str());
					ComboBox_AddString(hWndFileId, upTargetNameT.get());
					ComboBox_SetItemData(hWndFileId, cbFileIdIdx, fileIt->pContextInfo->getFileID());
					if (cbFileIdIdx == 0)
					{
						AssetsFileContextInfo *pAssetsInfo = reinterpret_cast<AssetsFileContextInfo*>(fileIt->pContextInfo.get());
						pThis->EnumScriptIndices(pAssetsInfo, pThis->scriptDescriptors);
						pathId = (long long int)pThis->getFreePathID(pAssetsInfo);
					}
					++cbFileIdIdx;
					if (cbFileIdIdx == INT_MAX)
						break;
				}
			}

			ComboBox_ResetContent(hWndScriptClass);
			int cbHorizExtent = 0;
			for (size_t i = 0; i < pThis->scriptDescriptors.size(); ++i)
			{
				size_t descLenT = 0;
				TCHAR *classDescT = _MultiByteToTCHAR(pThis->scriptDescriptors[i].scriptDescText.c_str(), descLenT);
				int idx = ComboBox_AddString(hWndScriptClass, classDescT);
				cbHorizExtent = std::max<int>(cbHorizExtent, GetTextExtent(hWndScriptClass, classDescT));
				if (idx != CB_ERR)
					ComboBox_SetItemData(hWndScriptClass, idx, i);
				_FreeTCHAR(classDescT);
			}
			COMBOBOXINFO comboBoxInfo = {};
			comboBoxInfo.cbSize = sizeof(COMBOBOXINFO);
			if (GetComboBoxInfo(hWndScriptClass, &comboBoxInfo))
				ListBox_SetHorizontalExtent(comboBoxInfo.hwndList, cbHorizExtent);
			ComboBox_SetCurSel(hWndScriptClass, 0);

			ComboBox_SetCurSel(hWndFileId, 0);

			Edit_SetText(hWndClassId, TEXT("0"));
			Edit_SetText(hWndMonoClassId, TEXT("-1"));
			Button_SetCheck(hWndValidAsset, TRUE);
			ShowWindow(hWndStaticScriptClass, SW_HIDE);
			ShowWindow(hWndScriptClass, SW_HIDE);

			TCHAR numTmp[22];
			_stprintf_s(numTmp, TEXT("%lld"), pathId);
			Edit_SetText(hWndPathId, numTmp);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
			case IDC_COMBOFILEID:
				{
					if (wmEvent == CBN_SELCHANGE)
					{
						int curSel = ComboBox_GetCurSel((HWND)lParam);
						if (curSel != CB_ERR)
						{
							unsigned int fileId = (unsigned int)ComboBox_GetItemData((HWND)lParam, curSel);
							HWND hWndOptions = GetDlgItem(hDlg, IDC_COMBOOPTIONLIST);
							int selectedOption = ComboBox_GetCurSel(hWndOptions);

							HWND hWndFileId = GetDlgItem(hDlg, IDC_COMBOFILEID);
							HWND hWndScriptClass = GetDlgItem(hDlg, IDC_COMBOSCRIPTCLASS);
							HWND hWndMonoClassId = GetDlgItem(hDlg, IDC_EDITMONOCLASSID);

							ComboBox_ResetContent(hWndScriptClass);
							FileContextInfo_ptr pContextInfo = pThis->appContext.getContextInfo(fileId);
							if (pContextInfo != nullptr && 
								pContextInfo->getFileContext() && 
								pContextInfo->getFileContext()->getType() == FileContext_Assets)
							{
								AssetsFileContextInfo *pAssetsInfo = reinterpret_cast<AssetsFileContextInfo*>(pContextInfo.get());
								pThis->EnumScriptIndices(pAssetsInfo, pThis->scriptDescriptors);
								int selIdx = ComboBox_SetCurSel(hWndScriptClass, 0);
								if (selectedOption == 1 && selIdx != CB_ERR)
								{
									size_t listIdx = (size_t)ComboBox_GetItemData(hWndScriptClass, selIdx);
									if (pThis->scriptDescriptors.size() > listIdx)
									{
										uint16_t newClassID = pThis->scriptDescriptors[listIdx].monoClassID;
										TCHAR classIDBuf[8]; classIDBuf[7] = 0;
										_stprintf_s(classIDBuf, TEXT("%u"), newClassID);
										Edit_SetText(hWndMonoClassId, classIDBuf);
									}
								}
							}
						}
					}
				}
				break;
			case IDC_COMBOSCRIPTCLASS:
				{
					if (wmEvent == CBN_EDITCHANGE || wmEvent == CBN_EDITUPDATE)
					{
						//Show the dropdown list
						DWORD rangePre = ComboBox_GetEditSel((HWND)lParam);
						uint16_t selStartPre = LOWORD(rangePre);
						
						ComboBox_ShowDropdown((HWND)lParam, TRUE);

						DWORD rangePost = ComboBox_GetEditSel((HWND)lParam);
						uint16_t selEndPost = HIWORD(rangePost);
						ComboBox_SetEditSel((HWND)lParam, selStartPre, selEndPost);

						//Workaround, see https://stackoverflow.com/questions/1093067/why-combobox-hides-cursor-when-droppeddown-is-set
						SendMessage((HWND)lParam, WM_SETCURSOR, 0, 0);
					}
					//if (wmEvent == CBN_SELCHANGE)
					{
						HWND hWndMonoClassId = GetDlgItem(hDlg, IDC_EDITMONOCLASSID);
						HWND hWndOptions = GetDlgItem(hDlg, IDC_COMBOOPTIONLIST);
						int selectedOption = ComboBox_GetCurSel(hWndOptions);

						if (selectedOption == 1)
						{
							unsigned int curScriptClass = (unsigned int)ComboBox_GetCurSel((HWND)lParam);
							size_t listIdx = (curScriptClass != CB_ERR) ? (size_t)ComboBox_GetItemData((HWND)lParam, (int)curScriptClass) : (size_t)-1;
							if (listIdx < pThis->scriptDescriptors.size())
							{
								uint16_t newClassID = pThis->scriptDescriptors[listIdx].monoClassID;
								TCHAR classIDBuf[8]; classIDBuf[7] = 0;
								_stprintf_s(classIDBuf, TEXT("%u"), newClassID);
								Edit_SetText(hWndMonoClassId, classIDBuf);
							}
						}
					}
				}
				break;
			case IDC_COMBOOPTIONLIST:
				{
					if (wmEvent == CBN_SELCHANGE)
					{
						HWND hWndStaticClassId = GetDlgItem(hDlg, IDC_STATICCLASS);
						HWND hWndClassId = GetDlgItem(hDlg, IDC_EDITCLASSID);
						HWND hWndMonoClassId = GetDlgItem(hDlg, IDC_EDITMONOCLASSID);
						HWND hWndValidAsset = GetDlgItem(hDlg, IDC_CKVALIDASSET);

						HWND hWndStaticScriptClass = GetDlgItem(hDlg, IDC_STATICSCRIPTCLASS);
						HWND hWndScriptClass = GetDlgItem(hDlg, IDC_COMBOSCRIPTCLASS);

						int curSel = ComboBox_GetCurSel((HWND)lParam);
						EnableWindow(hWndClassId, (curSel <= 1) ? TRUE : FALSE);
						EnableWindow(hWndMonoClassId, (curSel <= 1) ? TRUE : FALSE);
						if (curSel == 1) Button_SetCheck(hWndValidAsset, BST_CHECKED);
						EnableWindow(hWndValidAsset, (curSel < 1) ? TRUE : FALSE);

						ShowWindow(hWndStaticClassId, (curSel == 1) ? SW_HIDE : SW_SHOW);
						ShowWindow(hWndClassId, (curSel == 1) ? SW_HIDE : SW_SHOW);
						ShowWindow(hWndStaticScriptClass, (curSel == 1) ? SW_SHOW : SW_HIDE);
						ShowWindow(hWndScriptClass, (curSel == 1) ? SW_SHOW : SW_HIDE);

						if (curSel == 1)
						{
							int curScriptClass = ComboBox_GetCurSel(hWndScriptClass);
							size_t listIdx = (curScriptClass != CB_ERR) ? (size_t)ComboBox_GetItemData((HWND)lParam, (int)curScriptClass) : (size_t)-1;
							if (listIdx < pThis->scriptDescriptors.size())
							{
								uint16_t newClassID = pThis->scriptDescriptors[listIdx].monoClassID;
								TCHAR classIDBuf[8]; classIDBuf[7] = 0;
								_stprintf_s(classIDBuf, TEXT("%u"), newClassID);
								Edit_SetText(hWndMonoClassId, classIDBuf);
							}
						}
						else
							Edit_SetText(hWndMonoClassId, TEXT("-1"));
					}
				}
				break;
			case IDOK:
				{
					TCHAR editText[100] = {0};
					wchar_t *editTextEnd = nullptr;
					HWND hWndOptions = GetDlgItem(hDlg, IDC_COMBOOPTIONLIST);
					HWND hWndScriptClass = GetDlgItem(hDlg, IDC_COMBOSCRIPTCLASS);
					HWND hWndFileId = GetDlgItem(hDlg, IDC_COMBOFILEID);
					HWND hWndPathId = GetDlgItem(hDlg, IDC_EDITPATHID);
					HWND hWndClassId = GetDlgItem(hDlg, IDC_EDITCLASSID);
					HWND hWndMonoClassId = GetDlgItem(hDlg, IDC_EDITMONOCLASSID);
					HWND hWndValidAsset = GetDlgItem(hDlg, IDC_CKVALIDASSET);

					int selectedOption = ComboBox_GetCurSel(hWndOptions);

					int selectedFileIndex = ComboBox_GetCurSel(hWndFileId);
					unsigned int fileId = (selectedFileIndex == CB_ERR) ? 0 : (unsigned int)ComboBox_GetItemData(hWndFileId, selectedFileIndex);
					FileContextInfo_ptr pContextInfo = pThis->appContext.getContextInfo(fileId);
					if (pContextInfo == nullptr
						|| pContextInfo->getFileContext() == nullptr
						|| pContextInfo->getFileContext()->getType() != FileContext_Assets)
					{
						MessageBox(hDlg,
							TEXT("Unable to find the selected file!"),
							TEXT("Asset Bundle Extractor"), MB_ICONERROR);
						break;
					}
					AssetsFileContextInfo *pAssetsInfo = reinterpret_cast<AssetsFileContextInfo*>(pContextInfo.get());
					int monoScriptCBIdx = ComboBox_GetCurSel(hWndScriptClass);
					size_t monoScriptIdx = (monoScriptCBIdx != CB_ERR) ? (size_t)ComboBox_GetItemData(hWndScriptClass, (int)monoScriptCBIdx) : (size_t)-1;
					Edit_GetText(hWndPathId, editText, 100);
					*_errno() = 0;
					long long int pathId = _tcstoi64(editText, NULL, 0);
					if (errno == ERANGE)
					{
						*_errno() = 0;
						pathId = (long long int)_tcstoui64(editText, NULL, 0);
					}
					if (errno == ERANGE)
						pathId = 0;
					//long long int pathId = _tcstoi64(editText, NULL, 0);
					Edit_GetText(hWndClassId, editText, 100);
					*_errno() = 0;
					int classId = _tcstol(editText, &editTextEnd, 0);
					if (errno == ERANGE)
					{
						*_errno() = 0;
						classId = (int)_tcstoul(editText, NULL, 0);
					}
					if (errno == ERANGE || editTextEnd == editText)
					{
						classId = 0;
						size_t classnameLenMB = 0;
						auto classnameMB = unique_TCHARToMultiByte(editText, classnameLenMB);
						classId = pAssetsInfo->GetClassByName(classnameMB.get());
						if (classId < 0) //not found
							classId = 0;
					}
					Edit_GetText(hWndMonoClassId, editText, 100);
					*_errno() = 0;
					int monoClassId = _tcstol(editText, NULL, 0);
					if (errno == ERANGE)
					{
						*_errno() = 0;
						monoClassId = (int)_tcstoul(editText, NULL, 0);
					}
					if (errno == ERANGE)
						monoClassId = -1;

					if (selectedOption == 0 && classId < 0 && pAssetsInfo->getAssetsFileContext()->getAssetsFile()->header.format >= 0x10)
						monoClassId = (-classId) - 1; //AssetsFileTable encodes this
					if (selectedOption <= 1)
					{
						unsigned int relFileID_MonoScript = 0;
						long long int pathID_MonoScript = 0;
						Hash128 propertiesHash_MonoScript = {};
						if (selectedOption == 1)
						{
							if (monoScriptIdx < pThis->scriptDescriptors.size())
							{
								relFileID_MonoScript = pThis->scriptDescriptors[monoScriptIdx].monoScriptFileIDRel;
								pathID_MonoScript = pThis->scriptDescriptors[monoScriptIdx].monoScriptPathID;
								propertiesHash_MonoScript = pThis->scriptDescriptors[monoScriptIdx].propertiesHash;
								bool hasExtendedInfoAlready = pThis->scriptDescriptors[monoScriptIdx].extendedTypeInfoPresent;

								FileContextInfo_ptr pMonoScriptFile;
								if (!hasExtendedInfoAlready
									&& (pMonoScriptFile = pThis->appContext.getContextInfo(pAssetsInfo->resolveRelativeFileID(relFileID_MonoScript))) != nullptr
									&& pMonoScriptFile->getFileContext() != nullptr
									&& pMonoScriptFile->getFileContext()->getType() == FileContext_Assets
									&& !reinterpret_cast<AssetsFileContextInfo*>(pMonoScriptFile.get())->hasAnyScriptDatabases())
								{
									AssetsFileContextInfo *pMonoScriptAssetsFile = reinterpret_cast<AssetsFileContextInfo*>(pMonoScriptFile.get());
									switch (MessageBoxA(hDlg, addAsset_MonoBehavMessage, "Add Asset", MB_YESNO))
									{
									case IDYES:
										{
											std::vector<std::shared_ptr<AssetsFileContextInfo>> filesToSearchScripts;
											filesToSearchScripts.push_back(std::shared_ptr<AssetsFileContextInfo>(pMonoScriptFile, pMonoScriptAssetsFile));
											GetAllScriptInformation(pThis->appContext, filesToSearchScripts);
										}
										break;
									case IDNO:
										break;
									}
								}
							}
							classId = -1 - (int)monoClassId;
						}
						bool hasReplacer = false;
						if (Button_GetCheck(hWndValidAsset) == BST_CHECKED)
						{
							//Make a replacer with all fields set to 0.
							AssetsEntryReplacer *pReplacer = MakeEmptyAssetReplacer(
								pThis->appContext, std::shared_ptr<AssetsFileContextInfo>(pContextInfo, pAssetsInfo),
								pathId, classId, monoClassId, relFileID_MonoScript, pathID_MonoScript, propertiesHash_MonoScript);
							if (pReplacer)
							{
								pAssetsInfo->addReplacer(std::shared_ptr<AssetsEntryReplacer>(pReplacer, FreeAssetsReplacer), pThis->appContext);
								hasReplacer = true;
							}
						}
					}
					//else if (fileId < dlg_AssetsFileListLen)
					//{
					//	//Set the asset interface.
					//	std::vector<CAssetInterface> interfaces;
					//	interfaces.assign(1, CAssetInterface(pathId, fileId, "", 0, 0, 0, (uint16_t)-1, nullptr));
					//	OverrideInterfaceList(pMainWindow, interfaces);
					//	//Run the plugin.
					//	RunPluginOption(pMainWindow, hDlg);
					//}
				}
			case IDCANCEL:
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}