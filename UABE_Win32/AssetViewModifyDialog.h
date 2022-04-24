#pragma once
#include "api.h"
#include "AssetListDialog.h"
#include "Win32AppContext.h"
#include "../AssetsTools/AssetTypeClass.h"
#include <memory>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mCtrl\treelist.h>


struct AssetAbsPPtr
{
	unsigned int fileID;
	pathid_t pathID;
	inline AssetAbsPPtr()
		: fileID(0), pathID(0)
	{}
	inline AssetAbsPPtr(unsigned int fileID, pathid_t pathID)
		: fileID(fileID), pathID(pathID)
	{}
	inline bool operator==(AssetAbsPPtr other) const
	{
		return fileID == other.fileID && pathID == other.pathID;
	}
};

namespace std
{
	template<>
	struct hash<AssetAbsPPtr>
	{
		std::size_t operator()(AssetAbsPPtr const& pptr) const
		{
			static std::hash<unsigned int> uintHash;
			static std::hash<pathid_t> llHash;
			size_t fidHash = uintHash(pptr.fileID);
			size_t pidHash = llHash(pptr.pathID);
			return (fidHash << 1) ^ pidHash;
		}
	};
}

class AssetViewModifyDialog : public AssetModifyDialog, public TaskProgressCallback
{
public:
	struct FieldInfo
	{
		AssetTypeValueField* pValueField = nullptr;
		//Reference to the asset (file ID and path ID).
		//Can be assumed not to change for any child value fields reachable from pValueField.
		AssetAbsPPtr assetIDs = {};
		//Handle to the UI representation of this value field.
		void* treeListHandle = nullptr;
		inline FieldInfo() {}
		inline FieldInfo(AssetTypeValueField* pValueField, AssetAbsPPtr assetIDs, MC_HTREELISTITEM treeListItem)
			: pValueField(pValueField), assetIDs(assetIDs), treeListHandle((void*)treeListItem)
		{}
	};
private:
	AssetListDialog &assetListDialog;
	Win32AppContext &appContext;
	
	bool isDestroyed;
	size_t registeredCallbackCounter; //(is 0) => callback is not registered
	struct AssetDeserializeDesc
	{
		AssetIdentifier asset;
		AssetTypeTemplateField templateBase;
		ITask *pLoadTask;
		TaskResult loadTaskResult;
		std::unique_ptr<AssetTypeInstance> pAssetInstance;
		//The tree list item above this asset's base item.
		MC_HTREELISTITEM parentItem; 
		MC_HTREELISTITEM baseItem; 
		//Set if this asset has 'unsaved' changes not added to the IAssetsReplacer list.
		bool hasChanged; 
		//Set if <pLoadTask> is non-null while closing the asset.
		//If set, this entry should get removed from the loadedAssets list once the task is done.
		bool pendingClose;
		//Set if the user has already been asked about extracting the MonoBehaviour class information.
		//-> Do not ask again in case the required type information still cannot be found.
		bool monoBehaviourInfoAsked;

		//Assumption: All open assets form a tree through the child lists. The root element is the first asset.
		// => Each AssetDeserializeDesc is child of exactly one other, or is the root (and no child).

		//Assets opened through PPtrs in this asset's tree. The first element of the pair is the 'PPtr' typed field.
		std::vector<std::pair<AssetTypeValueField*,std::list<AssetDeserializeDesc>::iterator>> children;
		AssetDeserializeDesc *pParent;

