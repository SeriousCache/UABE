#pragma once
#include "api.h"
#include <CommCtrl.h>
#include <mctrl.h>
#include <map>
#include <unordered_map>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <assert.h>
#include <deque>
#include <time.h>
#include "FileContext.h"
#include "AppContext.h"
#include "SelectClassDbDialog.h"
#include "SplitterControlHandler.h"
#include "../UABE_Generic/TaskStatusTracker.h"

//Note: If extensibility through plugins is required, UUIDs may need to be used instead.
enum EFileManipulateDialogType
{
	FileManipulateDialog_Other,
	FileManipulateDialog_AssetList,
	FileManipulateDialog_AssetsDependencies,
	FileManipulateDialog_AssetsContainers,
	FileManipulateDialog_Bundle,
};

class IFileManipulateDialog;
class IFileManipulateDialogFactory
{
public:
	UABE_Win32_API IFileManipulateDialogFactory();
	UABE_Win32_API virtual ~IFileManipulateDialogFactory();
	//Constructs the object. 
	virtual std::shared_ptr<IFileManipulateDialog> construct(EFileManipulateDialogType type, HWND hParent)=0;
};
class DefaultFileDialogFactory : public IFileManipulateDialogFactory
{
	class Win32AppContext *pContext;
	//std::shared_ptr<IFileManipulateDialog> pAssetListDialog;
public:
	UABE_Win32_API ~DefaultFileDialogFactory();
	UABE_Win32_API DefaultFileDialogFactory(class Win32AppContext *pContext);
	UABE_Win32_API std::shared_ptr<IFileManipulateDialog> construct(EFileManipulateDialogType type, HWND hParent);
};

class ITreeParameter
{
public:
	bool b_isFileManipulateDialogInfo;
	inline class FileManipulateDialogInfo *asFileManipulateDialogInfo()
	{
		if (isFileManipulateDialogInfo())
			return (class FileManipulateDialogInfo*)this;
		return nullptr;
	}
	inline class FileEntryUIInfo *asFileEntryInfo()
	{
		if (isFileEntryInfo())
			return (class FileEntryUIInfo*)this;
		return nullptr;
	}
	inline bool isFileManipulateDialogInfo() const { return b_isFileManipulateDialogInfo; }
	inline bool isFileEntryInfo() const { return !b_isFileManipulateDialogInfo; }
};
class FileManipulateDialogInfo : public ITreeParameter
{
public:
	FileManipulateDialogInfo();
	~FileManipulateDialogInfo();

	class FileEntryUIInfo *pEntry;
	MC_HTREELISTITEM hTreeItem;
	EFileManipulateDialogType type; uintptr_t param; 
};
class FileEntryUIInfo : public ITreeParameter
{
	 size_t shortNameIndex;
public:
	//Pending file entry constructor
	FileEntryUIInfo(MC_HTREELISTITEM hTreeItem, const std::string &fullName, bool isFilePath = true);
	~FileEntryUIInfo();
	//Iterator for the std::list this is stored in. Needs to be set manually after inserting the entry into the list!
	std::list<FileEntryUIInfo>::iterator myiter;
	bool failed;
	bool pending; //If true, implies that pContextInfo is null and that there are no child entries in the tree.
	//Note that hTreeItem may appear in the first standardDialogs entry.
	MC_HTREELISTITEM hTreeItem;
	std::shared_ptr<FileContextInfo> pContextInfo;
	ITask *pTask;

	//No duplicate hTreeItems are allowed. Only the first entry in standardDialogs may use the file entry's hTreeItem.
	size_t standardDialogsCount;
	std::array<FileManipulateDialogInfo, 6> standardDialogs;
	std::list<FileManipulateDialogInfo> subDialogs;

