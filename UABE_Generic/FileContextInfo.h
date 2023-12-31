#pragma once
#include "api.h"
#include "FileContext.h"
#include "AssetContainerList.h"
#include "FileModTree.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/BundleReplacer.h"
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>

//unique_ptr to a IAssetsReader with a deleter.
typedef std::unique_ptr<IAssetsReader, void(*)(IAssetsReader*)> IAssetsReader_ptr;
static void DummyAssetsReaderDeleter(IAssetsReader*){}

typedef std::shared_ptr<ClassDatabaseFile> ClassDatabaseFile_sharedptr;
static void ClassDatabaseFileDeleter_Dummy(ClassDatabaseFile*) {}
static void ClassDatabaseFileDeleter_delete(ClassDatabaseFile *pFile)
{
	delete pFile;
}

typedef uint64_t pathid_t;

//Wrapper class around FileContext to track dynamically changeable information
// and to provide additional meta functionality.
class FileContextInfo
{
	unsigned int fileID;
	unsigned int parentFileID; //0 : has no parent
	std::string lastFileName; //Used only for internal management in AppContext (friend class).
	std::mutex fileNameOverrideMutex;
	std::string fileNameOverride;
public:
	UABE_Generic_API FileContextInfo(unsigned int fileID, unsigned int parentFileID);
	UABE_Generic_API virtual ~FileContextInfo();
	UABE_Generic_API virtual IFileContext *getFileContext()=0;
	UABE_Generic_API virtual void getChildFileIDs(std::vector<unsigned int> &childFileIDs)=0;
	UABE_Generic_API virtual void onCloseChild(unsigned int childFileID)=0;
	UABE_Generic_API virtual bool hasNewChanges(class AppContext &appContext)=0;
	UABE_Generic_API virtual bool hasAnyChanges(class AppContext &appContext)=0;
	UABE_Generic_API virtual uint64_t write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag)=0;
	UABE_Generic_API virtual std::unique_ptr<BundleReplacer> makeBundleReplacer(class AppContext &appContext, const char *oldName, const char *newName, uint32_t bundleIndex, bool resetChangedFlag)=0;
	inline unsigned int getFileID() { return fileID; }
	inline unsigned int getParentFileID() { return parentFileID; }
	UABE_Generic_API std::string getFileName();
	UABE_Generic_API void setFileName(std::string name);

	friend class AppContext;
};
typedef std::shared_ptr<FileContextInfo> FileContextInfo_ptr;

class AssetsFileContextInfo : public FileContextInfo
{
	//Locking order (to prevent deadlocks):
	//1) referencesLock
	//2) containersLock
	//2) replacersLock
	//3) classDatabaseLock
	//4) scriptDatabasesMutex

	AssetsFileContext *pContext;
	
	std::shared_mutex containersMutex;
	AssetContainerList containers;
	//After locking&unlocking containers for write, the AppContext has to be notified about a containers update.
	//  Since the UI thread may not wait for the read lock, any kind of lock needs to be followed by such a notification.
	inline void lockContainersWrite()
	{
		containersMutex.lock();
	}
	inline void unlockContainersWrite()
	{
		containersMutex.unlock();
	}
	std::shared_mutex referencesMutex;
	std::vector<unsigned int> references; //File IDs this has references to, in the same order as the .assets file dependency list. May contain 0 entries as fillers.
	std::vector<AssetsFileDependency> dependencies; //Overrides the .assets file dependency list. Locked by referencesMutex.
	bool dependenciesChanged = false;
	std::vector<unsigned int> containerSources; //File IDs that have a container list and references to this file.
	std::shared_mutex classDatabaseMutex; //Lock for atomic writes of pClassDatabase. Lock replacersLock first if both are to be locked.
	ClassDatabaseFile_sharedptr pClassDatabase; //Current main class database.
	std::mutex scriptDatabasesMutex;
	std::vector<std::shared_ptr<ClassDatabaseFile>> pScriptDatabases;
	std::shared_mutex replacersMutex;
	bool changedFlag = false;
	bool permanentChangedFlag = false;
	//shared_ptr adds overhead, e.g. while copying and freeing.
	// Since new AssetsEntryReplacers are only added and removed by user interaction (and in small amounts), this should not impose a problem.
	struct ReplacerEntry
	{
		bool replacesExistingAsset; //Set to true if the replacer's path ID matches the path ID of an asset existing in the underlying file.
		std::shared_ptr<AssetsEntryReplacer> pReplacer;
	};
	std::unordered_map<pathid_t, ReplacerEntry> pReplacersByPathID;
	inline void lockReplacersRead()
	{
		replacersMutex.lock_shared();
	}
	inline void unlockReplacersRead()
	{
		replacersMutex.unlock_shared();
	}
	//Lock before classDatabaseLock first if both are to be locked.
	inline void lockReplacersWrite()
	{
		replacersMutex.lock();
	}
	inline void unlockReplacersWrite()
	{
		replacersMutex.unlock();
	}