		std::unordered_set<uint8_t*> instanceModificationBuffers;
		struct ArrayMappings
		{
			std::vector<MC_HTREELISTITEM> treeItems; //Same size as the array. NULL indicates gaps.
			std::unordered_map<MC_HTREELISTITEM, uint32_t> itemToIndexMap;
			inline void insertItem(MC_HTREELISTITEM item, uint32_t index)
			{
				size_t oldSize = treeItems.size();
				treeItems.insert(treeItems.begin() + index, item);
				if (index < oldSize)
				{
					for (auto it = itemToIndexMap.begin(); it != itemToIndexMap.end(); ++it)
					{
						if (it->second >= index)
							it->second++;
					}
				}
				itemToIndexMap.insert(std::make_pair(item, index));
			}
			inline void removeItem(MC_HTREELISTITEM item, uint32_t index)
			{
				itemToIndexMap.erase(item);
				treeItems.erase(treeItems.begin() + index);
				for (auto it = itemToIndexMap.begin(); it != itemToIndexMap.end(); ++it)
				{
					if (it->second > index)
						it->second--;
				}
			}
			inline void swapItems(MC_HTREELISTITEM firstItem, uint32_t secondIndex)
			{
				MC_HTREELISTITEM secondItem = treeItems[secondIndex];
				auto firstItemIt = itemToIndexMap.find(firstItem);
				uint32_t firstIndex = firstItemIt->second;
				firstItemIt->second = secondIndex;
				itemToIndexMap.find(secondItem)->second = firstIndex;
				treeItems[firstIndex] = secondItem;
				treeItems[secondIndex] = firstItem;
			}
			inline void moveItem(MC_HTREELISTITEM item, uint32_t newIndex)
			{
				auto itemIt = itemToIndexMap.find(item);
				uint32_t oldIndex = itemIt->second;
				treeItems.erase(treeItems.begin() + oldIndex);
				for (auto it = itemToIndexMap.begin(); it != itemToIndexMap.end(); ++it)
				{
					if (it->second >= oldIndex)
						it->second--;
					if (it->second >= newIndex)
						it->second++;
				}
				treeItems.insert(treeItems.begin() + newIndex, item);
				itemIt->second = newIndex;
			}
		};
		std::unordered_map<AssetTypeValueField*,ArrayMappings> arrayMappingsByArray;

		inline AssetDeserializeDesc()
			: pLoadTask(nullptr), loadTaskResult(-256), parentItem(NULL), baseItem(NULL), hasChanged(false), pendingClose(false), monoBehaviourInfoAsked(false), pParent(nullptr)
		{}
		inline ~AssetDeserializeDesc()
		{
			for (auto it = instanceModificationBuffers.begin(); it != instanceModificationBuffers.end(); ++it)
				delete[] *it;
		}
		inline AssetDeserializeDesc(AssetDeserializeDesc &&other)
			: asset(std::move(other.asset)), templateBase(std::move(other.templateBase)),
			pLoadTask(other.pLoadTask), loadTaskResult(other.loadTaskResult),
			pAssetInstance(std::move(other.pAssetInstance)),
			parentItem(other.parentItem), baseItem(other.baseItem),
			hasChanged(other.hasChanged), pendingClose(other.pendingClose),
			monoBehaviourInfoAsked(other.monoBehaviourInfoAsked),
			children(std::move(other.children)), pParent(other.pParent),
			instanceModificationBuffers(std::move(other.instanceModificationBuffers))
		{
			other.pLoadTask = nullptr;
		}
		inline AssetDeserializeDesc &operator=(AssetDeserializeDesc &&other) noexcept
		{
			this->asset = std::move(other.asset);
			this->templateBase = std::move(other.templateBase);
			this->pLoadTask = other.pLoadTask;
			other.pLoadTask = nullptr;
			this->loadTaskResult = other.loadTaskResult;
			this->pAssetInstance = std::move(other.pAssetInstance);
			this->parentItem = other.parentItem;
			this->baseItem = other.baseItem;
			this->hasChanged = other.hasChanged;
			this->pendingClose = other.pendingClose;
			this->monoBehaviourInfoAsked = other.monoBehaviourInfoAsked;
			this->children = std::move(other.children);
			this->instanceModificationBuffers = std::move(other.instanceModificationBuffers);
			this->pParent = other.pParent;
		}
	};
	std::list<AssetDeserializeDesc> loadedAssets;
	std::unordered_map<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator> loadedAssetsByPPtrField;
	std::unordered_map<AssetAbsPPtr, std::list<AssetDeserializeDesc>::iterator> loadedAssetsByPPtr;
	std::unordered_map<MC_HTREELISTITEM, std::list<AssetDeserializeDesc>::iterator> loadedAssetsByBaseItem;

	std::list<AssetDeserializeDesc>::iterator findOrPrepareLoad(AssetIdentifier asset, MC_HTREELISTITEM parentItem, AssetDeserializeDesc *pParentAsset,
		AssetTypeValueField *pPPtrField = nullptr);
	bool startLoadTask(std::list<AssetDeserializeDesc>::iterator assetEntry,
		std::shared_ptr<AssetViewModifyDialog> selfPtr = std::shared_ptr<AssetViewModifyDialog>());
	AssetDeserializeDesc *getAssetDescForItem(MC_HTREELISTITEM item);

