#include "stdafx.h"
#include "AssetViewModifyDialog.h"
#include "AsyncTask.h"
#include "resource.h"
#include "ProgressDialog.h"
#include "../libStringConverter/convert.h"
#include "CreateEmptyValueField.h"
#include "MonoBehaviourManager.h"
#include "Win32PluginManager.h"
#include <string>
#include <WindowsX.h>
#include <format>

class AssetInstanceTask : public ITask
{
	std::shared_ptr<AssetViewModifyDialog> pDialog; //Maintain object lifetime.
	std::list<AssetViewModifyDialog::AssetDeserializeDesc>::iterator itDeserializeDesc;

	std::string name;
public:
	AssetInstanceTask(std::shared_ptr<AssetViewModifyDialog> pDialog, std::list<AssetViewModifyDialog::AssetDeserializeDesc>::iterator itDeserializeDesc)
		: pDialog(pDialog), itDeserializeDesc(itDeserializeDesc)
	{
		assert(itDeserializeDesc->asset.pFile != nullptr);
		name = "Deserialize asset : File ID " + std::to_string(static_cast<uint64_t>(itDeserializeDesc->asset.pFile->getFileID())) + 
			", Path ID " + std::to_string(itDeserializeDesc->asset.pathID);
	}
	const std::string &getName()
	{
		return name;
	}
	TaskResult execute(TaskProgressManager &progressManager)
	{
		progressManager.setProgress(0, 0);
		progressManager.setProgressDesc("Generating the type template");
		bool missingScriptTypeInfo = false;
		if (!itDeserializeDesc->asset.pFile->MakeTemplateField(
				&itDeserializeDesc->templateBase, pDialog->appContext, 
				itDeserializeDesc->asset.getClassID(), itDeserializeDesc->asset.getMonoScriptID(), &itDeserializeDesc->asset,
				missingScriptTypeInfo))
		{
			progressManager.logMessage("Unable to generate the type template!");
			return -1;
		}
		uint64_t size = itDeserializeDesc->asset.getDataSize();
		IAssetsReader_ptr pReader = itDeserializeDesc->asset.makeReader();
		if (!pReader)
		{
			progressManager.logMessage("Unable to read the asset!");
			return -2;
		}
		progressManager.setProgressDesc("Deserializing the asset");
		AssetTypeTemplateField *pTemplateBase = &itDeserializeDesc->templateBase;
		std::unique_ptr<AssetTypeInstance> pAssetInstance(
			new AssetTypeInstance(1, &pTemplateBase, size, pReader.get(), itDeserializeDesc->asset.isBigEndian()));
		
		AssetTypeValueField *pInstanceBase = pAssetInstance->GetBaseField();
		if (pInstanceBase)
		{
			itDeserializeDesc->pAssetInstance = std::move(pAssetInstance);
			//Assuming the caller sets a memory fence (e.g. with EnterCriticalSection) before notifying the main thread.
			return missingScriptTypeInfo ? 1 : 0;
		}
		else
		{
			progressManager.logMessage("Unable to deserialize the asset!");
			return -3;
		}
	}
	friend class AssetViewModifyDialog;
};

AssetViewModifyDialog::AssetViewModifyDialog(AssetListDialog &assetListDialog, Win32AppContext &appContext, AssetIdentifier asset, std::string assetName)
	: assetListDialog(assetListDialog), appContext(appContext), assetName(std::move(assetName)),
	hDialog(NULL), isDestroyed(false), registeredCallbackCounter(0), ignoreExpandNotifications(false),
	hCurPopupMenu(NULL), hCurEditPopup(NULL), hCurEditPopupUpDown(NULL), hEditPopupItem(NULL), pEditValueField(nullptr), pEditAssetDesc(nullptr),
	testItemHasTextcallbackActive(false), testItemHasTextcallbackResult(false)
{
	assert(asset.pFile);
	assert(asset.pReplacer || asset.pAssetInfo);

	findOrPrepareLoad(std::move(asset), MC_TLI_ROOT, nullptr);
}
AssetViewModifyDialog::~AssetViewModifyDialog()
{
	if (this->hDialog)
	{
		SendMessage(this->hDialog, WM_CLOSE, 0, 0);
		//SetWindowLongPtr(this->hDialog, GWLP_USERDATA, 0);
	}
}
HWND AssetViewModifyDialog::_getTreeHandle(HWND hDialog)
{
	return GetDlgItem(hDialog, IDC_TYPETREE);
}

static void setTreeValueText(HWND hTree, MC_HTREELISTITEM hItem, AssetTypeValueField *pField)
{
	TCHAR sprntTmp[64]; sprntTmp[0] = 0;
	TCHAR *convertedText = nullptr;
	const TCHAR *text = nullptr;
	if (AssetTypeValue *pValue = pField->GetValue())
	{
		switch (pValue->GetType())
		{
			case ValueType_Bool		:
				text = (pValue->AsBool() ? TEXT("true") : TEXT("false"));
				break;
			case ValueType_Int8		:
			case ValueType_UInt8	:
			case ValueType_Int16	:
			case ValueType_UInt16	:
			case ValueType_Int32	:
				_stprintf_s(sprntTmp, TEXT("%d"), pValue->AsInt());
				text = sprntTmp;
				break;
			case ValueType_UInt32	:
				_stprintf_s(sprntTmp, TEXT("%u"), pValue->AsUInt());
				text = sprntTmp;
				break;
			case ValueType_Int64	:
				_stprintf_s(sprntTmp, TEXT("%lld"), pValue->AsInt64());
				text = sprntTmp;
				break;
			case ValueType_UInt64	:
				_stprintf_s(sprntTmp, TEXT("%llu"), pValue->AsUInt64());
				text = sprntTmp;
				break;
			case ValueType_Float	:
				_stprintf_s(sprntTmp, TEXT("%f"), pValue->AsFloat());
				text = sprntTmp;
				break;
			case ValueType_Double	:
				_stprintf_s(sprntTmp, TEXT("%f"), pValue->AsDouble());
				text = sprntTmp;
				break;
			case ValueType_String	:
				if (pValue->AsString())
				{
					std::string valueString = std::string("\"") + pValue->AsString() + "\"";
					size_t sizeTmp;
					convertedText = _MultiByteToTCHAR(valueString.c_str(), sizeTmp);
					text = convertedText;
				}
				break;
			case ValueType_Array	:
				_stprintf_s(sprntTmp, TEXT("[%u]"), pValue->AsArray()->size);
				text = sprntTmp;
				break;
			case ValueType_ByteArray:
				_stprintf_s(sprntTmp, TEXT("[%u]"), pValue->AsByteArray()->size);
				text = sprntTmp;
				break;
			default:
				break;
		}
	}
	if (text != nullptr)
	{
		MC_TLSUBITEM subItem;
		subItem.fMask = MC_TLSIF_TEXT;
		subItem.iSubItem = 1;
		subItem.cchTextMax = 0;
		subItem.pszText = const_cast<TCHAR*>(text);
		SendMessage(hTree, MC_TLM_SETSUBITEM, reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(&subItem));
	}
	if (convertedText)
	{
		_FreeTCHAR(convertedText);
	}
}

MC_HTREELISTITEM AssetViewModifyDialog::addTreeItems(AssetDeserializeDesc *pAssetDesc, MC_HTREELISTITEM hParent, LPARAM parentLParam, 
	AssetTypeValueField **pFields, size_t fieldCount, bool isPPtr, size_t startIdx)
{
	HWND hTree = GetDlgItem(this->hDialog, IDC_TYPETREE);
	if (startIdx > 0 && SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CHILD, (LPARAM)hParent) == NULL)
		startIdx = 0; //Was never expanded.

	bool parentIsArray = false;
	auto arrayMappingIt = pAssetDesc->arrayMappingsByArray.end();
	if (AssetTypeValueField *pParentField = reinterpret_cast<AssetTypeValueField*>(parentLParam))
	{
		if (pParentField->GetValue() && pParentField->GetValue()->GetType() == ValueType_Array)
		{
			parentIsArray = true;
			auto insertResult = pAssetDesc->arrayMappingsByArray.insert(std::make_pair(pParentField, AssetDeserializeDesc::ArrayMappings()));
			arrayMappingIt = insertResult.first;
			assert(fieldCount <= pParentField->GetValue()->AsArray()->size);
			arrayMappingIt->second.treeItems.resize(pParentField->GetValue()->AsArray()->size);
			if (insertResult.second)
			{
				arrayMappingIt->second.itemToIndexMap.rehash(static_cast<size_t>(std::ceil(
					static_cast<double>(pParentField->GetValue()->AsArray()->size) / arrayMappingIt->second.itemToIndexMap.max_load_factor())));
			}
		}
	}

	std::shared_ptr<CProgressIndicator> pProgressIndicator = std::make_shared<CProgressIndicator>(this->appContext.getMainWindow().getHInstance());
	if (pProgressIndicator->Start(hDialog, pProgressIndicator, 2000))
	{
		size_t nTotal = fieldCount - startIdx;
		pProgressIndicator->SetStepRange(0, (nTotal > INT_MAX) ? INT_MAX : static_cast<unsigned int>(nTotal));
		pProgressIndicator->SetCancellable(true);
		pProgressIndicator->SetTitle("Adding fields to the View");
	}
	else
	{
		pProgressIndicator.reset();
	}
	
	SendMessage(hTree, WM_SETREDRAW, FALSE, 0);

	MC_HTREELISTITEM hLastItem = NULL;
	
	MC_TLINSERTSTRUCT insert;
	insert.hParent = hParent;
	insert.hInsertAfter = MC_TLI_LAST;
	insert.item.fMask = MC_TLIF_CHILDREN | MC_TLIF_PARAM | MC_TLIF_TEXT;
	insert.item.cchTextMax = 0;
	for (size_t i = startIdx; i < fieldCount; i++)
	{
		if (pProgressIndicator && pProgressIndicator->IsCancelled())
			break;
		if (pProgressIndicator && i <= INT_MAX && (i % 101) == 0)
		{
			TCHAR descTmp[64];
			_stprintf_s(descTmp, TEXT("Adding field %llu/%llu"), 
				static_cast<unsigned long long>(i - startIdx), static_cast<unsigned long long>(fieldCount - startIdx));
			pProgressIndicator->SetDescription(descTmp);
			pProgressIndicator->SetStepStatus(static_cast<unsigned int>(i - startIdx));
		}
		MC_HTREELISTITEM hCurParent = hParent;
		if (parentIsArray)
		{
			insert.hParent = hParent;
			insert.item.cChildren = 1; 
			insert.item.lParam = 0;
			//TCHAR sprntTmp[64]; sprntTmp[0] = 0;
			//_stprintf_s(sprntTmp, TEXT("[%llu]"), (unsigned long long)i);
			//insert.item.pszText = sprntTmp;
			insert.item.pszText = MC_LPSTR_TEXTCALLBACK;
			hCurParent = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&insert)));
			assert(hCurParent != NULL);
			if (hCurParent == NULL)
				break;
			arrayMappingIt->second.treeItems[i] = hCurParent;
			arrayMappingIt->second.itemToIndexMap[hCurParent] = static_cast<uint32_t>(i);
		}
		insert.hParent = hCurParent;
		insert.item.cChildren = (pFields[i]->GetChildrenCount() > 0) ? 1 : 0; 
		insert.item.lParam = reinterpret_cast<LPARAM>(pFields[i]);
		auto pTypeNameT = unique_MultiByteToTCHAR(pFields[i]->GetType().c_str());
		auto pFieldNameT = unique_MultiByteToTCHAR(pFields[i]->GetName().c_str());
		std::basic_string<TCHAR> fullName = std::basic_string<TCHAR>(pTypeNameT.get()) + TEXT(" ") + pFieldNameT.get();
		//Requires a const_cast, since the item structure can also be used for text retrieval.
		//Should be safe, since the text is only read for MC_TLM_INSERTITEM.
		insert.item.pszText = const_cast<TCHAR*>(fullName.c_str());
		MC_HTREELISTITEM hItem = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&insert)));
		assert(hItem != NULL);
		if (hItem == NULL)
			break;
		hLastItem = hItem;
		setTreeValueText(hTree, hItem, pFields[i]);
	}
	if (isPPtr)
	{
		insert.item.cChildren = 1; 
		//Use a null parameter for special items.
		// => The parent item is the PPtr. Retrieve via MC_TLM_GETNEXTITEM with MC_TLGN_PARENT.
		insert.item.lParam = 0; 
		//Requires a const_cast, since the item structure can also be used for text retrieval.
		//Should be safe, since the text is only read for MC_TLM_INSERTITEM.
		insert.item.pszText = const_cast<TCHAR*>(TEXT("View asset"));
		MC_HTREELISTITEM hItem = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&insert)));
	}
	SendMessage(hTree, WM_SETREDRAW, TRUE, 0);
	if (pProgressIndicator != nullptr)
	{
		pProgressIndicator->End();
		pProgressIndicator->Free();
	}
	return hLastItem;
}