	bool _GetMonoBehaviourScriptInfo(class AssetIdentifier &asset, class AppContext &appContext,
		std::string &fullClassName, std::string &assemblyName,
		std::string &className, std::string &namespaceName,
		class AssetIdentifier &scriptAsset);

	//TODO: Replace this with a shared reader lock at some point.
	UABE_Generic_API std::vector<std::shared_ptr<ClassDatabaseFile>> &lockScriptDatabases();
	UABE_Generic_API void unlockScriptDatabases();

	//Adds a replacer for an asset. Removes any previous replacers for that path ID. 
	//If reuseTypeMetaFromOldReplacer is set and the replacer is not a remover, 
	// type information from any previous replacer is transferred to the new one.
	UABE_Generic_API void addReplacer(std::shared_ptr<AssetsEntryReplacer> replacer, class AppContext &appContext, bool reuseTypeMetaFromOldReplacer, bool signalMainThread);

public:
	UABE_Generic_API AssetsFileContextInfo(AssetsFileContext *pContext, unsigned int fileID, unsigned int parentFileID);
	~AssetsFileContextInfo();
	IFileContext *getFileContext();
	void getChildFileIDs(std::vector<unsigned int> &childFileIDs);
	void onCloseChild(unsigned int childFileID);
	bool hasNewChanges(class AppContext &appContext);
	bool hasAnyChanges(class AppContext &appContext);
	uint64_t write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag);
	std::unique_ptr<BundleReplacer> makeBundleReplacer(class AppContext &appContext, const char *oldName, const char *newName, uint32_t bundleIndex, bool resetChangedFlag);
	inline AssetsFileContext *getAssetsFileContext() { return pContext; }
	
	inline std::unique_lock<std::shared_mutex> lockReferencesWrite()
	{
		return std::unique_lock<std::shared_mutex>(referencesMutex);
	}
	inline std::vector<unsigned int>& getReferencesWrite(const std::unique_lock<std::shared_mutex>& lock)
	{
#ifdef _DEBUG
		if (!lock.owns_lock() || lock.mutex() != &referencesMutex)
			throw std::invalid_argument("AssetsFileContextInfo::getReferencesWrite: Unexpected lock");
#endif
		return references;
	}
	//Important: When adding or removing elements,
	//           the references vector must also be changed respectively!
	inline std::vector<AssetsFileDependency>& getDependenciesWrite(const std::unique_lock<std::shared_mutex>& lock)
	{
#ifdef _DEBUG
		if (!lock.owns_lock() || lock.mutex() != &referencesMutex)
			throw std::invalid_argument("AssetsFileContextInfo::getDependenciesWrite: Unexpected lock");
#endif
		return dependencies;
	}
	inline std::shared_lock<std::shared_mutex> lockReferencesRead()
	{
		return std::shared_lock<std::shared_mutex>(referencesMutex);
	}
	inline const std::vector<unsigned int>& getReferencesRead(const std::shared_lock<std::shared_mutex>& lock)
	{
#ifdef _DEBUG
		if (!lock.owns_lock() || lock.mutex() != &referencesMutex)
			throw std::invalid_argument("AssetsFileContextInfo::getReferencesRead: Unexpected lock");
#endif
		return references;
	}
	inline const std::vector<AssetsFileDependency>& getDependenciesRead(const std::shared_lock<std::shared_mutex>& lock)
	{
#ifdef _DEBUG
		if (!lock.owns_lock() || lock.mutex() != &referencesMutex)
			throw std::invalid_argument("AssetsFileContextInfo::getDependenciesRead: Unexpected lock");
#endif
		return dependencies;
	}
	inline const std::vector<unsigned int> getReferences()
	{
		auto lock = lockReferencesRead();
		const std::vector<unsigned int> ret = getReferencesRead(lock);
		return ret;
	}

	//Only call from the UI thread.
	inline std::vector<unsigned int> &getContainerSources()
	{ 
		return containerSources;
	}

	inline void setDependenciesChanged()
	{
		dependenciesChanged = true;
		changedFlag = true;
	}
	//Set if the underlying AssetsFile has changed.
	inline void setPermanentChangedFlag()
	{
		permanentChangedFlag = true;
		changedFlag = true;
	}
	
	inline AssetContainerList *tryLockContainersRead()
	{
		if (containersMutex.try_lock_shared())
			return &containers;
		return nullptr;
	}
	inline AssetContainerList &lockContainersRead()
	{
		containersMutex.lock_shared();
		return containers;
	}
	inline void unlockContainersRead()
	{
		containersMutex.unlock_shared();
	}

	inline bool getEndianness(bool &isBigEndian)
	{
		isBigEndian = false;
		if (pContext && pContext->getAssetsFile())
		{
			isBigEndian = (pContext->getAssetsFile()->header.endianness == 1);
			return true;
		}
		return false;
	}

	inline unsigned int resolveRelativeFileID(unsigned int relFileID)
	{
		if (relFileID == 0)
			return getFileID();
		auto lock = lockReferencesRead();
		const decltype(this->references) &references = getReferencesRead(lock);
		unsigned int ret = (references.size() < relFileID) ? 0 : references[relFileID-1];
		return ret;
	}
	
	//Guaranteed to return only up to one AssetsEntryReplacer per pathID.
	UABE_Generic_API std::vector<std::shared_ptr<AssetsReplacer>> getAllReplacers();
	UABE_Generic_API std::shared_ptr<AssetsEntryReplacer> getReplacer(pathid_t pathID);
	//Adds a replacer for an asset. Removes any previous replacers for that path ID. 
	//If reuseTypeMetaFromOldReplacer is set and the replacer is not a remover, 
	// type information from any previous replacer is transferred to the new one.
	inline void addReplacer(std::shared_ptr<AssetsEntryReplacer> replacer, class AppContext &appContext, bool reuseTypeMetaFromOldReplacer = true)
	{
		addReplacer(std::move(replacer), appContext, reuseTypeMetaFromOldReplacer, true);
	}

	//Retrieves the classID for the requested type, or -1 if it wasn't found.
	UABE_Generic_API int32_t GetClassByName(const char *name);
	//Returns the name of the requested type, or an empty string if it wasn't found. Note that the name is not guaranteed to work in a GetClassByName call.
	// The weird underscore is to avoid conflicts with the Win32 GetClassName TCHAR macro.
	UABE_Generic_API std::string GetClassName_(class AppContext &appContext, int32_t classID, uint16_t scriptIndex = 0xFFFF, class AssetIdentifier *pAsset = nullptr);
	UABE_Generic_API bool MakeTemplateField(AssetTypeTemplateField* pTemplateBase, class AppContext& appContext, int32_t classID, uint16_t scriptIndex = 0xFFFF, class AssetIdentifier* pAsset = nullptr,
		std::optional<std::reference_wrapper<bool>> missingScriptTypeInfo = {});
	UABE_Generic_API bool FindScriptClassDatabaseEntry(ClassDatabaseFile *&pClassFile, ClassDatabaseType *&pClassType, class AssetIdentifier &asset, class AppContext &appContext, Hash128 *pScriptID = nullptr);

	//Tries to find a proper class database from the package.
	// Returns true if a proper ClassDatabaseFile was found and/or the full type information is embedded in the .assets file.
	UABE_Generic_API bool FindClassDatabase(ClassDatabasePackage &package);
	//Sets the class database.
	UABE_Generic_API void SetClassDatabase(ClassDatabaseFile_sharedptr pClassDatabase);
	//Gets the class database (may return an empty pointer).
	UABE_Generic_API ClassDatabaseFile_sharedptr GetClassDatabase();

	inline void appendScriptDatabase(std::shared_ptr<ClassDatabaseFile> pDatabase)
	{
		auto &pScriptDatabases = lockScriptDatabases();
		pScriptDatabases.push_back(std::move(pDatabase));
		unlockScriptDatabases();
	}
	inline bool hasAnyScriptDatabases()
	{
		auto &pScriptDatabases = lockScriptDatabases();
		bool ret = !pScriptDatabases.empty();
		unlockScriptDatabases();
		return ret;
	}
	