	std::string assetName;
	HWND hDialog;
	HWND hTree=NULL;
	static HWND _getTreeHandle(HWND hDialog);
	inline HWND getTreeHandle()
	{
		if (!hTree) hTree = _getTreeHandle(hDialog);
		return hTree;
	}
	bool ignoreExpandNotifications;
	
	HMENU hCurPopupMenu;
	HWND hCurEditPopup;
	HWND hCurEditPopupUpDown;
	MC_HTREELISTITEM hEditPopupItem;
	AssetTypeValueField *pEditValueField;
	int iEditPopupSubItem;
	AssetDeserializeDesc *pEditAssetDesc;

	//Finds all non-empty PPtrs in the asset, starting with the provided base field.
	// pBaseField: Base field for the search, corresponding with the given asset. Does not have to be the absolute base field for the asset.
	// asset: The resolved AssetIdentifier, based on which the absolute PPtr IDs are resolved.
	//Returns: All identified "PPtr<*" type fields with a m_FileID and m_PathID value, the latter being non-zero.
	static std::vector<AssetTypeValueField*> findAllPPtrs(AssetTypeValueField *pBaseField, AssetIdentifier &asset);
	//Finds all assets in the subtree of a given asset (including the asset itself).
	//Additionally outputs, whether any of the assets have unapplied changes (-> hasUnappliedChanges).
	std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>> findAllSubtreeAssets(
		std::list<AssetDeserializeDesc>::iterator itBaseAssetDesc,
		bool &hasUnappliedChanges,
		AssetTypeValueField *pBasePPtrField = nullptr);
	void closeAllSubtreeAssets(HWND hTree,
		std::vector<std::pair<AssetTypeValueField*, std::list<AssetDeserializeDesc>::iterator>> subtreeAssets,
		AssetDeserializeDesc *pParentAsset);

	void doCloseEditPopup();
	void openEditPopup(HWND hTree, MC_HTREELISTITEM hItem, AssetTypeValueField *pValueField, AssetDeserializeDesc *pAssetDesc,
		int iSubItem = 1);
	bool moveArrayItem(HWND hTree, AssetDeserializeDesc *pAssetDesc, 
		AssetTypeValueField *pArrayField, MC_HTREELISTITEM hItem, uint32_t newIndex);
	void openContextMenuPopup(HWND hTree, POINT clickPos,
		MC_HTREELISTITEM hItem, AssetTypeValueField *pValueField,
		MC_HTREELISTITEM hParentItem, AssetTypeValueField *pParentValueField,
		AssetDeserializeDesc *pAssetDesc);

	//Inner implementation for applyChanges, see below.
	//Allows other lists than all assets in the view to be saved.
	//ForwardIt: forward iterator that dereferences to AssetDeserializeDesc& (e.g. std::list<AssetDeserializeDesc>::iterator).
	template <class ForwardIt>
	bool applyChangesIn(ForwardIt assetBegin, ForwardIt assetEnd);

	bool testItemHasTextcallbackActive;
	bool testItemHasTextcallbackResult;
	bool testItemHasTextcallback(HWND hTree, MC_HTREELISTITEM hItem);
	TCHAR itemTextcallbackBuf[32];
	
	static INT_PTR CALLBACK AssetViewProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK AssetTreeListSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);
	static LRESULT CALLBACK EditPopupProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, 
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);
	static void onCompletionMainThread(uintptr_t param1, uintptr_t param2);

	bool setStringValue(AssetTypeValueField* pField, AssetDeserializeDesc* pAssetDesc, const char* str, size_t str_len);
protected:
	MC_HTREELISTITEM addTreeItems(AssetDeserializeDesc *pAssetDesc, MC_HTREELISTITEM hParent, LPARAM parentLParam, AssetTypeValueField **pFields, size_t fieldCount, bool isPPtr, size_t startIdx = 0);