void AssetViewModifyDialog::onCompletionMainThread(uintptr_t param1, uintptr_t param2)
{
	AssetViewModifyDialog *pThis = reinterpret_cast<AssetViewModifyDialog*>(param1);
	auto ppInstanceTask = reinterpret_cast<std::shared_ptr<AssetInstanceTask>*>(param2);
	if (--pThis->registeredCallbackCounter == 0)
		pThis->appContext.taskManager.removeCallback(pThis);
	AssetDeserializeDesc *pAssetDesc = &*(*ppInstanceTask)->itDeserializeDesc;
	pAssetDesc->pLoadTask = nullptr;
	assert(pAssetDesc->pParent != nullptr || pAssetDesc->parentItem == MC_TLI_ROOT);
	bool closeAsset = false;
	bool reloadAsset = false;
	if (pAssetDesc->pendingClose)
	{
		//Remove this asset from the list, as the load task has now finished.
		//Invalidates pAssetDesc
		closeAsset = true;
	}
	else if (pAssetDesc->pAssetInstance == nullptr
		|| pAssetDesc->pAssetInstance->GetBaseField()->IsDummy())
	{
		if (!pThis->isDestroyed && pThis->hDialog != NULL)
		{
			const TCHAR *errorMessage = nullptr;
			switch (pAssetDesc->loadTaskResult)
			{
				case TaskResult_Canceled:
					break;
				case -1:
					errorMessage = TEXT("Could not retrieve the type information for the asset view!");
					break;
				case -2:
					errorMessage = TEXT("Unable to read the asset for viewing!");
					break;
				case -3:
				default:
					errorMessage = TEXT("Unable to deserialize the asset for viewing!");
					break;
			}
			if (errorMessage != nullptr)
				MessageBox(pThis->hDialog, errorMessage, TEXT("Asset Bundle Extractor"), MB_ICONERROR);
		}
		closeAsset = true;
	}
	else if (pAssetDesc->loadTaskResult == 1  //i.e. is missing MonoBehaviour type information
		&& !pAssetDesc->monoBehaviourInfoAsked)
	{
		AssetTypeValueField *pBaseField = pAssetDesc->pAssetInstance->GetBaseField();
		AssetTypeValueField *pFileIDField = pBaseField->Get("m_Script")->Get("m_FileID");
		std::vector<std::shared_ptr<AssetsFileContextInfo>> typeAssets = { pAssetDesc->asset.pFile };
		bool foundScriptFile = false;
		if (!pFileIDField->IsDummy() && pFileIDField->GetValue() != nullptr && pFileIDField->GetValue()->GetType() == ValueType_Int32)
		{
			unsigned int absScriptFileID = pAssetDesc->asset.pFile->resolveRelativeFileID((unsigned int)pFileIDField->GetValue()->AsInt());
			std::shared_ptr<AssetsFileContextInfo> scriptDefFileInfo
				= std::dynamic_pointer_cast<AssetsFileContextInfo>(pThis->appContext.getContextInfo(absScriptFileID));
			if (scriptDefFileInfo != nullptr)
			{
				foundScriptFile = true;
				if (scriptDefFileInfo->getFileID() != pAssetDesc->asset.pFile->getFileID())
					typeAssets.push_back(std::move(scriptDefFileInfo));
			}
		}
		if (foundScriptFile)
		{
			switch (MessageBox(pThis->hDialog,
				TEXT("Class information needs to be extracted to show the complete asset data, and to not break the changed asset when saving.\n")
				TEXT("Do you want to do this now?\n")
				TEXT("Note: This currently does not work with il2cpp game builds."),
				TEXT("Asset Bundle Extractor"), MB_ICONWARNING | MB_YESNOCANCEL))
			{
			case IDYES:
				pAssetDesc->monoBehaviourInfoAsked = true;
				if (GetAllScriptInformation(pThis->appContext, typeAssets))
					reloadAsset = true;
				else
					closeAsset = true;
				break;
			case IDNO:
				break;
			case IDCANCEL:
			default:
				closeAsset = true;
				break;
			}
		}
	}
	if (closeAsset)
	{
		pThis->loadedAssets.erase((*ppInstanceTask)->itDeserializeDesc);
		if (!pThis->isDestroyed && pThis->loadedAssets.size() == 1)
		{
			assert(&pThis->loadedAssets.front() == pAssetDesc);
			pThis->assetListDialog.removeModifyDialog(pThis);
			pThis->isDestroyed = true;
		}
	}
	else if (reloadAsset)
	{
		pAssetDesc->pLoadTask = nullptr;
		pAssetDesc->pAssetInstance.reset();
		pThis->startLoadTask((*ppInstanceTask)->itDeserializeDesc, (*ppInstanceTask)->pDialog);
	}
	else
	{
		assert(pAssetDesc->pAssetInstance != nullptr);
		AssetTypeValueField *pBaseField = pAssetDesc->pAssetInstance->GetBaseField();
		MC_HTREELISTITEM hBaseItem = pThis->addTreeItems(pAssetDesc, pAssetDesc->parentItem, 0, &pBaseField, 1, false);
		pAssetDesc->baseItem = hBaseItem;
		pThis->loadedAssetsByBaseItem.insert(std::make_pair(hBaseItem, (*ppInstanceTask)->itDeserializeDesc));
	}
	delete ppInstanceTask;
}
typedef void(*CallbackProc)(uintptr_t,uintptr_t);

void AssetViewModifyDialog::OnCompletion(std::shared_ptr<ITask>& pTask, TaskResult result)
{
	//Not necessarily run from the main thread.
	if (std::shared_ptr<AssetInstanceTask> pInstanceTask = std::dynamic_pointer_cast<AssetInstanceTask>(pTask))
	{
		if (pInstanceTask->pDialog.get() != this)
			return;
		assert(pInstanceTask->itDeserializeDesc->pLoadTask == pTask.get());
		pInstanceTask->itDeserializeDesc->loadTaskResult = result;

		CallbackProc callback = onCompletionMainThread;
		appContext.signalMainThread(AppContextMsg_DoMainThreadCallback,
			new std::tuple<void(*)(uintptr_t, uintptr_t), uintptr_t, uintptr_t>(
				callback,
				reinterpret_cast<uintptr_t>(this),
				reinterpret_cast<uintptr_t>(new std::shared_ptr<AssetInstanceTask>(pInstanceTask))));
	}
}
bool AssetViewModifyDialog::init(std::shared_ptr<AssetViewModifyDialog> &selfPtr, HWND hParentWnd)
{
	assert(selfPtr.get() == this);
	assert(this->loadedAssets.size() == 1);
	assert(this->loadedAssets.front().pLoadTask == nullptr &&
		this->loadedAssets.front().pAssetInstance == nullptr);
	if (this->loadedAssets.size() != 1)
		return false;
	return startLoadTask(this->loadedAssets.begin(), selfPtr);
}
std::list<AssetViewModifyDialog::AssetDeserializeDesc>::iterator 
	AssetViewModifyDialog::findOrPrepareLoad(AssetIdentifier asset, MC_HTREELISTITEM parentItem, AssetDeserializeDesc *pParentAsset,
		AssetTypeValueField *pPPtrField)
{
	if (!asset.resolve(appContext))
		return loadedAssets.end();
	unsigned int fileID = asset.pFile->getFileID();
	pathid_t pathID = asset.pathID;

	auto mapIt = loadedAssetsByPPtr.find(AssetAbsPPtr(fileID, pathID));
	if (mapIt != loadedAssetsByPPtr.end())
		return mapIt->second;

	auto assetListEntryIt = loadedAssets.insert(loadedAssets.end(), AssetDeserializeDesc());
	assetListEntryIt->asset = std::move(asset);
	assetListEntryIt->parentItem = parentItem;
	assetListEntryIt->pParent = pParentAsset;
	if (pParentAsset != nullptr)
	{
		assert(pPPtrField != nullptr);
		pParentAsset->children.push_back(std::make_pair(pPPtrField, assetListEntryIt));
	}
	else
		assert(parentItem == MC_TLI_ROOT);

	loadedAssetsByPPtr.emplace(std::make_pair(AssetAbsPPtr(fileID, pathID), assetListEntryIt));
	if (pPPtrField != nullptr)
		loadedAssetsByPPtrField.emplace(std::make_pair(pPPtrField, assetListEntryIt));
	return assetListEntryIt;
}
bool AssetViewModifyDialog::startLoadTask(std::list<AssetDeserializeDesc>::iterator assetEntry, std::shared_ptr<AssetViewModifyDialog> selfPtr)
{
	if (assetEntry->pLoadTask != nullptr || assetEntry->pAssetInstance)
		return true;
	if (this->isDestroyed)
		return false;
	if (!selfPtr)
	{
		selfPtr = std::static_pointer_cast<AssetViewModifyDialog,AssetModifyDialog>(this->assetListDialog.getModifyDialogRef(this));
		if (!selfPtr)
			return false;
	}

	std::shared_ptr<AssetInstanceTask> pLoadTask = std::make_shared<AssetInstanceTask>(std::move(selfPtr), assetEntry);
	assetEntry->pLoadTask = pLoadTask.get();

	if (this->registeredCallbackCounter++ == 0)
		appContext.taskManager.addCallback(this);
	if (appContext.taskManager.enqueue(pLoadTask))
		return true;
	if (--this->registeredCallbackCounter == 0)
		appContext.taskManager.removeCallback(this);

	assetEntry->pLoadTask = nullptr;
	return false;
}
AssetViewModifyDialog::AssetDeserializeDesc *AssetViewModifyDialog::getAssetDescForItem(MC_HTREELISTITEM item)
{
	HWND hTree = GetDlgItem(this->hDialog, IDC_TYPETREE);
	do {
		auto mapIt = this->loadedAssetsByBaseItem.find(item);
		if (mapIt != this->loadedAssetsByBaseItem.end())
			return &*mapIt->second;
		item = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_PARENT, reinterpret_cast<LPARAM>(item)));
	} while (item != NULL);
	return nullptr;
}
//Called when the user requests to close the tab.
//Returns true if there are unsaved changes, false otherwise.
//If the function will return true and applyable is not null,
// *applyable will be set to true iff applyNow() is assumed to succeed without further interaction
// (e.g. all fields in the dialog have a valid value, ...).
//The caller uses this info to decide whether and how it should display a confirmation dialog before proceeding.
bool AssetViewModifyDialog::hasUnappliedChanges(bool *applyable)
{
	bool ret = false;
	for (auto assetIt = this->loadedAssets.begin(); assetIt != this->loadedAssets.end(); ++assetIt)
	{
		if (assetIt->hasChanged && assetIt->pAssetInstance != nullptr
			&& assetIt->pAssetInstance->GetBaseField(0) != nullptr
			&& assetIt->asset.pFile != nullptr)
		{
			ret = true;
			break;
		}
	}
	if (ret && applyable) *applyable = true;
	return ret;
}
template<class ForwardIt>
bool AssetViewModifyDialog::applyChangesIn(ForwardIt assetBegin, ForwardIt assetEnd)
{
	//Check for conflicting changes, e.g. if the user changed the same asset in a different tab without closing this one.
	bool hasConflictingChanges = false;
	for (auto assetIt = assetBegin; assetIt != assetEnd; ++assetIt)
	{
		AssetDeserializeDesc &assetDesc = *assetIt;
		if (assetDesc.hasChanged && assetDesc.pAssetInstance != nullptr
			&& assetDesc.pAssetInstance->GetBaseField(0) != nullptr
			&& assetDesc.asset.pFile != nullptr)
		{
			std::shared_ptr<AssetsReplacer> pCurrentReplacer = assetDesc.asset.pFile->getReplacer(assetDesc.asset.pathID);
			if (pCurrentReplacer.get() != assetDesc.asset.pReplacer.get())
			{
				hasConflictingChanges = true;
				break;
			}
		}
	}
	if (hasConflictingChanges)
	{
		HWND hParent = this->hDialog;
		if (hParent == NULL)
			hParent = this->appContext.getMainWindow().getWindow();
		std::string message = std::string("Applying changes in the tab <") + this->getTabName() + 
			std::string("> will overwrite existing changes.\n Do you want to proceed anyway?");
		auto messageT = unique_MultiByteToTCHAR(message.c_str());
		switch (MessageBox(hParent, messageT.get(), TEXT("Asset Bundle Extractor"), MB_YESNO))
		{
		case IDYES:
			break;
		case IDNO:
			return false;
		}
	}
	for (auto assetIt = assetBegin; assetIt != assetEnd; ++assetIt)
	{
		AssetDeserializeDesc &assetDesc = *assetIt;
		if (assetDesc.hasChanged && assetDesc.pAssetInstance != nullptr
			&& assetDesc.pAssetInstance->GetBaseField(0) != nullptr
			&& assetDesc.asset.pFile != nullptr)
		{
			IAssetsWriterToMemory *pWriter = Create_AssetsWriterToMemory();
			QWORD size = assetDesc.pAssetInstance->GetBaseField(0)->Write(pWriter, 0, assetDesc.asset.isBigEndian());
			size_t bufferSize = 0;
			void *buffer = nullptr;
			if (pWriter->GetBuffer(buffer, bufferSize))
			{
				AssetsEntryReplacer *pReplacer = MakeAssetModifierFromMemory(
					assetDesc.asset.pFile->getFileID(), assetDesc.asset.pathID, 
					assetDesc.asset.getClassID(), assetDesc.asset.getMonoScriptID(), 
					buffer, bufferSize, Free_AssetsWriterToMemory_DynBuf);
				assetDesc.asset.pReplacer.reset(pReplacer);
				assetDesc.asset.pFile->addReplacer(assetDesc.asset.pReplacer, this->appContext);
				assetDesc.hasChanged = false;
				assert(size == bufferSize);
			}
			else
				assert(false);
		}
	}
	return true;
}
//std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>>
//Called when the user requests to apply the changes (e.g. selecting Apply, Save or Save All in the menu).
//Returns whether the changes have been applied;
// if true, the caller may continue closing the AssetModifyDialog.
// if false, the caller may stop closing the AssetModifyDialog.
//Note: applyChanges() is expected to notify the user about errors (e.g. via MessageBox).
bool AssetViewModifyDialog::applyChanges()
{
	return this->applyChangesIn(this->loadedAssets.begin(), this->loadedAssets.end());
}
std::string AssetViewModifyDialog::getTabName()
{
	if (this->assetName.empty())
	{
		if (this->loadedAssets.size() >= 1)
		{
			AssetIdentifier &asset = this->loadedAssets.front().asset;
			return std::format("View Asset (FileID {}, PathID {})",
				static_cast<uint64_t>(asset.pFile->getFileID()), std::to_string(asset.pathID));
		}
		return "View Asset";
	}
	if (this->loadedAssets.size() >= 1)
	{
		AssetIdentifier& asset = this->loadedAssets.front().asset;
		return std::format("View Asset \"{}\" (FileID {}, PathID {})", this->assetName,
			static_cast<uint64_t>(asset.pFile->getFileID()), std::to_string(asset.pathID));
	}
	return "View Asset \"" + this->assetName + "\"";
}
HWND AssetViewModifyDialog::getWindowHandle()
{
	return hDialog;
}
//Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
bool AssetViewModifyDialog::onCommand(WPARAM wParam, LPARAM lParam)
{
	int wmId = LOWORD(wParam);
	return false;
}
void AssetViewModifyDialog::onHotkey(ULONG message, DWORD keyCode) //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
{
	
}
void AssetViewModifyDialog::onShow(HWND hParentWnd)
{
	if (!this->hDialog)
	{
		this->hDialog = CreateDialogParam(appContext.getMainWindow().getHInstance(), MAKEINTRESOURCE(IDD_ASSETVIEW), hParentWnd, AssetViewProc, (LPARAM)this);
	}
	else
	{
		SetParent(this->hDialog, hParentWnd);
		ShowWindow(this->hDialog, SW_SHOW);
	}
}
//Called when the dialog is to be hidden, either because of a tab switch or while closing the tab.
void AssetViewModifyDialog::onHide()
{
	if (this->hDialog)
	{
		ShowWindow(this->hDialog, SW_HIDE);
		SetParent(this->hDialog, NULL);
		//SendMessage(this->hDialog, WM_CLOSE, 0, 0);
	}
}
//Called when the tab is about to be destroyed.
void AssetViewModifyDialog::onDestroy()
{
	this->isDestroyed = true;
}