	std::string openLogText;
	std::string fullName;
public:
	class DialogsIterator
	{
		FileEntryUIInfo &entryInfo;
		size_t standardDialogsI;
		std::list<FileManipulateDialogInfo>::iterator iter;
	public:
		inline DialogsIterator(FileEntryUIInfo &entryInfo)
			: entryInfo(entryInfo), standardDialogsI(0), iter(entryInfo.subDialogs.begin())
		{}
		inline FileManipulateDialogInfo *operator->() { return &**this; }
		inline FileManipulateDialogInfo &operator*()
		{
			if (standardDialogsI < entryInfo.standardDialogsCount)
				return entryInfo.standardDialogs[standardDialogsI];
			return *iter;
		}
		inline DialogsIterator &operator++()
		{
			standardDialogsI++;
			if (standardDialogsI > entryInfo.standardDialogsCount)
				iter++;
			return *this;
		}
		inline bool end()
		{
			return (standardDialogsI >= entryInfo.standardDialogsCount && iter == entryInfo.subDialogs.end());
		}
	};
	inline const char *getShortName()
	{
		return &(fullName.c_str()[shortNameIndex]);
	}
	inline void setContextInfo(std::shared_ptr<FileContextInfo> pContextInfo)
	{
		if (!pending)
			this->pContextInfo = pContextInfo;
		else
			assert(false);
	}
	inline std::shared_ptr<FileContextInfo> getContextInfo()
	{
		if (!pending) return pContextInfo;
		return nullptr;
	}
	inline FileContextInfo *getContextInfoPtr()
	{
		if (!pending) return pContextInfo.get();
		return nullptr;
	}
	inline ITask *getTask()
	{
		if (pending) return pTask;
		return nullptr;
	}
	inline DialogsIterator getDialogsIterator()
	{
		return DialogsIterator(*this);
	}
	//Updates the name for inner files (esp. bundle entries).
	inline void updateName(std::string newName)
	{
		if (shortNameIndex == 0)
			fullName = std::move(newName);
	}
};
//Resource only used by the UI thread (not thread safe). The resource can be generated at any time.
class UIDisposableCache
{
	unsigned int nRefs;
public:
	UABE_Win32_API UIDisposableCache();
	UABE_Win32_API virtual ~UIDisposableCache();
	virtual size_t approxMemory() = 0;
	//The timestamp of the last time this cache item was used.
	//Only needs to be up to date if isInUse would return false.
	virtual time_t getLastUseTime() = 0;
	virtual bool isInUse() = 0;
	//TODO: Last usage timestamp and bool 'isInUse'
	UABE_Win32_API virtual void incRef();
	UABE_Win32_API virtual void decRef();
};
//Basic reference holder for UIDisposableCache and sub classes.
template <class T=UIDisposableCache>
struct UIDisposableCacheRef
{
	T *pCache;
public:
	inline UIDisposableCacheRef(T *pCache = nullptr)
		: pCache(pCache)
	{
		if (pCache) pCache->incRef();
	}
	inline ~UIDisposableCacheRef()
	{
		if (pCache) pCache->decRef();
		pCache = nullptr;
	}
	inline UIDisposableCacheRef(const UIDisposableCacheRef &other)
	{
		pCache = other.pCache;
		if (pCache)	pCache->incRef();
	}
	inline UIDisposableCacheRef(UIDisposableCacheRef &&other) noexcept
	{
		pCache = other.pCache;
		other.pCache = nullptr;
	}
	inline UIDisposableCacheRef &operator=(const UIDisposableCacheRef &other)
	{
		pCache = other.pCache;
		if (pCache)	pCache->incRef();
		return (*this);
	}
	inline T *operator->()
	{
		return pCache;
	}
	inline T *get()
	{
		return pCache;
	}
};
class MainWindowEventHandler
{
public:
	UABE_Win32_API virtual void onUpdateContainers(AssetsFileContextInfo *pFile);
	UABE_Win32_API virtual void onChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved);
	UABE_Win32_API virtual void onUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to);
	UABE_Win32_API virtual void onUpdateBundleEntry(BundleFileContextInfo *pFile, size_t index);
};
typedef std::list<MainWindowEventHandler*>::iterator MainWindowEventHandlerHandle;
class MainWindow2
{
	typedef void* HResource;