public:
	class ContainersTask : public ITask
	{
		std::shared_ptr<AssetsFileContextInfo> pFileContextInfo; //Maintain object lifetime.

		std::string name;
		class AppContext &appContext;
	public:
		ContainersTask(class AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> &pContextInfo);
		const std::string &getName();
		TaskResult execute(TaskProgressManager &progressManager);
		inline AssetsFileContextInfo *getFileContextInfo() const
		{
			return pFileContextInfo.get();
		}
	};
	//Must be called from the main app thread.
	UABE_Generic_API bool EnqueueContainersTask(class AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> selfPointer);

	friend class AssetIterator;
	friend class AppContext;
};

struct BundleFileDirectoryInfo
{
	//File ID to find the child *FileContextInfo in the AppContext.
	//0: not loaded
	unsigned int fileID;
	//Override reader to use when loading the asset.
	//-> User can import files outside the bundle;
	//   instead of searching for the entry in the original bundle,
	//   this reader will be used instead (if non-nullptr).
	//-> Note: If the child file still is loaded (fileID != 0 and valid),
	//   it may be useful to initiate a reload after setting this field;
	//   otherwise, the sub file will still use the previous reader,
	//   and the pOverrideReader change will be ignored when saving.
	//-> Ignored if entryRemoved is set to true.
	std::shared_ptr<IAssetsReader> pOverrideReader;