public:
	//asset: The resolved identifier of the target asset.
	AssetViewModifyDialog(AssetListDialog &assetListDialog, Win32AppContext &appContext, AssetIdentifier asset, std::string assetName);
	~AssetViewModifyDialog();
	//hParentWnd : Parent window for MessageBox calls during init(..)
	bool init(std::shared_ptr<AssetViewModifyDialog> &selfPtr, HWND hParentWnd);
	
	//Called when the user requests to close the tab.
	//Returns true if there are unsaved changes, false otherwise.
	//If the function will return true and applyable is not null,
	// *applyable will be set to true iff applyNow() is assumed to succeed without further interaction
	// (e.g. all fields in the dialog have a valid value, ...).
	//The caller uses this info to decide whether and how it should display a confirmation dialog before proceeding.
	bool hasUnappliedChanges(bool *applyable=nullptr);
	//Called when the user requests to apply the changes (e.g. selecting Apply, Save or Save All in the menu).
	//Returns whether the changes have been applied;
	// if true, the caller may continue closing the AssetModifyDialog.
	// if false, the caller may stop closing the AssetModifyDialog.
	//Note: applyChanges() is expected to notify the user about errors (e.g. via MessageBox).
	bool applyChanges();
	std::string getTabName();
	HWND getWindowHandle();
	//Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
	bool onCommand(WPARAM wParam, LPARAM lParam);
	void onHotkey(ULONG message, DWORD keyCode); //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
	//Called when the dialog is to be shown. The parent window will not change before the next onHide call.
	void onShow(HWND hParentWnd);
	//Called when the dialog is to be hidden, either because of a tab switch or while closing the tab.
	void onHide();
	//Called when the tab is about to be destroyed.
	//Once this function is called, AssetListDialog::removeModifyDialog must not be used for this dialog.
	void onDestroy();

	//TaskProgressCallback
	void OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result);

	//Set the value for a string field and manages the string memory properly.
	UABE_Win32_API bool setStringValue(AssetTypeValueField* pValueField, AssetAbsPPtr assetIDs, const char *str, size_t str_len);
	inline bool setStringValue(AssetTypeValueField* pValueField, AssetAbsPPtr assetIDs, const std::string& str)
	{ return setStringValue(pValueField, assetIDs, str.c_str(), str.size()); }

	//Set the value for a ByteArray field and manages the string memory properly.
	UABE_Win32_API bool setByteArrayValue(FieldInfo fieldInfo, std::unique_ptr<uint8_t[]> data, size_t data_len);

	//Update the shown value for a field and its children.
	//Note: Array and ByteArray fields are NOT supported,
	// and PPtr-referenced assets opened below the given field are not updated!
	UABE_Win32_API void updateValueFieldText(FieldInfo fieldInfo, bool markAsChanged = true);

	//Retrieves the next child of the parent, given the previous child and the next child's index.
	//idxTracker must be set to 0 by the caller before the first call, and not modified in between calls!
	// Bypasses the seek inefficiency compared to getChildFieldInfo, if all childs are to be enumerated.
	//Note: Array fields are NOT supported, and PPtr-referenced assets opened in the view are not followed!
	UABE_Win32_API FieldInfo getNextChildFieldInfo(FieldInfo parentFieldInfo, FieldInfo prevFieldInfo);

	//Retrieves the child of the parent with the given index.
	// Quite inefficient (linear search due to UI backend limitations).
	//Note: Array fields are NOT supported, and PPtr-referenced assets opened in the view are not followed!
	inline FieldInfo getChildFieldInfo(FieldInfo fieldInfo, size_t iChild)
	{
		FieldInfo ret;
		for (size_t i = 0; i < iChild; ++i)
		{
			ret = getNextChildFieldInfo(fieldInfo, ret);
			if (ret.pValueField == nullptr)
				break;
		}
		return ret;
	}
	//Retrieves the child of the parent with the given name.
	//Note: Array fields are NOT supported!
	inline FieldInfo getChildFieldInfo(FieldInfo fieldInfo, const char* childFieldName)
	{
		AssetTypeValueField** childList = fieldInfo.pValueField->GetChildrenList();
		uint32_t childCount = fieldInfo.pValueField->GetChildrenCount();
		for (uint32_t i = 0; i < childCount; i++)
		{
			if (childList[i]->GetTemplateField() != NULL)
			{
				if (childList[i]->GetTemplateField()->name == childFieldName)
					return getChildFieldInfo(fieldInfo, i);
			}
		}
		return FieldInfo();
	}

	friend class AssetInstanceTask;
};