	class Win32AppContext *pContext;

	HWND hDlg;
	HMENU hMenu;
	HINSTANCE hInstance;
	HHOOK hHotkeyHook;

	SplitterControlHandler<true> mainPanelSplitter;
	float fileTreeColumnRatio;

	HWND hContainersDlg;
	struct ManipDlgDesc
	{
		std::shared_ptr<IFileManipulateDialog> pCurManipDlg;
		std::vector<ITreeParameter*> selection;
		//TODO: If a selected tree parameter is a pending file that is about to finish loading, either
		//      a) directly force a refresh of the selected elements list like with a selection change (if this is the current tab)
		//   or b) set the forceSelectionRefresh flag so the elements list gets refreshed once the user switches to this tab
		//          (This could also be done by noticing that an ITreeParameter is a FileEntryUIInfo and not a FileManipulateDialogInfo,
		//            which means that it is for a pending file).
		//bool forceSelectionRefresh;
	};
	std::vector<ManipDlgDesc> manipDlgTabs; size_t activeManipDlgTab;
	bool oneshot_applySelectionToCurrentTab;
	inline ManipDlgDesc *getActiveManipDlgDesc()
	{
		if (activeManipDlgTab < manipDlgTabs.size())
			return &manipDlgTabs[activeManipDlgTab];
		return nullptr;
	}
	inline IFileManipulateDialog *getActiveManipDlg()
	{
		ManipDlgDesc *pDesc = getActiveManipDlgDesc();
		if (pDesc)
			return pDesc->pCurManipDlg.get();
		return nullptr;
	}
	std::list<FileEntryUIInfo> fileEntries;
	std::unordered_map<FileContextInfo*, FileEntryUIInfo*> fileEntriesByContextInfo;
	std::unordered_map<ITask*, FileEntryUIInfo*> pendingFileEntriesByTask;
	std::array<size_t, FileContext_COUNT> fileEntryCountersByType;

	//Maps a resource (FileContextInfo*, FileEntryUIInfo*) to a disposable cache entry.
	//Removal of the underlying resource will remove the cache entry from this map and decrease its reference counter.
	//(TODO: Implement this behaviour, and use this functionality for the AssetListDialog
	//  -> Cache of asset list rows for an AssetsFileContextInfo object)
	std::unordered_map<HResource, std::list<UIDisposableCacheRef<>>> disposableCacheElements;
	std::list<MainWindowEventHandler*> eventHandlers;

	bool decompressTargetDir_cancel;
	std::string decompressTargetDir;

	std::unique_ptr<class TaskStatusTracker> pStatusTracker;
public:
	MainWindow2(HINSTANCE hInstance);
	~MainWindow2(void);

	bool Initialize();
	int HandleMessages();

	//void addManipulateDialog(IFileManipulateDialog *pDialog);
	//void removeManipulateDialog(IFileManipulateDialog *pDialog);
	UABE_Win32_API void hideManipulateDialog(class IFileManipulateDialog *pDialog);

	//Selects a file context.
	//preventOpenNewTab: If set, the new selection will be applied to the current tab (if possible).
	//                   If not set, a new tab may be opened depending on whether the user opened editors (among other factors).
	UABE_Win32_API void selectFileContext(unsigned int fileID, bool preventOpenNewTab);

	//Loads a bundle entry.
	UABE_Win32_API bool loadBundleEntry(std::shared_ptr<BundleFileContextInfo> pBundleInfo, unsigned int bundleEntryIdx);