	//Set if the file type readable in pOverrideReader is an .assets file.
	//-> Ignored if fileID != 0 or if entryRemoved is set to true.
	bool hasSerializedData;

	//Set if the entry should be considered removed.
	//-> Ignored if the child file is still loaded (fileID != 0).
	bool entryRemoved;

	//Set if a change in pOverrideReader or entryRemoved is considered new (unsaved).
	//-> Ignored if the child file is still loaded (fileID != 0).
	bool newChangeFlag;

	bool entryNameOverridden;
	std::string newEntryName;

	inline BundleFileDirectoryInfo()
		: fileID(0), hasSerializedData(false), entryRemoved(false), newChangeFlag(false), entryNameOverridden(false)
	{}
};
class BundleFileContextInfo : public FileContextInfo
{
	BundleFileContext *pContext;

	std::shared_mutex directoryMutex;
	//File IDs and other info for entries of this bundle, in the same order as the directory list.
	//-> Entries of this vector are never removed so as to preserve indices.
	//Changes to the vector or its data needs to be protected by an exclusive lock on directoryLock.
	//Reads need to be protected by a shared (or exclusive) lock on directoryLock.
	std::vector<BundleFileDirectoryInfo> directoryRefs; 

	std::unique_ptr<class VisibleFileEntry> modificationsToApply; //May contain changes to apply when opening sub file contexts.
	bool isDecompressed;
	void CloseContext();
	//Assumes that pContext != nullptr.
	//Takes its own exclusive lock on directoryRefs.
	std::vector<std::unique_ptr<BundleReplacer>> makeEntryReplacers(class AppContext &appContext, bool resetChangedFlag);
	UABE_Generic_API std::string getNewEntryName(size_t index, bool acquireLock);
public:
	BundleFileContextInfo(BundleFileContext *pContext, unsigned int fileID, unsigned int parentFileID);
	~BundleFileContextInfo();
	IFileContext *getFileContext();
	void getChildFileIDs(std::vector<unsigned int> &childFileIDs);
	void onCloseChild(unsigned int childFileID);
	bool hasNewChanges(class AppContext &appContext);
	bool hasAnyChanges(class AppContext &appContext);
	uint64_t write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag);
	std::unique_ptr<BundleReplacer> makeBundleReplacer(class AppContext &appContext, const char *oldName, const char *newName, uint32_t bundleIndex, bool resetChangedFlag);
	inline BundleFileContext *getBundleFileContext() { return pContext; }
	//Retrieves the bundle name portion in "archive:/"-style paths, e.g. "BuildPlayer-something".
	//Returns an empty string if no such name could be determined.
	UABE_Generic_API std::string getBundlePathName();
	inline std::string getNewEntryName(size_t index)
	{
		return getNewEntryName(index, true);
	}
	//Returns nullptr on error (e.g. index out of range, I/O error, ...), or if the entry has been deleted.
	UABE_Generic_API std::shared_ptr<IAssetsReader> makeEntryReader(size_t index, bool &readerIsModified);

	//Called once the bundle directory is deserialized.
	UABE_Generic_API void onDirectoryReady(class AppContext &appContext);
	
	//Sets the override name for a bundle entry. Returns true on success.
	UABE_Generic_API bool renameEntry(class AppContext &appContext, size_t index, std::string newEntryName);
	//Sets the override reader for a bundle entry. Returns true on success.
	UABE_Generic_API bool overrideEntryReader(class AppContext &appContext, size_t index, std::shared_ptr<IAssetsReader> pReader, bool hasSerializedData);
	//Sets the override reader and override name for a bundle entry. Returns true on success (returns false on partial success, i.e. reader but not name changed).
	inline bool overrideEntryReader(class AppContext &appContext, size_t index, std::shared_ptr<IAssetsReader> pReader, bool hasSerializedData, std::string newEntryName)
	{
		return overrideEntryReader(appContext, index, std::move(pReader), hasSerializedData)
			&& renameEntry(appContext, index, std::move(newEntryName));
	}
	//Sets the remove flag of a bundle entry. Note: The effect only takes place once the bundle entry has closed. Returns true on success.
	//Note: Does not affect any of the succeeding entry indices.
	UABE_Generic_API bool removeEntry(class AppContext &appContext, size_t index);
	//Adds a new entry and returns its index. Returns (size_t)-1 on failure.
	UABE_Generic_API size_t addEntry(class AppContext &appContext, std::shared_ptr<IAssetsReader> pReader, bool hasSerializedData, std::string entryName);
	
	//Checks whether an entry has been removed. Can return true even if the child file still is open.
	UABE_Generic_API bool entryIsRemoved(size_t index);
	//Checks whether an entry has changed (renamed, reader overridden, removed, added). Does not check for changes in the child FileContextInfo.
	UABE_Generic_API bool entryHasChanged(size_t index);
	
	UABE_Generic_API size_t getEntryCount();

