#pragma once
#include "api.h"
#include "AppContext.h"
#include "MainWindow2.h"
#include "Win32BatchImportDesc.h"

static const GUID UABE_FILEDIALOG_FILE_GUID = { 0x832dbb4b, 0xf1bf, 0x8e37, 0x69, 0xc5, 0x41, 0x6e, 0x5f, 0x72, 0x67, 0xc1 };
static const GUID UABE_FILEDIALOG_EXPIMPASSET_GUID = { 0x832dbb4b, 0xf1bf, 0x8e37, 0x69, 0xc5, 0x41, 0x6e, 0x5f, 0x72, 0x67, 0xc2 };
static const GUID UABE_FILEDIALOG_CLDB_GUID = { 0x832dbb4b, 0xf1bf, 0x8e37, 0x69, 0xc5, 0x41, 0x6e, 0x5f, 0x72, 0x67, 0xc3 };

enum EWin32AppContextMsg
{
	Win32AppContextMsg_COUNT=AppContextMsg_COUNT
};

class MainWindow2;
class Win32AppContext : public AppContext
{
	MainWindow2 mainWindow;
	std::string baseDir;

	size_t gcMemoryLimit;
	unsigned int gcMinAge;
	void LoadSettings();

	bool handlingMessages;
	CRITICAL_SECTION messageMutex;
	std::vector<std::pair<EAppContextMsg,void*>> messageQueue;
	bool messagePosted;

	void handleMessages();
	bool processMessage(EAppContextMsg message, void *args);

	std::shared_ptr<FileContextInfo> OnFileOpenAsBundle(std::shared_ptr<FileOpenTask> pTask, BundleFileContext *pContext, EBundleFileOpenStatus openStatus, unsigned int parentFileID, unsigned int directoryEntryIdx);
	std::shared_ptr<FileContextInfo> OnFileOpenAsAssets(std::shared_ptr<FileOpenTask> pTask, AssetsFileContext *pContext, EAssetsFileOpenStatus openStatus, unsigned int parentFileID, unsigned int directoryEntryIdx);
	std::shared_ptr<FileContextInfo> OnFileOpenAsResources(std::shared_ptr<FileOpenTask> pTask, ResourcesFileContext *pContext, unsigned int parentFileID, unsigned int directoryEntryIdx);
	std::shared_ptr<FileContextInfo> OnFileOpenAsGeneric(std::shared_ptr<FileOpenTask> pTask, GenericFileContext *pContext, unsigned int parentFileID, unsigned int directoryEntryIdx);
	void OnFileOpenFail(std::shared_ptr<FileOpenTask> pTask, std::string &logText);
	public: UABE_Win32_API void OnUpdateContainers(AssetsFileContextInfo *info);
	UABE_Win32_API void OnUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to);
	protected:
	void OnDecompressBundle(BundleFileContextInfo::DecompressTask *pTask, TaskResult result);
	void OnChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved);
	void OnChangeBundleEntry(BundleFileContextInfo *pFile, size_t index);

	void RemoveContextInfo(FileContextInfo *info);
public:
	UABE_Win32_API void signalMainThread(EAppContextMsg message, void *args);
	//Process memory threshold that triggers cache 'garbage collection' (see MainWindow2::disposableCacheElements).
	inline size_t getGCMemoryLimit() { return gcMemoryLimit; }
	//Minimum age of a resource to be eligible for 'garbage collection'.
	inline unsigned int getGCMinAge() { return gcMinAge; }
	inline MainWindow2 &getMainWindow() { return mainWindow; }
	inline std::string getBaseDir() { return baseDir; }

	UABE_Win32_API bool ShowAssetBatchImportDialog(IAssetBatchImportDesc* pDesc, std::string basePath);
	UABE_Win32_API bool ShowAssetBatchImportDialog(IAssetBatchImportDesc* pDesc, IWin32AssetBatchImportDesc* pDescWin32, std::string basePath);

	//Asks the user to provide an export file or directory path.
	// -> If assets.size() == 0, returns an empty string.
	// -> If assets.size() == 1, the user is asked to select one output file path.
	// -> If assets.size() >  1, the user is asked to select an output directory.
	//Assumes that all AssetIdentifiers in assets are resolved.
	UABE_Win32_API std::string QueryAssetExportLocation(const std::vector<struct AssetUtilDesc>& assets,
		const std::string &extension, const std::string &extensionFilter);
	//Asks the user to provide an import file or directory path. An empty return value signals cancelling the action.
	//The user may possibly change the set of assets to actually import.
	//The returned vector indices correspond to the indices in the (potentially modified) assets vector.
	// -> If assets.size() == 0, returns an empty vector.
	// -> If assets.size() == 1, the user is asked to select one file from the filesystem.
	// -> If assets.size() >  1, the user is asked to select an input directory and a batch import dialog is shown.
	//Assumes all assets have resolved AssetIdentifiers already.
	UABE_Win32_API std::vector<std::string> QueryAssetImportLocation(std::vector<AssetUtilDesc>& assets,
		std::string extension, std::string extensionRegex, std::string extensionFilter);

	UABE_Win32_API Win32AppContext(HINSTANCE hInstance, const std::string &baseDir);
	UABE_Win32_API ~Win32AppContext();

	UABE_Win32_API int Run(size_t argc, char **argv);
	friend class MainWindow2;
};