	//Adds a disposable cache element to the list.
	//Avoid adding too many (small) cache elements to limit the 'garbage collection' performance hit.
	inline void addDisposableCacheElement(HResource hResource, UIDisposableCache *pElement)
	{
		disposableCacheElements[hResource].push_back(UIDisposableCacheRef<>(pElement));
	}
	//Find a disposable cache element on the given resource where dynamic_cast succeeds with the type T.
	//Returns a reference object with a null pointer on failure.
	template <class T>
	inline UIDisposableCacheRef<T> findDisposableCacheElement(HResource hResource)
	{
		auto cacheIt = disposableCacheElements.find(hResource);
		if (cacheIt != disposableCacheElements.end())
		{
			//This obviously is not efficient for large amounts of cache entries for a resource.
			for (auto elementIt = cacheIt->second.begin(); elementIt != cacheIt->second.end(); ++elementIt)
			{
				T *target = dynamic_cast<T*>(elementIt->get());
				if (target != nullptr)
					return UIDisposableCacheRef<T>(target);
			}
		}
		return UIDisposableCacheRef<T>();
	}
	inline MainWindowEventHandlerHandle registerEventHandler(MainWindowEventHandler *pHandler)
	{
		return eventHandlers.insert(eventHandlers.end(), pHandler);
	}
	inline void unregisterEventHandler(MainWindowEventHandlerHandle hHandler)
	{
		eventHandlers.erase(hHandler);
	}

	inline HMENU getMenu() { return hMenu; }
	inline HINSTANCE getHInstance() { return hInstance; }
	inline HWND getWindow() { return hDlg; }

	inline std::list<FileEntryUIInfo> &getFileEntries()
	{
		return fileEntries;
	}
	
	//Closes the file and all children. Be careful when closing multiple files, as other FileContextInfo pointers (i.e. from child files) may become invalid.
	//Returns false if the user aborted it due to unsaved changes.
	UABE_Win32_API bool CloseFile(FileEntryUIInfo *info, HWND hTree = NULL);
	//Closes the file and all children. Note that FileContextInfo pointers from child files may become invalid.
	//Returns false if the user aborted it due to unsaved changes, or if the file wasn't opened.
	UABE_Win32_API bool CloseFile(unsigned int fileID);

protected:
	bool OnFileEntryLoadSuccess(ITask *pTask, std::shared_ptr<FileContextInfo> &pContextInfo, TaskResult result);
	//Uses logText.swap()
	void OnFileEntryLoadFailure(ITask *pTask, std::string logText);
	void OnDecompressSuccess(BundleFileContextInfo::DecompressTask *pTask);
	void OnDecompressFailure(BundleFileContextInfo::DecompressTask *pTask);
	void OnFindClassDatabaseFailure(AssetsFileContextInfo *pAssetsFileInfo, ClassDatabasePackage &package);
	void OnRemoveContextInfo(FileContextInfo *info);
	void OnUpdateContainers(AssetsFileContextInfo *pFile);
	void OnUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to);
	void OnChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved);
	void OnChangeBundleEntry(BundleFileContextInfo *pFile, size_t index);

	void CloseUIFileEntry(FileEntryUIInfo *info, HWND hTree = NULL);

	//Checks whether any tab potentially has unapplied changes for a given file.
	//-> Returns true iff a tab has this file selected and has unapplied changes.
	bool fileHasUnappliedChanges(FileEntryUIInfo *pFileInfo);
	//Checks whether the file has any applied but unsaved changes.
	bool fileHasUnsavedChanges(FileEntryUIInfo *pFileInfo);

	void updateBundleEntryName(BundleFileContextInfo *pBundleInfo, size_t bundleEntryIdx, std::string newName);

private:
	std::unique_ptr<SelectClassDbDialog> pSelectClassDbDialog;
	std::shared_ptr<ClassDatabaseFile> defaultDatabaseFile;
	std::map<std::string, std::shared_ptr<ClassDatabaseFile>> databaseFilesByEngineVersion;
	struct DbSelectionQueueEntry
	{
		FileEntryUIInfo *pEntry;
		bool reason_DatabaseNotFound;
		inline DbSelectionQueueEntry(FileEntryUIInfo *pEntry, bool reason_DatabaseNotFound)
			: pEntry(pEntry), reason_DatabaseNotFound(reason_DatabaseNotFound)
		{}
	};
	std::deque<DbSelectionQueueEntry> fileEntriesPendingForDbSelection;

	void OnSelectClassDbDialogFinished();
	//Tries to find the class database based on defaults : databaseFilesByEngineVersion, defaultDatabaseFile.
	bool TryFindClassDatabase(AssetsFileContextInfo *pAssetsFileInfo);
	//Requires that the front entry of fileEntriesPendingForDbSelection matches the function parameters.
	void OpenClassDatabaseSelection(AssetsFileContextInfo *pAssetsFileInfo, bool reason_DatabaseNotFound);
	
	std::unique_ptr<IFileManipulateDialogFactory> pDialogFactory;