public:
	class DecompressTask : public ITask
	{
		std::shared_ptr<BundleFileContextInfo> pFileContextInfo; //Maintain object lifetime.

		std::string name;
		std::string outputPath;
		class AppContext &appContext;
	public:
		EBundleFileDecompressStatus decompressStatus;
		EBundleFileOpenStatus reopenStatus;
		DecompressTask(class AppContext &appContext, std::shared_ptr<BundleFileContextInfo> &pContextInfo, std::string outputPath);
		const std::string &getName();
		TaskResult execute(TaskProgressManager &progressManager);
		inline std::shared_ptr<BundleFileContextInfo> &getFileContextInfo()
		{
			return pFileContextInfo;
		}
	};
	//Creates and enqueues a task that decompresses and then reopens the bundle file, replacing the BundleFileContext of this instance.
	//Must be called from the main app thread. This instance or its BundleFileContext must not be used from other threads while the task is running.
	UABE_Generic_API std::shared_ptr<DecompressTask> EnqueueDecompressTask(AppContext &appContext, std::shared_ptr<BundleFileContextInfo> &selfPointer, std::string outputPath);

	friend class AppContext;
};

class ResourcesFileContextInfo : public FileContextInfo
{
	ResourcesFileContext *pContext;
	std::shared_mutex resourcesMutex;
	std::list<ReplacedResourceDesc> resources;
	QWORD originalFileSize;
	std::atomic_bool changedFlag;
public:
	UABE_Generic_API ResourcesFileContextInfo(ResourcesFileContext *pContext, unsigned int fileID, unsigned int parentFileID);
	~ResourcesFileContextInfo();
	IFileContext *getFileContext();
	void getChildFileIDs(std::vector<unsigned int> &childFileIDs);
	void onCloseChild(unsigned int childFileID);
	bool hasNewChanges(class AppContext &appContext);
	bool hasAnyChanges(class AppContext &appContext);
	uint64_t write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag);
	std::unique_ptr<BundleReplacer> makeBundleReplacer(class AppContext &appContext, const char *oldName, const char *newName, uint32_t bundleIndex, bool resetChangedFlag);
	inline ResourcesFileContext *getResourcesFileContext() { return pContext; }

	UABE_Generic_API bool setByReplacer(class AppContext& appContext, BundleReplacer* pReplacer);
	//Adds a new resource to this object and marks the file as changed unless size == 0.
	//resourcesFilePos: The start position where the given resource is to be stored.
	//Note: The reader reference will be stored in this object's resource list, possibly for the lifetime of this object.
	//      The reader will be queried as needed (for writing or by the reader from getResource(..)).
	UABE_Generic_API void addResource(std::shared_ptr<IAssetsReader> pReader, uint64_t readerOffs, uint64_t size, uint64_t &resourcesFilePos);
	//Returns a reader for a given range in the resources file represented by this object.
	//The reader is a snapshot of the resource data at about call-time,
	// and includes all unsaved changes such as added resources.
	//Set size to numeric_limits<uint64_t>::max() to capture all the resource data.
	//Returns nullptr if no resource data is in the given range.
	//Note: The returned reader uses a mutex in Read, SetPosition, Tell, Seek calls. 
	//      For proper multi threading, CreateView should be used, as the views each use an indepentent mutex.
	UABE_Generic_API std::shared_ptr<IAssetsReader> getResource(std::shared_ptr<ResourcesFileContextInfo> selfRef, uint64_t offs, uint64_t size);
};