INT_PTR CALLBACK AssetViewModifyDialog::AssetViewProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret = (INT_PTR)FALSE;
	AssetViewModifyDialog *pThis = (AssetViewModifyDialog*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	switch (message)
	{
	case WM_CLOSE:
		if (pThis)
			pThis->hDialog = NULL;
		DestroyWindow(hDlg);
		ret = (INT_PTR)TRUE;
		break;
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			pThis = (AssetViewModifyDialog*)lParam;
			pThis->testItemHasTextcallbackActive = false;
			{
				HWND hTree = GetDlgItem(hDlg, IDC_TYPETREE);
				MC_TLCOLUMN col;
				col.fMask = MC_TLCF_TEXT | MC_TLCF_WIDTH;
				col.pszText = const_cast<TCHAR*>(_T("Field"));
				col.cx = 540;
				col.cchTextMax = static_cast<int>(_tcslen(col.pszText)) + 1;
				SendMessage(hTree, MC_TLM_INSERTCOLUMN, 0, (LPARAM)&col);
				col.pszText = const_cast<TCHAR*>(_T("Value"));
				col.cchTextMax = static_cast<int>(_tcslen(col.pszText)) + 1;
				col.cx = 160;
				SendMessage(hTree, MC_TLM_INSERTCOLUMN, 1, (LPARAM)&col);

				SetWindowSubclass(hTree, AssetTreeListSubclassProc, 0, reinterpret_cast<DWORD_PTR>(pThis));
			}
			PostMessage(hDlg, WM_SIZE, 0, 0);
		}
		break;
	case WM_SIZE:
		{
			RECT client = {};
			GetClientRect(hDlg, &client);
			LONG clientWidth = client.right-client.left;
			LONG clientHeight = client.bottom-client.top;
			SetWindowPos(GetDlgItem(hDlg, IDC_TYPETREE), HWND_TOP, 5, 5, clientWidth - 10, clientHeight - 10,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		}
		break;
	case WM_DRAWITEM:
		{

		}
		break;
	case WM_CONTEXTMENU:
		if (reinterpret_cast<HWND>(wParam) == GetDlgItem(hDlg, IDC_TYPETREE))
		{
			HWND hTree = reinterpret_cast<HWND>(wParam);
			POINT screenClickPos;
			screenClickPos.x = GET_X_LPARAM(lParam); //Screen area
			screenClickPos.y = GET_Y_LPARAM(lParam);
			bool foundHitItem = false;
			MC_TLHITTESTINFO hitTestInfo = {};
			hitTestInfo.pt.x = screenClickPos.x;
			hitTestInfo.pt.y = screenClickPos.y;
			if (screenClickPos.x == -1 && screenClickPos.y == -1)
			{
				hitTestInfo.hItem = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CARET, NULL));
				hitTestInfo.iSubItem = 0;
				if (hitTestInfo.hItem != NULL)
				{	
					RECT itemRect = {};
					itemRect.left = MC_TLIR_SELECTBOUNDS;
					if (SendMessage(hTree, MC_TLM_GETITEMRECT, reinterpret_cast<WPARAM>(hitTestInfo.hItem), reinterpret_cast<LPARAM>(&itemRect)))
					{
						screenClickPos.x = itemRect.left + (itemRect.right - itemRect.left) / 2;
						screenClickPos.y = itemRect.top + (itemRect.bottom - itemRect.top) / 2;
						if (ClientToScreen(hTree, &screenClickPos))
							foundHitItem = true;
					}
				}
			}
			else
			{
				if (ScreenToClient(hTree, &hitTestInfo.pt) &&
					SendMessage(hTree, MC_TLM_HITTEST, 0, reinterpret_cast<LPARAM>(&hitTestInfo)) != NULL)
				{
					//Select this item.
					MC_TLITEM item;
					item.fMask = MC_TLIF_STATE;
					item.state = MC_TLIS_SELECTED;
					item.stateMask = MC_TLIS_SELECTED;
					SendMessage(hTree, MC_TLM_SETITEM,  
						reinterpret_cast<WPARAM>(hitTestInfo.hItem),
						reinterpret_cast<LPARAM>(&item));
					foundHitItem = true;
				}
			}
			if (foundHitItem)
			{
				assert(hitTestInfo.hItem != NULL);
				if (pThis->hCurEditPopup != NULL)
					pThis->doCloseEditPopup();

				MC_TLITEM item;
				item.fMask = MC_TLIF_PARAM;
				item.lParam = 0;
				if (!SendMessage(hTree, MC_TLM_GETITEM, 
						reinterpret_cast<WPARAM>(hitTestInfo.hItem),
						reinterpret_cast<LPARAM>(&item)))
					break;
				if (item.lParam == 0 || hitTestInfo.hItem == (MC_HTREELISTITEM)SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_ROOT, NULL))
				{
					//if (hitTestInfo.iSubItem != 0)
					//	break;
					//Array index PPtr [View asset] node or base node
					MC_HTREELISTITEM hParentItem = reinterpret_cast<MC_HTREELISTITEM>(
						SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_PARENT,
							reinterpret_cast<LPARAM>(hitTestInfo.hItem)));
					item.fMask = MC_TLIF_PARAM;
					item.lParam = NULL;
					if ((hParentItem != NULL) && (!SendMessage(hTree, MC_TLM_GETITEM,
							reinterpret_cast<WPARAM>(hParentItem),
							reinterpret_cast<LPARAM>(&item))
							|| item.lParam == NULL))
						break;
					AssetTypeValueField *pParentValueField = reinterpret_cast<AssetTypeValueField*>(item.lParam);
					AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(hParentItem ? hParentItem : hitTestInfo.hItem);
					if (!pAssetDesc)
						break;
					bool isRootItem = (hParentItem == NULL);

					pThis->openContextMenuPopup(hTree, screenClickPos,
						isRootItem ? MC_TLI_ROOT : hitTestInfo.hItem, nullptr,
						hParentItem, pParentValueField, 
						pAssetDesc);
					break;
				}
				else
				{
					//Normal nodes with a linked AssetTypeValueField.
					AssetTypeValueField *pValueField = reinterpret_cast<AssetTypeValueField*>(item.lParam);
					AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(hitTestInfo.hItem);
					if (!pAssetDesc)
						break;
					
					pThis->openContextMenuPopup(hTree, screenClickPos,
						hitTestInfo.hItem, pValueField, 
						NULL, nullptr, //Don't retrieve the parent information (since it is not needed)
						pAssetDesc);
				}
			}
		}
		break;
	case WM_NOTIFY:
		{
			NMHDR *pNotifyHeader = reinterpret_cast<NMHDR*>(lParam);
			switch (pNotifyHeader->code)
			{
				case MC_TLN_GETDISPINFO:
					if (pNotifyHeader->hwndFrom == GetDlgItem(hDlg, IDC_TYPETREE) && !pThis->ignoreExpandNotifications)
					{
						MC_NMTLDISPINFO *pNotify = reinterpret_cast<MC_NMTLDISPINFO*>(lParam);
						if (pThis->testItemHasTextcallbackActive)
							pThis->testItemHasTextcallbackResult = (pNotify->item.fMask & MC_TLIF_TEXT) ? true : false;
						assert((pNotify->item.fMask & ~MC_TLIF_TEXT) == 0);
						if (pNotify->item.fMask & MC_TLIF_TEXT)
						{
							MC_HTREELISTITEM hArrayItem = reinterpret_cast<MC_HTREELISTITEM>(
								SendMessage(pNotifyHeader->hwndFrom, MC_TLM_GETNEXTITEM, MC_TLGN_PARENT, reinterpret_cast<LPARAM>(pNotify->hItem)));
							if (hArrayItem == NULL)
								break;
							MC_TLITEM item;
							item.fMask = MC_TLIF_PARAM;
							item.lParam = 0;
							if (!SendMessage(pNotifyHeader->hwndFrom, MC_TLM_GETITEM, 
								reinterpret_cast<WPARAM>(hArrayItem),
								reinterpret_cast<LPARAM>(&item)) || item.lParam == NULL)
								break;
							AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(hArrayItem);
							if (pAssetDesc == nullptr)
								break;
							auto mappingIt = pAssetDesc->arrayMappingsByArray.find(reinterpret_cast<AssetTypeValueField*>(item.lParam));
							if (mappingIt == pAssetDesc->arrayMappingsByArray.end())
								break;
							auto indexMapIt = mappingIt->second.itemToIndexMap.find(pNotify->hItem);
							if (indexMapIt == mappingIt->second.itemToIndexMap.end())
								break;
							
							//Get the text of an array index item.
							_stprintf_s(pThis->itemTextcallbackBuf, TEXT("[%u]"), indexMapIt->second);
							pNotify->item.pszText = pThis->itemTextcallbackBuf;
						}
					}
					break;
				case MC_TLN_EXPANDED:
					if (pNotifyHeader->hwndFrom == GetDlgItem(hDlg, IDC_TYPETREE) && !pThis->ignoreExpandNotifications)
					{
						MC_NMTREELIST *pNotify = reinterpret_cast<MC_NMTREELIST*>(lParam);
						if (pNotify->action == MC_TLE_EXPAND)
						{
							//Check if the child tree items already exist
							if (SendMessage(pNotify->hdr.hwndFrom, MC_TLM_GETNEXTITEM, MC_TLGN_CHILD, reinterpret_cast<LPARAM>(pNotify->hItemNew)) != NULL)
								break;
							if (pNotify->lParamNew == 0)
							{
								if (pThis->testItemHasTextcallback(pNotify->hdr.hwndFrom, pNotify->hItemNew))
								{
									//Currently, only array index items use MC_LPSTR_TEXTCALLBACK.
									//Array index items should always have a child in the tree list.
									assert(false);
									break;
								}
								//Special case - PPtr "View asset" node
								MC_HTREELISTITEM hPPtrNode = reinterpret_cast<MC_HTREELISTITEM>(
									SendMessage(pNotify->hdr.hwndFrom, MC_TLM_GETNEXTITEM, MC_TLGN_PARENT, reinterpret_cast<LPARAM>(pNotify->hItemNew)));
								assert(hPPtrNode != NULL);
								if (hPPtrNode == NULL)
									break;
								//Find the asset behind this PPtr.
								AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(hPPtrNode);
								if (pAssetDesc == nullptr)
									break;
								MC_TLITEM item;
								item.fMask = MC_TLIF_PARAM;
								item.lParam = 0;
								SendMessage(pNotify->hdr.hwndFrom, MC_TLM_GETITEM, 
									reinterpret_cast<WPARAM>(hPPtrNode),
									reinterpret_cast<LPARAM>(&item));
								if (AssetTypeValueField *pPPtrField = reinterpret_cast<AssetTypeValueField*>(item.lParam))
								{
									//Retrieve the relative File/Path ID.
									AssetTypeValueField *pFileIDField = pPPtrField->Get("m_FileID");
									AssetTypeValueField *pPathIDField = pPPtrField->Get("m_PathID");
									if (pFileIDField->GetValue() != nullptr && pPathIDField->GetValue() != nullptr)
									{
										//Initialize the AssetDeserializeDesc structure and add it to the list,
										// or find an existing structure for the target asset.
										auto newAssetDescIt = pThis->findOrPrepareLoad(AssetIdentifier(
												pAssetDesc->asset.pFile, //Reference source is the file containing the referring asset
												pFileIDField->GetValue()->AsUInt(), //Relative File ID for the referred asset
												pPathIDField->GetValue()->AsUInt64()),
											pNotify->hItemNew, pAssetDesc, pPPtrField);
										if (newAssetDescIt == pThis->loadedAssets.end())
										{
											MessageBox(hDlg, TEXT("Unable to resolve the target asset."), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
											break;
										}
										if (newAssetDescIt->pLoadTask == nullptr && !newAssetDescIt->pAssetInstance)
										{
											//Enqueue a task to load the asset.
											pThis->startLoadTask(newAssetDescIt);
										}
										else
										{
											//findOrPrepareLoad returned an existing asset that is either loading or has loaded already.
											MC_HTREELISTITEM itemToFocus = newAssetDescIt->parentItem;
											if (itemToFocus == MC_TLI_ROOT)
												itemToFocus = reinterpret_cast<MC_HTREELISTITEM>(
													SendMessage(pNotify->hdr.hwndFrom, MC_TLM_GETNEXTITEM, MC_TLGN_ROOT, NULL));
											if (itemToFocus == NULL)
												break;
											//Collapse the just expanded item again.
											SendMessage(pNotify->hdr.hwndFrom, MC_TLM_EXPAND, MC_TLE_COLLAPSE, reinterpret_cast<LPARAM>(pNotify->hItemNew));
											//Select and go to an existing "View asset" node for the target asset.
											item.fMask = MC_TLIF_STATE;
											item.state = MC_TLIS_SELECTED;
											item.stateMask = MC_TLIS_SELECTED;
											SendMessage(pNotify->hdr.hwndFrom, MC_TLM_SETITEM, 
												reinterpret_cast<WPARAM>(itemToFocus),
												reinterpret_cast<LPARAM>(&item));
											SendMessage(pNotify->hdr.hwndFrom, MC_TLM_ENSUREVISIBLE, 0, reinterpret_cast<LPARAM>(itemToFocus));
										}
									}
								}
							}
							else
							{
								AssetTypeValueField *pField = reinterpret_cast<AssetTypeValueField*>(pNotify->lParamNew);

								//Find the asset.
								AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(pNotify->hItemNew);
								if (pAssetDesc == nullptr)
									break;

								bool isPPtr = false;
								const char *typeName = pField->GetType().c_str();
								if (typeName && !strncmp(typeName, "PPtr<", 5))
									isPPtr = true;
								pThis->ignoreExpandNotifications = true;
								//Collapse the just expanded item again to prevent scrollbar redraw.
								SendMessage(pNotify->hdr.hwndFrom, MC_TLM_EXPAND, MC_TLE_COLLAPSE, reinterpret_cast<LPARAM>(pNotify->hItemNew));
								
								pThis->addTreeItems(pAssetDesc, pNotify->hItemNew, pNotify->lParamNew, pField->GetChildrenList(), pField->GetChildrenCount(), isPPtr);

								//Expand the item to redraw.
								SendMessage(pNotify->hdr.hwndFrom, MC_TLM_EXPAND, MC_TLE_EXPAND, reinterpret_cast<LPARAM>(pNotify->hItemNew));
								pThis->ignoreExpandNotifications = false;
							}
						}
					}
					break;
				//case UDN_DELTAPOS:
				//	break;
			}
		}
		break;
	}
	return ret;
}
bool AssetViewModifyDialog::testItemHasTextcallback(HWND hTree, MC_HTREELISTITEM hItem)
{
	MC_TLITEM item;
	item.fMask = MC_TLIF_TEXT;
	item.pszText = nullptr;
	item.cchTextMax = 0;
	this->testItemHasTextcallbackResult = false;
	this->testItemHasTextcallbackActive = true;
	SendMessage(hTree, MC_TLM_GETITEM, 
		reinterpret_cast<WPARAM>(hItem),
		reinterpret_cast<LPARAM>(&item));
	this->testItemHasTextcallbackActive = false;
	return this->testItemHasTextcallbackResult;
}
static void FreeFieldRecursively(AssetTypeValueField *pField, std::unordered_set<uint8_t*> &allocatedMemory)
{
	if (pField->GetValue() && pField->GetValue()->GetType() == ValueType_String)
	{
		auto memoryEntryIt = allocatedMemory.find(reinterpret_cast<uint8_t*>(pField->GetValue()->AsString()));
		if (memoryEntryIt != allocatedMemory.end())
		{
			delete[] *memoryEntryIt;
			allocatedMemory.erase(memoryEntryIt);
		}
	}
	for (DWORD i = 0; i < pField->GetChildrenCount(); i++)
	{
		FreeFieldRecursively(pField->Get(i), allocatedMemory);
	}
	AssetTypeValueField **pChildList = pField->GetChildrenList();
	auto fieldMemoryEntryIt = allocatedMemory.find(reinterpret_cast<uint8_t*>(pField));
	if (fieldMemoryEntryIt != allocatedMemory.end())
	{
		delete[] *fieldMemoryEntryIt;
		allocatedMemory.erase(fieldMemoryEntryIt);
	}
	auto childListMemoryEntryIt = allocatedMemory.find(reinterpret_cast<uint8_t*>(pChildList));
	if (childListMemoryEntryIt != allocatedMemory.end())
	{
		delete[] *childListMemoryEntryIt;
		allocatedMemory.erase(childListMemoryEntryIt);
	}
}
std::vector<std::pair<AssetTypeValueField*, std::list<AssetViewModifyDialog::AssetDeserializeDesc>::iterator>> 
AssetViewModifyDialog::findAllSubtreeAssets(
	std::list<AssetDeserializeDesc>::iterator itBaseAssetDesc,
	bool &hasUnappliedChanges,
	AssetTypeValueField *pBasePPtrField)
{
	if (pBasePPtrField == nullptr && itBaseAssetDesc->pParent != nullptr)
	{
		bool foundSelf = false;
		for (size_t k = 0; k < itBaseAssetDesc->pParent->children.size(); k++)
		{
			if (&*itBaseAssetDesc->pParent->children[k].second == &*itBaseAssetDesc)
			{
				pBasePPtrField = itBaseAssetDesc->pParent->children[k].first;
				foundSelf = true;
				break;
			}
		}
		assert(foundSelf);
	}
	//Find all child assets of the asset.
	hasUnappliedChanges = false;
	std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>> 
		recursiveChildren(1, std::make_pair(pBasePPtrField, itBaseAssetDesc));
	std::vector<size_t> idxStack(1, 0); //Initialize with {0}
	AssetDeserializeDesc *pCurDesc = &*itBaseAssetDesc;
	do {
		assert(pCurDesc != nullptr);
		size_t i = idxStack.back(); 
		idxStack.pop_back();
		if (pCurDesc->hasChanged)
			hasUnappliedChanges = true;
		if (i < pCurDesc->children.size())
		{
			assert(pCurDesc->children[i].second->pParent == pCurDesc);
			idxStack.push_back(i + 1);
			idxStack.push_back(0);
			recursiveChildren.push_back(pCurDesc->children[i]);
			pCurDesc = &*pCurDesc->children[i].second;
		}
		else
			pCurDesc = pCurDesc->pParent;
	} while (!idxStack.empty());
	return recursiveChildren;
}
void AssetViewModifyDialog::closeAllSubtreeAssets(HWND hTree,
	std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>> subtreeAssets,
	AssetDeserializeDesc *pParentAsset)
{
	assert(pParentAsset->pLoadTask == nullptr);
	for (size_t i = 0; i < subtreeAssets.size(); i++)
	{
		assert(subtreeAssets[i].second->pParent != nullptr);
		if (subtreeAssets[i].second->pParent == pParentAsset)
		{
			//Delete the asset and its children from the tree list.
			SendMessage(hTree, MC_TLM_DELETEITEM, MC_TLDI_NONOTIFY, reinterpret_cast<LPARAM>(subtreeAssets[i].second->baseItem));
			assert(subtreeAssets[i].second->pLoadTask == nullptr);
			bool foundSelf = false;
			for (size_t k = 0; k < subtreeAssets[i].second->pParent->children.size(); k++)
			{
				if (&*subtreeAssets[i].second->pParent->children[k].second == &*subtreeAssets[i].second)
				{
					//Delete this asset from its parent's child list.
					subtreeAssets[i].second->pParent->children.erase(subtreeAssets[i].second->pParent->children.begin() + k);
					foundSelf = true;
					break;
				}
			}
			assert(foundSelf);
		}
	}
	//Delete this asset from internal lists and maps. 
	for (size_t i = 0; i < subtreeAssets.size(); i++)
	{
		std::list<AssetDeserializeDesc>::iterator itCurAsset = subtreeAssets[i].second;
		auto curAssetByBaseItemIt = this->loadedAssetsByBaseItem.find(itCurAsset->baseItem);
		if (curAssetByBaseItemIt != this->loadedAssetsByBaseItem.end())
		{
			assert(itCurAsset->pLoadTask == nullptr);
			assert(&*curAssetByBaseItemIt->second == &*itCurAsset);
			this->loadedAssetsByBaseItem.erase(curAssetByBaseItemIt);
		}
		else
			assert(itCurAsset->pLoadTask != nullptr);

		auto curAssetByPPtrIt = this->loadedAssetsByPPtr.find(
			AssetAbsPPtr(itCurAsset->asset.pFile->getFileID(), 
				itCurAsset->asset.pathID));
		assert(curAssetByPPtrIt != this->loadedAssetsByPPtr.end());
		if (curAssetByPPtrIt != this->loadedAssetsByPPtr.end())
		{
			assert(&*curAssetByPPtrIt->second == &*itCurAsset);
			this->loadedAssetsByPPtr.erase(curAssetByPPtrIt);
		}

		if (subtreeAssets[i].first != nullptr)
		{
			auto curAssetByPPtrFieldIt = this->loadedAssetsByPPtrField.find(subtreeAssets[i].first);
			assert(curAssetByPPtrFieldIt != this->loadedAssetsByPPtrField.end());
			if (curAssetByPPtrFieldIt != this->loadedAssetsByPPtrField.end())
			{
				assert(&*curAssetByPPtrFieldIt->second == &*itCurAsset);
				this->loadedAssetsByPPtrField.erase(curAssetByPPtrFieldIt);
			}
		}

		if (itCurAsset->pLoadTask != nullptr)
		{
			//The asset will be deleted once the task is finished.
			itCurAsset->pendingClose = true;
			itCurAsset->pParent = nullptr;
		}
		else
		{
			//Delete the asset.
			this->loadedAssets.erase(itCurAsset);
		}
	}
}
void AssetViewModifyDialog::doCloseEditPopup()
{
	bool updateValueText = false;
	bool addTreeListChildren = false;
	size_t firstChildToAdd = 0;
	union {
		uint64_t oldValue64u;
		int64_t oldValue64s;
	};
	bool hasOldIntValue = false;
	HWND hTree = GetDlgItem(this->hDialog, IDC_TYPETREE);
	if (this->hCurEditPopupUpDown != NULL)
	{
		BOOL gotValue = FALSE;
		int value = static_cast<int>(SendMessage(this->hCurEditPopupUpDown, UDM_GETPOS32, 0, reinterpret_cast<LPARAM>(&gotValue)));
		if (this->pEditValueField != nullptr && this->pEditValueField->GetValue() != nullptr)
		{
			switch (this->pEditValueField->GetValue()->GetType())
			{
			case ValueType_Int8:
			case ValueType_UInt8:
			case ValueType_Int16:
			case ValueType_UInt16:
				assert(this->iEditPopupSubItem == 1);
				if (this->iEditPopupSubItem != 1)
					break;
				oldValue64s = this->pEditValueField->GetValue()->AsInt();
				hasOldIntValue = true;
				if (value != this->pEditValueField->GetValue()->AsInt())
				{
					this->pEditValueField->GetValue()->Set(&value);
					updateValueText = true;
				}
				break;
			case ValueType_Array:
				if (this->iEditPopupSubItem == 0 && value >= 0 && static_cast<DWORD>(value) < this->pEditValueField->GetValue()->AsArray()->size)
				{
					//Move array element.
					this->moveArrayItem(hTree, this->pEditAssetDesc, this->pEditValueField, this->hEditPopupItem, static_cast<uint32_t>(value));
				}
				else if (this->iEditPopupSubItem == 1 && value > 0 && static_cast<DWORD>(value) > this->pEditValueField->GetValue()->AsArray()->size)
				{
					//Create one or several array elements.
					DWORD oldSize = this->pEditValueField->GetValue()->AsArray()->size;
					if (this->pEditValueField->GetTemplateField() == nullptr
						|| !this->pEditValueField->GetTemplateField()->isArray
						|| this->pEditValueField->GetTemplateField()->children.size() != 2)
					{
						MessageBox(this->hDialog, TEXT("Invalid array type information."), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
						break;
					}
					AssetTypeTemplateField *pArrayDataTemplate = &this->pEditValueField->GetTemplateField()->children[1];
					AssetTypeValueField **oldChildrenList = this->pEditValueField->GetChildrenList();
					std::vector<std::unique_ptr<uint8_t[]>> newAllocatedMemory;
					//Allocate the new, larger child list.
					bool failed = false;
					uint8_t *newChildrenListMemory = new uint8_t[sizeof(AssetTypeValueField*) * value];
					AssetTypeValueField **newChildrenList = reinterpret_cast<AssetTypeValueField**>(newChildrenListMemory);
					newAllocatedMemory.push_back(std::unique_ptr<uint8_t[]>(newChildrenListMemory));
					//Copy the old info to the new list.
					memcpy(newChildrenList, oldChildrenList, sizeof(AssetTypeValueField*) * oldSize);
					for (DWORD i = oldSize; i < static_cast<DWORD>(value); i++)
					{
						//Generate new empty array elements.
						newChildrenList[i] = CreateEmptyValueFieldFromTemplate(pArrayDataTemplate, newAllocatedMemory);
						if (newChildrenList[i] == nullptr)
						{
							failed = true;
							break;
						}
					}
					if (failed)
					{
						MessageBox(this->hDialog, TEXT("Failed to create new array elements."), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
						break;
					}
					//Update the child list.
					this->pEditValueField->SetChildrenList(newChildrenList, static_cast<DWORD>(value));
					updateValueText = true;
					addTreeListChildren = true;
					firstChildToAdd = this->pEditValueField->GetValue()->AsArray()->size;
					this->pEditValueField->GetValue()->AsArray()->size = static_cast<DWORD>(value);
					//Free the old child list memory (if possible).
					auto oldListIt = this->pEditAssetDesc->instanceModificationBuffers.find(reinterpret_cast<uint8_t*>(oldChildrenList));
					if (oldListIt != this->pEditAssetDesc->instanceModificationBuffers.end())
					{
						delete[] *oldListIt;
						this->pEditAssetDesc->instanceModificationBuffers.erase(oldListIt);
					}
					//Add all new allocated memory to the set.
					for (std::unique_ptr<uint8_t[]>& pMem : newAllocatedMemory)
						this->pEditAssetDesc->instanceModificationBuffers.insert(pMem.release());
					//Insert the new array elements to the tree list control.
					addTreeListChildren = true;
					firstChildToAdd = oldSize;
				}
				break;
			}
		}
	}
	else if (this->hCurEditPopup != NULL)
	{
		//Retrieve the value from the edit.
		int textLen = Edit_GetTextLength(this->hCurEditPopup);
		if (textLen >= 0 && textLen < INT_MAX)
		{
			std::unique_ptr<TCHAR[]> textBuf(new TCHAR[textLen + 1]);
			int nCharacters = Edit_GetText(this->hCurEditPopup, textBuf.get(), textLen + 1);
			if (nCharacters <= textLen)
				textBuf[nCharacters] = 0;
			else
				textBuf[textLen] = 0;
			TCHAR *endPtr = nullptr;
			switch (this->pEditValueField->GetValue()->GetType())
			{
			case ValueType_Int32:
			case ValueType_Int64:
				{
					assert(this->iEditPopupSubItem == 1);
					if (this->iEditPopupSubItem != 1)
						break;
					int64_t value = _tcstoi64(textBuf.get(), &endPtr, 0);
					if (endPtr == textBuf.get() || endPtr == nullptr)
						break;
					oldValue64s = this->pEditValueField->GetValue()->AsInt64();
					hasOldIntValue = true;
					if (value != this->pEditValueField->GetValue()->AsInt64())
					{
						this->pEditValueField->GetValue()->Set(&value);
						updateValueText = true;
					}
				}
				break;
			case ValueType_UInt32:
			case ValueType_UInt64:
				{
					assert(this->iEditPopupSubItem == 1);
					if (this->iEditPopupSubItem != 1)
						break;
					uint64_t value = _tcstoui64(textBuf.get(), &endPtr, 0);
					if (endPtr == textBuf.get() || endPtr == nullptr)
						break;
					oldValue64u = this->pEditValueField->GetValue()->AsUInt64();
					hasOldIntValue = true;
					if (value != this->pEditValueField->GetValue()->AsUInt64())
					{
						this->pEditValueField->GetValue()->Set(&value);
						updateValueText = true;
					}
				}
				break;
			case ValueType_Float:
				{
					assert(this->iEditPopupSubItem == 1);
					if (this->iEditPopupSubItem != 1)
						break;
					double valueD = _tcstod(textBuf.get(), &endPtr);
					if (endPtr == textBuf.get() || endPtr == nullptr)
						break;
					float value = static_cast<float>(valueD);
					if (value != this->pEditValueField->GetValue()->AsFloat())
					{
						this->pEditValueField->GetValue()->Set(&value);
						updateValueText = true;
					}
				}
				break;
			case ValueType_Double:
				{
					assert(this->iEditPopupSubItem == 1);
					if (this->iEditPopupSubItem != 1)
						break;
					double value = _tcstod(textBuf.get(), &endPtr);
					if (endPtr == textBuf.get() || endPtr == nullptr)
						break;
					if (value != this->pEditValueField->GetValue()->AsDouble())
					{
						this->pEditValueField->GetValue()->Set(&value);
						updateValueText = true;
					}
				}
				break;
			case ValueType_String:
				{
					assert(this->iEditPopupSubItem == 1);
					if (this->iEditPopupSubItem != 1)
						break;
					size_t newLen = 0;
					auto pNewUTF8 = unique_TCHARToMultiByte(textBuf.get(), newLen);
					char *oldString = this->pEditValueField->GetValue()->AsString();
					if ((oldString == nullptr && newLen == 0)
						|| !strcmp(oldString, pNewUTF8.get()))
						break;
					//Update the string value in the field, and manage the memory allocations.
					setStringValue(this->pEditValueField, this->pEditAssetDesc, pNewUTF8.get(), newLen);
					updateValueText = true;
				}
				break;
			}
		}
	}
	if (updateValueText
			&& (this->pEditValueField->GetName() == "m_FileID"
				|| this->pEditValueField->GetName() == "m_PathID")
			&& hasOldIntValue
		)
	{
		//Find the [view asset] tree item, delete its subtree (if it exists) and the corresponding one or several AssetDeserializeDesc*
		HWND hTree = GetDlgItem(this->hDialog, IDC_TYPETREE);
		MC_HTREELISTITEM hPPtrBaseItem = reinterpret_cast<MC_HTREELISTITEM>(
			SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_PARENT, reinterpret_cast<LPARAM>(this->hEditPopupItem)));
		AssetTypeValueField *pPPtrBaseField = nullptr;
		if (hPPtrBaseItem != NULL)
		{
			MC_TLITEM item;
			item.fMask = MC_TLIF_PARAM;
			item.lParam = 0;
			if (SendMessage(hTree, MC_TLM_GETITEM, reinterpret_cast<WPARAM>(hPPtrBaseItem), reinterpret_cast<LPARAM>(&item)))
			{
				pPPtrBaseField = reinterpret_cast<AssetTypeValueField*>(item.lParam);
			}
		}
		MC_HTREELISTITEM hCurItem = this->hEditPopupItem;
		MC_HTREELISTITEM hChildItem = NULL;
		auto assetIt = this->loadedAssetsByBaseItem.end();
		while ((hCurItem = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_NEXT, reinterpret_cast<LPARAM>(hCurItem))))
				!= NULL)
		{
			hChildItem = reinterpret_cast<MC_HTREELISTITEM>(SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CHILD, reinterpret_cast<LPARAM>(hCurItem)));
			if (hChildItem != NULL)
			{
				assetIt = this->loadedAssetsByBaseItem.find(hChildItem);
				if (assetIt != this->loadedAssetsByBaseItem.end())
				{
					assert(assetIt->second->pParent == this->pEditAssetDesc);
					if (assetIt->second->pParent == this->pEditAssetDesc)
						break;
				}
			}
		}
		if (assetIt != this->loadedAssetsByBaseItem.end() && pPPtrBaseField != nullptr)
		{
			bool hasUnappliedChanges;
			auto recursiveChildren = this->findAllSubtreeAssets(assetIt->second, hasUnappliedChanges, pPPtrBaseField);
			bool cancelledDelete = false;
			if (hasUnappliedChanges)
			{
				switch (MessageBox(this->hDialog, TEXT("There are unsaved changes in assets below the changed PPtr.\n")
					TEXT("Do you want save these changes before proceeding?"), TEXT("Asset Bundle Extractor"), MB_ICONWARNING | MB_YESNOCANCEL))
				{
				case IDYES:
					assert(false); //TODO: Save changes
				case IDNO:
					break;
				case IDCANCEL:
					this->pEditValueField->GetValue()->Set(&oldValue64u);
					updateValueText = false;
					cancelledDelete = true;
					break;
				}
			}
			if (!cancelledDelete)
			{
				this->closeAllSubtreeAssets(hTree, recursiveChildren, this->pEditAssetDesc);

				SendMessage(hTree, MC_TLM_EXPAND, MC_TLE_COLLAPSE, reinterpret_cast<LPARAM>(hCurItem));
				MC_TLITEM item;
				item.fMask = MC_TLIF_CHILDREN;
				item.cChildren = 1;
				SendMessage(hTree, MC_TLM_SETITEM, reinterpret_cast<WPARAM>(hCurItem), reinterpret_cast<LPARAM>(&item));
			}
		}
	}
	if (updateValueText)
	{
		setTreeValueText(hTree, this->hEditPopupItem, this->pEditValueField);
		pEditAssetDesc->hasChanged = true;
		if (addTreeListChildren)
		{
			//Add new children of pEditValueField to the tree list, starting with firstChildToAdd.
			this->addTreeItems(this->pEditAssetDesc, this->hEditPopupItem, reinterpret_cast<LPARAM>(this->pEditValueField),
				this->pEditValueField->GetChildrenList(), this->pEditValueField->GetChildrenCount(), false, firstChildToAdd);
			//Keep in mind that the user may have cancelled adding tree elements before (and with the new items).
			// => The tree list may have gaps.
			//    Additional functionality, such as moving array entries, must be able to handle that.
		}
	}
	if (this->hCurPopupMenu != NULL)
	{
		DestroyMenu(this->hCurPopupMenu);
		this->hCurPopupMenu = NULL;
	}
	if (this->hCurEditPopupUpDown != NULL)
	{
		DestroyWindow(this->hCurEditPopupUpDown);
		this->hCurEditPopupUpDown = NULL;
	}
	if (this->hCurEditPopup != NULL)
	{
		DestroyWindow(this->hCurEditPopup);
		this->hCurEditPopup = NULL;
	}
	this->pEditAssetDesc = nullptr;
	this->pEditValueField = nullptr;
	this->hEditPopupItem = NULL;
}
bool AssetViewModifyDialog::setByteArrayValue(FieldInfo fieldInfo, std::unique_ptr<uint8_t[]> data, size_t data_len)
{
	if (fieldInfo.pValueField == nullptr
		|| fieldInfo.pValueField->GetValue() == nullptr
		|| data_len > std::numeric_limits<uint32_t>::max())
		return false;
	auto mapIt = loadedAssetsByPPtr.find(fieldInfo.assetIDs);
	if (mapIt == loadedAssetsByPPtr.end())
		return false;
	AssetDeserializeDesc* pAssetDesc = &(*mapIt->second);

	AssetTypeByteArray* pOldByteArray = fieldInfo.pValueField->GetValue()->AsByteArray();
	uint8_t* pOldByteArrayData = pOldByteArray ? pOldByteArray->data : nullptr;
	AssetTypeByteArray input;
	input.data = data.release();
	input.size = (uint32_t)data_len;
	(*fieldInfo.pValueField->GetValue()) = AssetTypeValue(ValueType_ByteArray, &input);
	fieldInfo.pValueField->SetChildrenList(nullptr, 0);

	pAssetDesc->hasChanged = true;

	if (pOldByteArrayData != nullptr)
	{
		//Free the old string memory, if possible.
		auto oldDataMemIt = pAssetDesc->instanceModificationBuffers.find(pOldByteArrayData);
		if (oldDataMemIt != pAssetDesc->instanceModificationBuffers.end())
		{
			delete[] *oldDataMemIt;
			pAssetDesc->instanceModificationBuffers.erase(oldDataMemIt);
		}
	}
	//Add the new memory to the set.
	pAssetDesc->instanceModificationBuffers.insert(input.data);

	HWND hTree = getTreeHandle();
	if (hTree == NULL)
		return true; //There is no need to update the tree if it doesn't exist.

	//Delete all child items (ByteArrays are shown without any child items in the tree list).
	MC_HTREELISTITEM childItem = reinterpret_cast<MC_HTREELISTITEM>(
			SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CHILD, reinterpret_cast<LPARAM>(fieldInfo.treeListHandle)));
	while (childItem != NULL)
	{
		MC_HTREELISTITEM nextChildItem = reinterpret_cast<MC_HTREELISTITEM>(
			SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_NEXT, reinterpret_cast<LPARAM>(childItem)));
		SendMessage(hTree, MC_TLM_DELETEITEM, MC_TLDI_NONOTIFY, reinterpret_cast<LPARAM>(childItem));
		childItem = nextChildItem;
	}
	//Mark the new ByteArray item as having no children.
	MC_TLITEM tlItem = {};
	tlItem.fMask = MC_TLIF_CHILDREN;
	tlItem.cChildren = 0;
	SendMessage(hTree, MC_TLM_SETITEM, reinterpret_cast<WPARAM>(fieldInfo.treeListHandle), reinterpret_cast<LPARAM>(&tlItem));

	return true;
}
bool AssetViewModifyDialog::setStringValue(AssetTypeValueField* pField, AssetDeserializeDesc* pAssetDesc, const char* str, size_t str_len)
{
	if (pField->GetValue() == nullptr || pField->GetValue()->GetType() != ValueType_String)
		return false;
	char* oldString = pField->GetValue()->AsString();
	uint8_t* newStringValue = new uint8_t[(str_len + 1) * sizeof(str[0])];
	memcpy(newStringValue, str, (str_len + 1) * sizeof(str[0]));
	pField->GetValue()->Set((char*)newStringValue, ValueType_String);
	pAssetDesc->hasChanged = true;

	if (oldString != nullptr)
	{
		//Free the old string memory, if possible.
		auto oldStringMemIt = pAssetDesc->instanceModificationBuffers.find(reinterpret_cast<uint8_t*>(oldString));
		if (oldStringMemIt != pAssetDesc->instanceModificationBuffers.end())
		{
			delete[] *oldStringMemIt;
			pAssetDesc->instanceModificationBuffers.erase(oldStringMemIt);
		}
	}
	//Add the new memory to the set.
	pAssetDesc->instanceModificationBuffers.insert(newStringValue);
	return true;
}
bool AssetViewModifyDialog::setStringValue(AssetTypeValueField* pValueField, AssetAbsPPtr assetIDs, const char* str, size_t str_len)
{
	auto mapIt = loadedAssetsByPPtr.find(assetIDs);
	if (mapIt == loadedAssetsByPPtr.end())
		return false;
	return setStringValue(pValueField, &(*mapIt->second), str, str_len);
}
AssetViewModifyDialog::FieldInfo AssetViewModifyDialog::getNextChildFieldInfo(FieldInfo parentFieldInfo, FieldInfo prevFieldInfo)
{
	if (parentFieldInfo.pValueField == nullptr)
		return FieldInfo(); //Make sure we don't accidentally follow a PPtr.
	if (prevFieldInfo.pValueField == nullptr && parentFieldInfo.pValueField->GetValue() != nullptr)
	{
		if (parentFieldInfo.pValueField->GetValue()->GetType() == ValueType_Array
			|| parentFieldInfo.pValueField->GetValue()->GetType() == ValueType_ByteArray)
			return FieldInfo(); //Not supported (yet).
	}
	HWND hTree = getTreeHandle();
	MC_HTREELISTITEM childItem = NULL;
	if (prevFieldInfo.treeListHandle == NULL)
	{
		childItem = reinterpret_cast<MC_HTREELISTITEM>(
			SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_CHILD, reinterpret_cast<LPARAM>(parentFieldInfo.treeListHandle)));
	}
	else
	{
		childItem = reinterpret_cast<MC_HTREELISTITEM>(
			SendMessage(hTree, MC_TLM_GETNEXTITEM, MC_TLGN_NEXT, reinterpret_cast<LPARAM>(prevFieldInfo.treeListHandle)));
	}
	if (childItem == NULL) return FieldInfo();
	AssetTypeValueField* pChildValueField = nullptr;
	MC_TLITEM item;
	item.fMask = MC_TLIF_PARAM;
	item.lParam = 0;
	if (SendMessage(hTree, MC_TLM_GETITEM, reinterpret_cast<WPARAM>(childItem), reinterpret_cast<LPARAM>(&item)))
	{
		pChildValueField = reinterpret_cast<AssetTypeValueField*>(item.lParam);
	}
	return FieldInfo(pChildValueField, parentFieldInfo.assetIDs, childItem);
}
void AssetViewModifyDialog::updateValueFieldText(FieldInfo fieldInfo, bool markAsChanged)
{
	if (markAsChanged)
	{
		auto mapIt = loadedAssetsByPPtr.find(fieldInfo.assetIDs);
		if (mapIt != loadedAssetsByPPtr.end())
			mapIt->second->hasChanged = true;
	}
	if (fieldInfo.treeListHandle == NULL)
		return;
	HWND hTree = GetDlgItem(this->hDialog, IDC_TYPETREE);
	if (fieldInfo.pValueField != nullptr)
		setTreeValueText(hTree, fieldInfo.treeListHandle, fieldInfo.pValueField);

	FieldInfo curChildField = FieldInfo();
	while ((curChildField = getNextChildFieldInfo(fieldInfo, curChildField)).treeListHandle != NULL)
	{
		updateValueFieldText(curChildField);
	}
}
void AssetViewModifyDialog::openEditPopup(HWND hTree, MC_HTREELISTITEM hItem, AssetTypeValueField *pValueField, AssetDeserializeDesc *pAssetDesc,
	int iSubItem)
{	
	RECT targetRect = {};
	targetRect.top = iSubItem;
	targetRect.left = MC_TLIR_BOUNDS;
	if (iSubItem == 0)
	{
		targetRect.left = MC_TLIR_LABEL;
		if (!SendMessage(hTree, MC_TLM_GETITEMRECT, 
			reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(&targetRect)))
			return;
		RECT subRect = {};
		subRect.top = 1;
		subRect.left = MC_TLIR_BOUNDS;
		if (!SendMessage(hTree, MC_TLM_GETSUBITEMRECT, 
			reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(&subRect)))
			return;
		targetRect.right = subRect.left;
	}
	else
	{
		if (!SendMessage(hTree, MC_TLM_GETSUBITEMRECT, 
				reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(&targetRect)))
			return;
	}

	int limitLow = 0;
	int limitHigh = 65535;
	this->pEditAssetDesc = pAssetDesc;
	this->pEditValueField = pValueField;
	this->hEditPopupItem = hItem;
	this->iEditPopupSubItem = iSubItem;
	switch (pValueField->GetValue()->GetType())
	{
	case ValueType_Bool:
		{
			assert(iSubItem == 1);
			if (iSubItem != 1)
				break;
			//Swap immediately.
			bool newValue = !pValueField->GetValue()->AsBool();
			pValueField->GetValue()->Set(&newValue);
			pAssetDesc->hasChanged = true;

			MC_TLSUBITEM subItem;
			subItem.fMask = MC_TLSIF_TEXT;
			subItem.iSubItem = 1;
			subItem.cchTextMax = 0;
			subItem.pszText = const_cast<TCHAR*>(newValue ? TEXT("true") : TEXT("false"));
			SendMessage(hTree, MC_TLM_SETSUBITEM, reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(&subItem));
		}
		break;
	case ValueType_Int8: limitHigh >>= 1; limitLow -= 127;   //For Int8:  limitHigh = 65535 >> 9 = 127;   limitLow = -127 + 32768 - 32768 = -127
	case ValueType_UInt8: limitHigh >>= 7; limitLow += 32768;//For UInt8: limitHigh = 65535 >> 8 = 255;   limitLow = 32768 - 32768 = 0
	case ValueType_Int16: limitHigh >>= 1; limitLow -= 32768;//For Int16: limitHigh = 65535 >> 1 = 32767; limitLow = -32768
	case ValueType_UInt16:
		{
			assert(iSubItem == 1);
			if (iSubItem != 1)
				break;
			this->hCurEditPopup = CreateWindowEx(WS_EX_CLIENTEDGE,
				WC_EDIT, NULL, ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_LEFT, 
				targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			this->hCurEditPopupUpDown = CreateWindowEx(WS_EX_LTRREADING,
				UPDOWN_CLASS, NULL, WS_CHILD | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 
				0, 0, 0, 0, // Auto size the Up-Down Control
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			SendMessage(this->hCurEditPopupUpDown, UDM_SETRANGE32, static_cast<INT_PTR>(limitLow), static_cast<INT_PTR>(limitHigh));
			SendMessage(this->hCurEditPopupUpDown, UDM_SETPOS32, 0, static_cast<INT_PTR>(pValueField->GetValue()->AsInt()));
		}
		break;
	case ValueType_Int32:
	case ValueType_UInt32:
	case ValueType_Int64:
	case ValueType_UInt64:
		{
			assert(iSubItem == 1);
			if (iSubItem != 1)
				break;
			this->hCurEditPopup = CreateWindowEx(WS_EX_CLIENTEDGE,
				WC_EDIT, NULL, ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_LEFT, 
				targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			TCHAR valueText[32];
			if (pValueField->GetValue()->GetType() == ValueType_Int32
				|| pValueField->GetValue()->GetType() == ValueType_Int64)
				_stprintf_s(valueText, TEXT("%lld"), pValueField->GetValue()->AsInt64());
			else
				_stprintf_s(valueText, TEXT("%llu"), pValueField->GetValue()->AsUInt64());
			Edit_SetText(this->hCurEditPopup, valueText);
		}
		break;
	case ValueType_Float:
	case ValueType_Double:
		{
			assert(iSubItem == 1);
			if (iSubItem != 1)
				break;
			this->hCurEditPopup = CreateWindowEx(WS_EX_CLIENTEDGE,
				WC_EDIT, NULL, ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_LEFT, 
				targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			TCHAR valueText[64];
			_stprintf_s(valueText, TEXT("%f"), pValueField->GetValue()->AsDouble());
			Edit_SetText(this->hCurEditPopup, valueText);
		}
		break;
	case ValueType_String:
		if (pValueField->GetValue()->AsString())
		{
			assert(iSubItem == 1);
			if (iSubItem != 1)
				break;
			this->hCurEditPopup = CreateWindowEx(WS_EX_CLIENTEDGE,
				WC_EDIT, NULL, ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_LEFT, 
				targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			auto pText = unique_MultiByteToTCHAR(pValueField->GetValue()->AsString());
			Edit_SetText(this->hCurEditPopup, pText.get());
		}
		break;
	case ValueType_Array:
		{
			int32_t curVal = -1;
			int32_t rangeMin = -1;
			int32_t rangeMax = -1;
			if (pValueField->GetValue()->AsArray()->size >= static_cast<DWORD>(INT_MAX))
				break;
			if (iSubItem == 1)  //Value
			{
				curVal = pValueField->GetValue()->AsArray()->size;
				rangeMin = pValueField->GetValue()->AsArray()->size;
				rangeMax = INT_MAX;
			}
			else if (iSubItem == 0) //hItem is an array item, while pValueField is the array base field.
			{
				assert(pValueField->GetValue()->AsArray()->size > 0);
				if (pValueField->GetValue()->AsArray()->size <= 1)
					break;
				rangeMin = 0;
				rangeMax = pValueField->GetValue()->AsArray()->size - 1;
				auto arrayMappingIt = pAssetDesc->arrayMappingsByArray.find(pValueField);
				if (arrayMappingIt == pAssetDesc->arrayMappingsByArray.end())
					break;
				auto indexMapIt = arrayMappingIt->second.itemToIndexMap.find(hItem);
				if (indexMapIt == arrayMappingIt->second.itemToIndexMap.end())
					break;
				curVal = indexMapIt->second;
			}
			else
				break;
			this->hCurEditPopup = CreateWindowEx(WS_EX_CLIENTEDGE,
				WC_EDIT, NULL, ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_LEFT, 
				targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top,
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			this->hCurEditPopupUpDown = CreateWindowEx(WS_EX_LTRREADING,
				UPDOWN_CLASS, NULL, WS_CHILD | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 
				0, 0, 0, 0, // Auto size the Up-Down Control
				hTree, GetMenu(this->hDialog), this->appContext.getMainWindow().getHInstance(), NULL);
			SendMessage(this->hCurEditPopupUpDown, UDM_SETRANGE32, rangeMin, rangeMax);
			SendMessage(this->hCurEditPopupUpDown, UDM_SETPOS32, 0, curVal);
		}
		break;
	case ValueType_ByteArray:
		//Ignore (for now)
		break;
	}
	if (this->hCurEditPopup == NULL)
		return;
	SetWindowSubclass(this->hCurEditPopup, EditPopupProc, 0, reinterpret_cast<DWORD_PTR>(this));
	SendMessage(this->hCurEditPopup, WM_SETFONT, (WPARAM)(HFONT)SendMessage(hTree, WM_GETFONT, 0, 0), FALSE);
	SetFocus(this->hCurEditPopup);
}

std::vector<AssetTypeValueField*> AssetViewModifyDialog::findAllPPtrs(AssetTypeValueField *pBaseField, AssetIdentifier &asset)
{
	std::vector<AssetTypeValueField*> ret;
	if (pBaseField->IsDummy() ||
		asset.pFile == nullptr)
		return ret;
	//Somewhat complex PPtr search algorithm.
	//The simplest way would be to (recursively) iterate through all AssetTypeValueFields.
	//To make sure the overhead does not explode if the asset has huge, unrelated arrays,
	// the corresponding type template is first searched
	// and 'search instructions' are generated for the path to each PPtr.
	//Finally, the 'interpreter' walks through these instructions,
	// looping if an Array is on the path (or several).
	struct PPtrSearchInstr
	{
		//Amount of visited fields to leave.
		// - Before leaving an array, jump back to the instruction that entered the array if there are more elements.
		uint32_t nLeave;
		//Index of the next child field to visit.
		uint32_t entryChildIdx;
		PPtrSearchInstr()
			: nLeave(0), entryChildIdx(0)
		{}
		PPtrSearchInstr(uint32_t nLeave, uint32_t entryChildIdx)
			: nLeave(nLeave), entryChildIdx(entryChildIdx)
		{}
	};
	struct TemplateStackEntry
	{
		AssetTypeTemplateField *pTemplate;
		size_t nextChildIdx;
		TemplateStackEntry()
			: pTemplate(nullptr), nextChildIdx(0)
		{}
		TemplateStackEntry(AssetTypeTemplateField *pTemplate, size_t nextChildIdx)
			: pTemplate(pTemplate), nextChildIdx(nextChildIdx)
		{}
	};
	std::vector<PPtrSearchInstr> searchInstructions; 
	uint32_t curSearchInstructionDepth = 0;
	uint32_t searchInstructionPendingLeaves = 0;
	std::vector<TemplateStackEntry> templateSearchStack;
	templateSearchStack.emplace_back(TemplateStackEntry(pBaseField->GetTemplateField(), 0));
	while (!templateSearchStack.empty())
	{
		TemplateStackEntry stackEntry = templateSearchStack.back();
		bool leaveEntry = false;
		if (stackEntry.pTemplate->isArray)
		{
			if (stackEntry.nextChildIdx == 0 && stackEntry.pTemplate->children.size() == 2)
			{
				//Enter the first array element.
				//The search instruction interpreter handles array iteration implicitly.
				// It remembers the arrays it entered and the current array index.
				// When it is instructed to leave fields, it jumps back if an array to be left has further elements.
				templateSearchStack.back().nextChildIdx = 1;
				templateSearchStack.push_back(TemplateStackEntry(&stackEntry.pTemplate->children[1], 0));
			}
			else
				leaveEntry = true;
		}
		else
		{
			if (stackEntry.nextChildIdx == 0 && //If it is a PPtr, its children aren't analyzed and therefore nextChildIdx always is 0.
				!strncmp(stackEntry.pTemplate->type.c_str(), "PPtr<", 5))
			{
				assert(curSearchInstructionDepth - searchInstructionPendingLeaves < templateSearchStack.size());
				//Add instructions to enter the PPtr.
				for (size_t i = curSearchInstructionDepth - searchInstructionPendingLeaves; i < templateSearchStack.size() - 1; i++)
				{
					assert(templateSearchStack[i].nextChildIdx > 0);
					//Put the pending leave counter in the instruction, and enter the next child.
					searchInstructions.emplace_back(
						PPtrSearchInstr(searchInstructionPendingLeaves,
							(uint32_t)(templateSearchStack[i].nextChildIdx - 1))
					);
					curSearchInstructionDepth = curSearchInstructionDepth - searchInstructionPendingLeaves + 1;
					searchInstructionPendingLeaves = 0;
				}
				//Leave the PPtr immediately, since there are no further PPtrs inside.
				leaveEntry = true;
			}
			else if (stackEntry.nextChildIdx < stackEntry.pTemplate->children.size())
			{
				assert(curSearchInstructionDepth - searchInstructionPendingLeaves <= templateSearchStack.size());
				//Enter the next child.
				size_t nextChildIdx = stackEntry.nextChildIdx;
				templateSearchStack.back().nextChildIdx++;
				templateSearchStack.push_back(TemplateStackEntry(&stackEntry.pTemplate->children[nextChildIdx], 0));
			}
			else
				leaveEntry = true;
		}
		if (leaveEntry)
		{
			if (curSearchInstructionDepth - searchInstructionPendingLeaves >= templateSearchStack.size())
			{
				//'Leave' this node, in case it is entered at the state of the latest search instruction.
				assert(curSearchInstructionDepth - searchInstructionPendingLeaves == templateSearchStack.size());
				searchInstructionPendingLeaves++;
			}
			templateSearchStack.pop_back();
		}
	}
	//Add an end instruction that leaves all open nodes plus the base node.
	// If the base node is a PPtr, the end instruction will be the only one.
	// If the end instruction wasn't there, the PPtr would not be processed.
	searchInstructions.emplace_back(PPtrSearchInstr(curSearchInstructionDepth + 1, 0));

	struct ExecArrayDesc
	{
		size_t enterInstrIdx; //Instruction index that entered the first element of the array.
		uint32_t execStackDepth; //Index in execStack where the array value field is located.
		uint32_t curChildIdx; //Index of the current child.
		ExecArrayDesc()
			: execStackDepth(0), enterInstrIdx(0), curChildIdx(0)
		{}
		ExecArrayDesc(uint32_t execStackDepth, size_t enterInstrIdx)
			: execStackDepth(execStackDepth), enterInstrIdx(enterInstrIdx), curChildIdx(0)
		{}
	};
	std::vector<ExecArrayDesc> openArrays;
	std::vector<AssetTypeValueField*> execStack;
	execStack.push_back(pBaseField);
	for (size_t ip = 0; ip < searchInstructions.size(); ip++)
	{
		assert(!execStack.empty());
		if (execStack.back()->GetType().starts_with("PPtr<"))
		{
			//Found a PPtr.
			AssetTypeValueField *pPPtrBase = execStack.back();
			AssetTypeValueField *pFileIDField = pPPtrBase->Get("m_FileID");
			AssetTypeValueField *pPathIDField = pPPtrBase->Get("m_PathID");
			if (!pFileIDField->IsDummy() && pFileIDField->GetValue() != nullptr &&
				!pPathIDField->IsDummy() && pPathIDField->GetValue() != nullptr &&
				pPathIDField->GetValue()->AsUInt64() != 0)
			{
				//The PPtr is non-empty.
				ret.push_back(pPPtrBase);
			}
		}
		//Leave the specified amount of stack elements.
		uint32_t nLeaveLeft = searchInstructions[ip].nLeave;
		assert(nLeaveLeft <= execStack.size());
		if (nLeaveLeft > execStack.size())
			break;
		//If an array element is left (<=> the leave counter shrinks the stack at least to the array itself,
		//      i.e. so that afterwards [execStack.size <= array.execStackDepth + 1] holds),
		// check if there are further array elements.
		while (!openArrays.empty() && execStack.size() - nLeaveLeft <= openArrays.back().execStackDepth + 1)
		{
			//Assert that an element of the outermost openArrays entry is in the stack.
			//If that were not the case, the openArrays entry should have been closed already.
			assert(execStack.size() > openArrays.back().execStackDepth);
			if (execStack.size() <= openArrays.back().execStackDepth)
				return ret;

			//Leave the array element but not the array itself.
			uint32_t nCurLeave = static_cast<uint32_t>(execStack.size() - openArrays.back().execStackDepth - 1);
			execStack.erase(execStack.begin() + (execStack.size() - nCurLeave), execStack.end());
			//for (uint32_t iLeave = 0; iLeave < nCurLeave; iLeave++)
			//	execStack.pop_back();
			nLeaveLeft -= nCurLeave;
			if (execStack[openArrays.back().execStackDepth]->GetChildrenCount() < openArrays.back().curChildIdx + 1)
			{
				//Continue with the instruction that entered the array element.
				ip = openArrays.back().enterInstrIdx;
				openArrays.back().curChildIdx++;
				nLeaveLeft = 0;
				break;
			}
			else
			{
				//Close the openArrays entry and then check for the next openArrays entry, if it exists.
				openArrays.pop_back();
			}
		}
		execStack.erase(execStack.begin() + (execStack.size() - nLeaveLeft), execStack.end());
		if (execStack.empty())
			break;
		assert(!execStack.back()->IsDummy());
		if (execStack.back()->IsDummy())
			break;
		if (execStack.back()->GetTemplateField()->isArray)
		{
			//Open an array element.
			if (openArrays.empty() || openArrays.back().execStackDepth != execStack.size() - 1)
			{
				//Create the openArrays entry when entering the first element.
				openArrays.emplace_back(ExecArrayDesc(static_cast<uint32_t>(execStack.size() - 1), ip));
			}
			execStack.push_back(execStack.back()->Get(openArrays.back().curChildIdx));
		}
		else
		{
			assert(execStack.back()->GetChildrenCount() > searchInstructions[ip].entryChildIdx);
			if (execStack.back()->GetChildrenCount() <= searchInstructions[ip].entryChildIdx)
				break;
			execStack.push_back(execStack.back()->Get(searchInstructions[ip].entryChildIdx));
		}
	}

	return ret;
}
bool AssetViewModifyDialog::moveArrayItem(HWND hTree, AssetDeserializeDesc *pAssetDesc, 
	AssetTypeValueField *pArrayField, MC_HTREELISTITEM hItem, uint32_t newIndex)
{
	auto arrayMappingIt = pAssetDesc->arrayMappingsByArray.find(pArrayField);
	if (arrayMappingIt == pAssetDesc->arrayMappingsByArray.end())
		return false;
	auto indexMapIt = arrayMappingIt->second.itemToIndexMap.find(hItem);
	if (indexMapIt == arrayMappingIt->second.itemToIndexMap.end())
		return false;
	if (static_cast<size_t>(newIndex) >= arrayMappingIt->second.treeItems.size())
		newIndex = static_cast<uint32_t>(arrayMappingIt->second.treeItems.size() - 1);
	uint32_t oldIndex = indexMapIt->second;

	//Move the item in the array<=>item mappings.
	arrayMappingIt->second.moveItem(hItem, newIndex);
	//Move the item in the child list.
	AssetTypeValueField **pChildrenList = pArrayField->GetChildrenList();
	AssetTypeValueField *pMovedField = pChildrenList[oldIndex];
	if (newIndex < oldIndex)
	{
		memmove(&pChildrenList[newIndex + 1], &pChildrenList[newIndex],
			(oldIndex - newIndex) * sizeof(AssetTypeValueField*));
	}
	else if (newIndex > oldIndex)
	{
		memmove(&pChildrenList[oldIndex], &pChildrenList[oldIndex + 1],
			(newIndex - oldIndex) * sizeof(AssetTypeValueField*));
	}
	pChildrenList[newIndex] = pMovedField;
	//Move the item in the tree list control (after the item with the next smaller index, or as the first item).
	MC_HTREELISTITEM afterItem = MC_TLI_FIRST;
	for (size_t _i = newIndex; _i > 0; _i--)
	{
		size_t i = _i - 1;
		if (arrayMappingIt->second.treeItems[i] != NULL)
		{
			afterItem = arrayMappingIt->second.treeItems[i];
			break;
		}
	}
	SendMessage(hTree, MC_TLM_MOVEITEM, reinterpret_cast<WPARAM>(hItem), reinterpret_cast<LPARAM>(afterItem));
	pAssetDesc->hasChanged = true;
	return true;
}
void AssetViewModifyDialog::openContextMenuPopup(HWND hTree, POINT clickPos,
		MC_HTREELISTITEM hItem, AssetTypeValueField *pValueField,
		MC_HTREELISTITEM hParentItem, AssetTypeValueField *pParentValueField,
		AssetDeserializeDesc *pAssetDesc)
{
	static const uintptr_t ID_ARRAYENTRY_DELETE =    9001;
	static const uintptr_t ID_ARRAYENTRY_MOVEUP =    9002;
	static const uintptr_t ID_ARRAYENTRY_MOVEDOWN =  9003;
	static const uintptr_t ID_ARRAYENTRY_MOVE =      9004;
	static const uintptr_t ID_PPTRENTRY_NEWVIEWTAB = 9005;
	static const uintptr_t ID_PPTRENTRY_SHOWINLIST = 9006;
	if (pAssetDesc == nullptr || pAssetDesc->asset.pFile == nullptr)
		return;
	if (this->hCurPopupMenu != NULL)
	{
		DestroyMenu(this->hCurPopupMenu);
		this->hCurPopupMenu = NULL;
	}
	UINT popupMenuFlags = TPM_RETURNCMD | TPM_NONOTIFY;
	if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
		popupMenuFlags |= TPM_RIGHTALIGN | TPM_HORNEGANIMATION;
	else
		popupMenuFlags |= TPM_HORPOSANIMATION;
	if (pValueField == nullptr)
	{
		if (hItem == NULL || (hItem != MC_TLI_ROOT && (hParentItem == NULL || pParentValueField == nullptr)))
			return;
		if (hItem == MC_TLI_ROOT
			|| (pParentValueField != nullptr
				&& pParentValueField->GetType().starts_with("PPtr<")))
		{
			pathid_t pathID = 0;
			unsigned int absFileID = 0;
			if (hItem == MC_TLI_ROOT)
			{
				absFileID = pAssetDesc->asset.pFile->getFileID();
				pathID = pAssetDesc->asset.pathID;
			}
			else if (pParentValueField != nullptr)
			{
				AssetTypeValueField* pFileIDField = pParentValueField->Get("m_FileID");
				AssetTypeValueField* pPathIDField = pParentValueField->Get("m_PathID");
				if (pFileIDField->GetValue() == nullptr || pPathIDField->GetValue() == nullptr)
					return;
				unsigned int fileID = pFileIDField->GetValue()->AsUInt();
				pathID = pPathIDField->GetValue()->AsUInt64();
				absFileID = pAssetDesc->asset.pFile->resolveRelativeFileID(fileID);
			}
			if (absFileID == 0 || pathID == 0)
				return;
			//Handle PPtr '[View asset]' nodes.
			this->hCurPopupMenu = CreatePopupMenu();
			if (this->hCurPopupMenu == NULL)
				return;
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_PPTRENTRY_NEWVIEWTAB, TEXT("Show in &new tab"));
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_PPTRENTRY_SHOWINLIST, TEXT("Show in asset &list"));
			uintptr_t selectedId = static_cast<uintptr_t>(TrackPopupMenuEx(this->hCurPopupMenu, popupMenuFlags, clickPos.x, clickPos.y, this->hDialog, NULL));
			switch (selectedId)
			{
			case ID_PPTRENTRY_NEWVIEWTAB:
				if (!this->assetListDialog.openViewDataTab(absFileID, pathID))
					MessageBox(this->hDialog, TEXT("Unable to find the requested asset."), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
				break;
			case ID_PPTRENTRY_SHOWINLIST:
				if (this->assetListDialog.selectAsset(absFileID, pathID))
					this->assetListDialog.switchToListTab();
				else
					MessageBox(this->hDialog, TEXT("Unable to find the requested asset."), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
				break;
			}
		}
		else if (pParentValueField != nullptr && pParentValueField->GetValue() && pParentValueField->GetValue()->AsArray())
		{
			//Handle Array index nodes.
			auto arrayMappingIt = pAssetDesc->arrayMappingsByArray.find(pParentValueField);
			if (arrayMappingIt == pAssetDesc->arrayMappingsByArray.end())
				return;
			auto indexMapIt = arrayMappingIt->second.itemToIndexMap.find(hItem);
			if (indexMapIt == arrayMappingIt->second.itemToIndexMap.end())
				return;
			if (indexMapIt->second > pParentValueField->GetChildrenCount())
				return;
			assert(pParentValueField->GetChildrenCount() == pParentValueField->GetValue()->AsArray()->size);

			this->hCurPopupMenu = CreatePopupMenu();
			if (this->hCurPopupMenu == NULL)
				return;
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_ARRAYENTRY_MOVEUP, TEXT("Move array item &up"));
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_ARRAYENTRY_MOVE, TEXT("Move array item &to"));
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_ARRAYENTRY_MOVEDOWN, TEXT("Move array item &down"));
			AppendMenu(this->hCurPopupMenu, MF_STRING, ID_ARRAYENTRY_DELETE, TEXT("&Remove array item"));
			uintptr_t selectedId = static_cast<uintptr_t>(TrackPopupMenuEx(this->hCurPopupMenu, popupMenuFlags, clickPos.x, clickPos.y, this->hDialog, NULL));
			//Find again to prevent any issues from message handling during TrackPopupMenuEx.
			arrayMappingIt = pAssetDesc->arrayMappingsByArray.find(pParentValueField);
			if (arrayMappingIt == pAssetDesc->arrayMappingsByArray.end())
				selectedId = 0;
			else
			{
				indexMapIt = arrayMappingIt->second.itemToIndexMap.find(hItem);
				if (indexMapIt == arrayMappingIt->second.itemToIndexMap.end())
					selectedId = 0;
			}
			switch (selectedId)
			{
			case ID_ARRAYENTRY_MOVE:
				this->openEditPopup(hTree, hItem, pParentValueField, pAssetDesc, 0);
				break;
			case ID_ARRAYENTRY_MOVEUP:
			case ID_ARRAYENTRY_MOVEDOWN:
				{
					uint32_t newIndex = indexMapIt->second;
					if (selectedId == ID_ARRAYENTRY_MOVEUP)
						newIndex = (newIndex > 0) ? (newIndex - 1) : 0;
					else if (selectedId == ID_ARRAYENTRY_MOVEDOWN)
						newIndex = (newIndex < pParentValueField->GetChildrenCount()-1) ? (newIndex + 1) : (pParentValueField->GetChildrenCount() - 1);
					this->moveArrayItem(hTree, pAssetDesc, pParentValueField, hItem, newIndex);
				}
				break;
			case ID_ARRAYENTRY_DELETE:
				{
					uint32_t oldIndex = indexMapIt->second;
					AssetTypeValueField **pChildrenList = pParentValueField->GetChildrenList();
					AssetTypeValueField *pDeletedField = pChildrenList[oldIndex];
					
					//1. Check for PPtrs below the deleted array entry, where an asset could be loaded in the subtree.
					std::vector<AssetTypeValueField*> pptrFields = findAllPPtrs(pDeletedField, pAssetDesc->asset);
					
					//2. For each PPtr candidate, find all subtree assets to be closed (=> AssetDeserializeDesc::children)
					bool hasUnappliedChanges = false;
					std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>> closedSubtreeAssets;
					for (size_t i = 0; i < pptrFields.size(); i++)
					{
						auto assetIt = this->loadedAssetsByPPtrField.find(pptrFields[i]);
						if (assetIt != this->loadedAssetsByPPtrField.end())
						{
							bool curHasUnappliedChanges;
							auto recursiveChildren = this->findAllSubtreeAssets(assetIt->second, curHasUnappliedChanges, pptrFields[i]);
							closedSubtreeAssets.insert(closedSubtreeAssets.end(), recursiveChildren.begin(), recursiveChildren.end());
						}
					}
					//3. If there are unsaved changes, ask.
					bool cancelledDelete = false;
					if (hasUnappliedChanges)
					{
						switch (MessageBox(this->hDialog, TEXT("There are unsaved changes in assets opened below a PPtr.\n")
							TEXT("Do you want save these changes before proceeding?"), TEXT("Asset Bundle Extractor"), MB_ICONWARNING | MB_YESNOCANCEL))
						{
						case IDYES:
							{
								std::vector<std::reference_wrapper<AssetDeserializeDesc>> assetsToApply;
								assetsToApply.reserve(closedSubtreeAssets.size());
								for (size_t i = 0; i < closedSubtreeAssets.size(); ++i)
									assetsToApply.push_back(*closedSubtreeAssets[i].second);
								if (!this->applyChangesIn(assetsToApply.begin(), assetsToApply.end()))
									cancelledDelete = true;
							}
							break;
						case IDNO:
							break;
						case IDCANCEL:
							cancelledDelete = true;
							break;
						}
					}
					if (cancelledDelete)
						break;
					//4. Close the assets properly and remove them from all maps.
					this->closeAllSubtreeAssets(hTree, closedSubtreeAssets, pAssetDesc);

					//5. Remove the array entry :
					//5a. Remove the entry from the array entry<->index mapping.
					arrayMappingIt->second.removeItem(hItem, oldIndex);
					//5b. Remove the entry field from the child list, and decrease the child list size (keeping the old and thereby oversized list memory).
					memmove(&pChildrenList[oldIndex], &pChildrenList[oldIndex + 1],
						(pParentValueField->GetChildrenCount() - oldIndex - 1) * sizeof(AssetTypeValueField*));
					pParentValueField->SetChildrenList(pChildrenList, pParentValueField->GetChildrenCount() - 1);
					pParentValueField->GetValue()->AsArray()->size = pParentValueField->GetChildrenCount();
					//5c. Delete the item from the tree list before freeing its memory.
					SendMessage(hTree, MC_TLM_DELETEITEM, MC_TLDI_NONOTIFY, reinterpret_cast<LPARAM>(hItem));
					//5d. Free any additionally allocated memory for the deleted field (i.e. if it was added as a new array element).
					FreeFieldRecursively(pDeletedField, pAssetDesc->instanceModificationBuffers);

					//6. Update the array size text.
					setTreeValueText(hTree, hParentItem, pParentValueField);
					pAssetDesc->hasChanged = true;
					
					//7. Select a new item and redraw the following items so the displayed indices are updated.
					RECT treeClientRect = {};
					GetClientRect(hTree, &treeClientRect);
					int curItemY = -1;
					int itemHeight = static_cast<int>(SendMessage(hTree, MC_TLM_GETITEMHEIGHT, 0, 0));
					if (itemHeight <= 0)
						break;
					bool foundNextItem = false;
					for (size_t i = oldIndex; i < pParentValueField->GetChildrenCount(); i++)
					{
						MC_HTREELISTITEM hCurItem = arrayMappingIt->second.treeItems[i];
						if (hCurItem == NULL)
							continue;
						MC_TLITEM item;
						item.fMask = 0;
						if (curItemY == -1)
						{
							RECT rect = {};
							rect.left = MC_TLIR_BOUNDS;
							if (!SendMessage(hTree, MC_TLM_GETITEMRECT, reinterpret_cast<WPARAM>(hCurItem), reinterpret_cast<LPARAM>(&rect)))
								break;
							curItemY = rect.top;
							//Select the next item. Visibility is ensured since the user selected the previous item at this location.
							item.fMask = MC_TLIF_STATE;
							item.state = MC_TLIS_SELECTED;
							item.stateMask = MC_TLIS_SELECTED;
							foundNextItem = true;
						}
						//Force an item redraw.
						SendMessage(hTree, MC_TLM_SETITEM, reinterpret_cast<WPARAM>(hCurItem), reinterpret_cast<LPARAM>(&item));
						curItemY += itemHeight;
						if (curItemY > treeClientRect.bottom)
							break;
					}
					if (!foundNextItem)
					{
						//If we deleted the last visible item, select the new last item.
						MC_TLITEM item;
						item.fMask = MC_TLIF_STATE;
						item.state = MC_TLIS_SELECTED;
						item.stateMask = MC_TLIS_SELECTED;
						for (size_t _i = oldIndex; _i > 0; _i--)
						{
							size_t i = _i - 1;
							MC_HTREELISTITEM hCurItem = arrayMappingIt->second.treeItems[i];
							if (hCurItem != NULL)
							{
								SendMessage(hTree, MC_TLM_SETITEM, reinterpret_cast<WPARAM>(hCurItem), reinterpret_cast<LPARAM>(&item));
								SendMessage(hTree, MC_TLM_ENSUREVISIBLE, 0, reinterpret_cast<LPARAM>(hCurItem));
								break;
							}
						}
					}
				}
				break;
			}
		}
	}
	else
	{
		if (hItem == NULL)
			return;
		//Context menu behaviour for normal nodes defined by plugins.
		const PluginMapping& plugins = appContext.getPlugins();
		auto citer = plugins.options.cbegin();
		std::shared_ptr<IAssetViewEntryOptionProvider> pCurProvider;
		std::vector<std::pair<std::string, std::unique_ptr<IOptionRunner>>> viableOptions;
		while (citer = plugins.getNextOptionProvider<IAssetViewEntryOptionProvider>(citer, pCurProvider), pCurProvider != nullptr)
		{
			std::string optionName;
			std::unique_ptr<IOptionRunner> pRunner = pCurProvider->prepareForSelection(
				appContext, *this,
				FieldInfo(pValueField,
					AssetAbsPPtr(pAssetDesc->asset.pFile->getFileID(), pAssetDesc->asset.pathID),
					hItem),
				optionName);
			if (pRunner != nullptr)
			{
				viableOptions.push_back({ std::move(optionName), std::move(pRunner) });
			}
		}

		size_t sel = ShowContextMenu(viableOptions.size(), [&viableOptions](size_t i) {return viableOptions[i].first.c_str(); },
			popupMenuFlags, clickPos.x, clickPos.y, this->hDialog,
			this->hCurPopupMenu);
		if (sel != (size_t)-1)
			(*viableOptions[sel].second)(); //Let the plugin perform the action.
	}
	if (this->hCurPopupMenu != NULL)
	{
		DestroyMenu(this->hCurPopupMenu);
		this->hCurPopupMenu = NULL;
	}
}

LRESULT CALLBACK AssetViewModifyDialog::AssetTreeListSubclassProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	AssetViewModifyDialog *pThis = reinterpret_cast<AssetViewModifyDialog*>(dwRefData);
	switch (message)
	{
	case WM_LBUTTONDBLCLK:
		{
			MC_TLHITTESTINFO hitTestInfo = {};
			hitTestInfo.pt.x = GET_X_LPARAM(lParam); //Client area
			hitTestInfo.pt.y = GET_Y_LPARAM(lParam);
			if (SendMessage(hWnd, MC_TLM_HITTEST, 0, reinterpret_cast<LPARAM>(&hitTestInfo)) != NULL && hitTestInfo.iSubItem == 1)
			{
				assert(hitTestInfo.hItem != NULL);
				if (pThis->hCurEditPopup != NULL)
					pThis->doCloseEditPopup();
				
				TCHAR textBuf[32]; textBuf[31] = 0;
				MC_TLITEM item;
				item.fMask = MC_TLIF_PARAM | MC_TLIF_TEXT;
				item.cchTextMax = 31;
				item.pszText = textBuf;
				item.lParam = 0;
				if (!SendMessage(hWnd, MC_TLM_GETITEM, 
						reinterpret_cast<WPARAM>(hitTestInfo.hItem),
						reinterpret_cast<LPARAM>(&item)))
					break;
				if (item.lParam == 0)
					break;

				AssetTypeValueField *pValueField = reinterpret_cast<AssetTypeValueField*>(item.lParam);
				AssetDeserializeDesc *pAssetDesc = pThis->getAssetDescForItem(hitTestInfo.hItem);
				if (!pAssetDesc)
					break;
				if (!pValueField->GetValue())
					break;
				
				pThis->openEditPopup(hWnd, hitTestInfo.hItem, pValueField, pAssetDesc);
				return (LRESULT)0;
			}
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, AssetTreeListSubclassProc, uIdSubclass);
		break;
	}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK AssetViewModifyDialog::EditPopupProc(HWND hWnd, UINT message, 
	WPARAM wParam, LPARAM lParam, 
	uintptr_t uIdSubclass, DWORD_PTR dwRefData)
{
	AssetViewModifyDialog *pThis = (AssetViewModifyDialog*)dwRefData;
	switch (message)
	{
	case WM_KILLFOCUS:
		//if (wParam == WA_INACTIVE)
		{
			if (pThis->hCurEditPopup != NULL)
				pThis->doCloseEditPopup();
		}
		break;
	case WM_KEYDOWN:
		if (LOWORD(wParam) == VK_ESCAPE || LOWORD(wParam) == VK_RETURN)
		{
			if (pThis->hCurEditPopup != NULL)
				pThis->doCloseEditPopup();
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, EditPopupProc, uIdSubclass);
		break;
	}
    return DefSubclassProc(hWnd, message, wParam, lParam);
}