private:
	static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
	static INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK ProgSubclassProc(HWND hWnd, UINT message,
		WPARAM wParam, LPARAM lParam,
		uintptr_t uIdSubclass, DWORD_PTR dwRefData);

	inline bool onAppContextMessageAsync()
	{
		bool result = PostMessage(hDlg, WM_APP+0, 0, 0);
		assert(result);
		return result;
	}

	void onResize(bool defer = true);

	bool ignoreTreeSelChanges, skipDeselectOnTabChange;
	void onClickSelectionCheckbox(unsigned int checkboxID, int checkState);
	void doUpdateSelectionCheckboxes(const std::vector<ITreeParameter*> &selections);
	void onChangeFileSelection();
	void onOpenFileCommand();
	void addPendingBaseFileEntry(ITask *pTask, const std::string &path);
	void onCloseFileCommand();
	bool onCloseProgramCommand();

	void onSaveFileRequest(FileEntryUIInfo *pUIInfo);

	void doOpenTab();
	bool preDeleteTab(MC_NMMTCLOSEITEM *pNotification);
	void onDeleteTab(MC_NMMTDELETEITEM *pNotification);
	void onSwitchTabs(MC_NMMTSELCHANGE *pNotification);

	//Runs the garbage collector if necessary.
	void onGCTick();

	inline void setContext(class Win32AppContext *pContext)
	{
		this->pContext = pContext;
	}
	friend class Win32AppContext;
};

class IFileManipulateDialog
{
protected:
	std::weak_ptr<IFileManipulateDialog> selfPtr;
	friend class MainWindow2;
public:
	UABE_Win32_API IFileManipulateDialog();
	UABE_Win32_API virtual ~IFileManipulateDialog();
	virtual void addFileContext(const std::pair<FileEntryUIInfo*,uintptr_t> &context)=0;
	virtual void removeFileContext(FileEntryUIInfo *pContext)=0;
	virtual EFileManipulateDialogType getType()=0;
	virtual HWND getWindowHandle()=0;
	virtual void onHotkey(ULONG message, DWORD keyCode)=0; //message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
	virtual bool onCommand(WPARAM wParam, LPARAM lParam)=0; //Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
	virtual void onShow()=0;
	virtual void onHide()=0;
	//Called when the user requests to close the tab.
	//Returns true if there are unapplied changes, false otherwise.
	//If the function will return true and applyable is not null,
	// *applyable will be set to true iff applyNow() is assumed to succeed without further interaction
	// (e.g. all fields in the dialog have a valid value, ...).
	//The caller uses this info to decide whether and how it should display a confirmation dialog before proceeding.
	virtual bool hasUnappliedChanges(bool *applyable=nullptr)=0;
	//Called when the user requests to apply the changes (e.g. selecting Apply, Save or Save All in the menu).
	//Returns whether the changes have been applied;
	// if true, the caller may continue closing the IFileManipulateDialog.
	// if false, the caller may stop closing the IFileManipulateDialog.
	//Note: applyChanges() is expected to notify the user about errors (e.g. via MessageBox).
	virtual bool applyChanges()=0;
	//Returns whether the tab prefers not to be automatically closed due to user interaction (e.g. open sub-dialogs).
	//-> The caller separately checks hasUnappliedChanges(), so unapplied changes need not be checked here.
	//For instance, a pure 'info' dialog with little to no user interaction should return false (can be closed).
	// Other dialogs may return true if the user has interacted to a certain degree (performed selections, ...).
	virtual bool doesPreferNoAutoclose()=0;
};