class GenericFileContextInfo : public FileContextInfo
{
	GenericFileContext *pContext;
	std::mutex replacementReaderMutex;
	//The last entry of this vector is the current replacement reader.
	//The vector ensures that each IAssetsReader indirectly returned through makeBundleReplacer
	// remains valid as long as some GenericFileContextInfo is valid.
	//TODO: Add some mechanism (e.g. a BundleReplacer wrapper) to free any outdated, no longer used readers
	// -> e.g. have the BundleReplacer keep some shared_ptr<IAssetsReader>,
	//    and only store the latest shared_ptr<IAssetsReader> here.
	std::vector<IAssetsReader_ptr> replacementReaderHistory;
	bool changedFlag;
public:
	UABE_Generic_API GenericFileContextInfo(GenericFileContext *pContext, unsigned int fileID, unsigned int parentFileID);
	~GenericFileContextInfo();
	IFileContext *getFileContext();
	void getChildFileIDs(std::vector<unsigned int> &childFileIDs);
	void onCloseChild(unsigned int childFileID);
	bool hasNewChanges(class AppContext &appContext);
	bool hasAnyChanges(class AppContext &appContext);
	uint64_t write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag);
	std::unique_ptr<BundleReplacer> makeBundleReplacer(class AppContext &appContext, const char *oldName, const char *newName, uint32_t bundleIndex, bool resetChangedFlag);
	inline GenericFileContext *getGenericFileContext() { return pContext; }
};

//#include "AppContext.h"