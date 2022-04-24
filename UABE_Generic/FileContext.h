#pragma once
#include "api.h"
#include <string>
#include "AsyncTask.h"
#include "../AssetsTools/AssetBundleFileFormat.h"
#include "../AssetsTools/AssetsFileFormat.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "../AssetsTools/AssetsFileReader.h"

class IFileContext;
class IFileOpenCallback
{
public:
	UABE_Generic_API IFileOpenCallback();
	UABE_Generic_API virtual ~IFileOpenCallback();
	UABE_Generic_API virtual void OnFileOpenResult(IFileContext *pContext, int result)=0;
};

enum EFileContextType
{
	FileContext_Bundle=0,
	FileContext_Assets=1,
	FileContext_Resources=2,
	FileContext_Generic=3,
	FileContext_COUNT
};
class IFileContext
{
protected:
	std::string fileName;
	std::string filePath;
	IFileContext *pParent;
public:
	UABE_Generic_API IFileContext(const std::string &filePath, IFileContext *pParent);
	UABE_Generic_API virtual ~IFileContext();
	UABE_Generic_API virtual EFileContextType getType()=0;
	UABE_Generic_API virtual IFileContext *getParent();

	UABE_Generic_API const std::string& getFileName(); //UTF-8; May return getFilePath(), e.g. if it has a parent.
	UABE_Generic_API const std::string& getFilePath(); //UTF-8
	UABE_Generic_API std::string getFileDirectoryPath(); //UTF-8
};

class AsyncOperationState
{
public:
#ifndef WINAPI
#define _TMPDEF_WINAPI
#define HANDLE void*
#endif
	HANDLE hOperationCompleteEvent;
#ifdef _TMPDEF_WINAPI
#undef _TMPDEF_WINAPI
#undef HANDLE
#endif
	union State {
		struct {
			bool isWorking;
			bool isReady;
		};
		unsigned int val;
	} state;
	UABE_Generic_API AsyncOperationState();
	UABE_Generic_API ~AsyncOperationState();
	//Sets the isWorking flag if neither isWorking nor isReady is set, and returns true. Returns false otherwise.
	UABE_Generic_API bool Start();
	//Waits for completion if it is in the working state. Resets the ready flag if necessary, returns true if any open resources should be freed.
	UABE_Generic_API bool Close();
	UABE_Generic_API void OnCompletion();
	UABE_Generic_API void OnFailure();
	inline bool isReady()
	{
		return this->state.isReady;
	}
};

enum EBundleFileOpenStatus
{
	BundleFileOpenStatus_ErrUnknownVersion=-3,
	BundleFileOpenStatus_ErrInvalid=-2,
	BundleFileOpenStatus_ErrFileOpen=-1,
	BundleFileOpenStatus_OK=0,
	BundleFileOpenStatus_Pend=1,
	BundleFileOpenStatus_CompressedDirectory=2, //The block&directory list is compressed and has not been processed. The context is considered open.
	BundleFileOpenStatus_CompressedData=3, //Reading succeeded but there are compressed blocks.
};
enum EBundleFileDecompressStatus
{
	BundleFileDecompressStatus_ErrBundleNotOpened=-3,
	BundleFileDecompressStatus_ErrDecompress=-2,
	BundleFileDecompressStatus_ErrOutFileOpen=-1,
	BundleFileDecompressStatus_OK=0,
	BundleFileDecompressStatus_Pend=1,
};
class IBundleDecompressCallback : public IFileOpenCallback
{};
class BundleFileContext : public IFileContext
{
	class OpenTaskCallback : public TaskProgressCallback
	{
		BundleFileContext *pContext;
	public:
		OpenTaskCallback(BundleFileContext *pContext);
		void OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result);
	};
	class OpenTask : public ITask
	{
		BundleFileContext *pContext;
		std::string name;
	public:
		OpenTask(BundleFileContext *pContext);
		const std::string &getName();
		TaskResult execute(TaskProgressManager &progressManager);
	};
	std::shared_ptr<IAssetsReader> pReader; bool inheritReader; bool readerIsModified;
	AssetBundleFile bundle;

	AsyncOperationState openState;
	AsyncOperationState decompressState;

	EBundleFileOpenStatus lastOpenStatus;
	OpenTaskCallback openTaskCallback;
	OpenTask openTask;

	EBundleFileDecompressStatus lastDecompressStatus;

	IFileOpenCallback *pOpenCallback;
	IBundleDecompressCallback *pDecompressCallback;

	EBundleFileOpenStatus OpenSync(TaskProgressManager *pProgressManager, unsigned int initProgress = 0, unsigned int progressScale = 100);
	EBundleFileDecompressStatus DecompressSync(TaskProgressManager *pProgressManager, const std::string &outPath);

	//For v3 bundles: Returns false. For v6 bundles: Returns the directory flag "has serialized data".
	//-> If true, the file is supposed to be an .assets file.
	bool hasSerializedData(size_t index);
public:
	UABE_Generic_API BundleFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> pReader = nullptr, bool readerIsModified = false);
	UABE_Generic_API BundleFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> pReader, bool readerIsModified = false);
	~BundleFileContext();

	UABE_Generic_API EBundleFileOpenStatus Open();
	//Run the open task inside another task. Takes up 100 progress units, starting with initProgress. Requires progressScale >= initProgress + 100. 
	UABE_Generic_API EBundleFileOpenStatus OpenInsideTask(TaskProgressManager *pProgressManager, unsigned int initProgress = 0, unsigned int progressScale = 100);

	UABE_Generic_API void Close();
	
	//Returns the internal reader. While the Read function itself is thread-safe, the internal reader position must not be relied on.
	//Returns null if the context has not been opened.
	UABE_Generic_API IAssetsReader *getReaderUnsafe(bool *isInherited = nullptr);
	//Creates a view of the internal reader for thread-safe access that can be freed with Free_AssetsReader.
	//Note that the reader view must not be used on several threads in parallel. Further CreateView calls on the view are required in that case.
	//Returns null if the context has not been opened or if creating a view failed.
	inline IAssetsReader *createReaderView(bool *isInherited = nullptr)
	{
		IAssetsReader *pReader = getReaderUnsafe(isInherited);
		if (pReader)
			return pReader->CreateView();
		return nullptr;
	}

	//Returns null if the context has not been opened.
	UABE_Generic_API AssetBundleFile *getBundleFile();
	//Returns nullptr if either the context has not been opened or the bundle is compressed (or I/O errors).
	UABE_Generic_API std::shared_ptr<IAssetsReader> makeEntryReader(size_t index);
	//Returns null if either the context has not been opened or the file table is compressed.
	UABE_Generic_API const char *getEntryName(size_t index);
	//Returns 0 if either no entries exist or the context has not been opened.
	UABE_Generic_API size_t getEntryCount();
	
	inline void setFreeReaderOnClose(bool free)
	{
		inheritReader = !free;
	}

	inline bool getReaderIsModified()
	{
		return readerIsModified;
	}

	EFileContextType getType();

	friend class BundleFileContextInfo;
};

enum EAssetsFileOpenStatus
{
	AssetsFileOpenStatus_ErrInvalidOrUnsupported=-2,
	AssetsFileOpenStatus_ErrFileOpen=-1,
	AssetsFileOpenStatus_OK=0,
	AssetsFileOpenStatus_Pend=1,
};
class AssetsFileContext : public IFileContext
{
	class OpenTaskCallback : public TaskProgressCallback
	{
		AssetsFileContext *pContext;
	public:
		OpenTaskCallback(AssetsFileContext *pContext);
		void OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result);
	};
	class OpenTask : public ITask
	{
		AssetsFileContext *pContext;
		std::string name;
	public:
		OpenTask(AssetsFileContext *pContext);
		const std::string &getName();
		TaskResult execute(TaskProgressManager &progressManager);
	};
	std::shared_ptr<IAssetsReader> pReader; bool inheritReader; bool readerIsModified;
	AssetsFile *pAssetsFile;
	AssetsFileTable *pAssetsFileTable;

	AsyncOperationState openState;

	EAssetsFileOpenStatus lastOpenStatus;
	OpenTaskCallback openTaskCallback;
	OpenTask openTask;

	IFileOpenCallback *pOpenCallback;
	bool doMakeBinaryTable;

	//The "total" amount of work to be done. 100 is the amount of work AssetsFileContext does.
	// Return a higher number in a deriving class in order to extend the progress scale for additional OpenSync functionality.
	virtual int getProgressScale();
	virtual EAssetsFileOpenStatus OpenSync(TaskProgressManager *pProgressManager, bool makeBinaryTable, unsigned int initProgress, unsigned int progressScale);
public:
	UABE_Generic_API AssetsFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> pReader = nullptr, bool readerIsModified = false);
	UABE_Generic_API AssetsFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> pReader, bool readerIsModified = false);
	~AssetsFileContext();

	UABE_Generic_API EAssetsFileOpenStatus Open(bool makeBinaryTable);
	//Run the open task inside another task. Takes up 100 progress units, starting with initProgress. Requires progressScale >= initProgress + 100. 
	UABE_Generic_API EAssetsFileOpenStatus OpenInsideTask(TaskProgressManager *pProgressManager, bool makeBinaryTable, unsigned int initProgress = 0, unsigned int progressScale = 100);

	UABE_Generic_API void Close();
	
	//Returns the internal reader. While the Read function itself is thread-safe, the internal reader position must not be relied on.
	//Returns null if the context has not been opened.
	UABE_Generic_API IAssetsReader *getReaderUnsafe(bool *isInherited = nullptr);
	//Creates a view of the internal reader for thread-safe access that can be freed with Free_AssetsReader.
	//Note that the reader view must not be used on several threads in parallel. Further CreateView calls on the view are required in that case.
	//Returns null if the context has not been opened or if creating a view failed.
	UABE_Generic_API IAssetsReader *createReaderView(bool *isInherited = nullptr);

	//Returns null if the context has not been opened.
	UABE_Generic_API AssetsFile *getAssetsFile();
	//Returns null if the context has not been opened.
	UABE_Generic_API AssetsFileTable *getAssetsFileTable();
	
	inline void setFreeReaderOnClose(bool free)
	{
		inheritReader = !free;
	}

	inline bool getReaderIsModified()
	{
		return readerIsModified;
	}

	EFileContextType getType();
};

enum EResourcesFileOpenStatus
{
	ResourcesFileOpenStatus_ErrFileOpen=-1,
	ResourcesFileOpenStatus_OK=0,
};
class ResourcesFileContext : public IFileContext
{
	std::shared_ptr<IAssetsReader> pReader; bool inheritReader; bool readerIsModified;

public:
	UABE_Generic_API ResourcesFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> pReader = nullptr, bool readerIsModified = false);
	UABE_Generic_API ResourcesFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> pReader, bool readerIsModified = false);
	~ResourcesFileContext();

	UABE_Generic_API EResourcesFileOpenStatus Open();

	UABE_Generic_API void Close();
	
	//Returns the internal reader. While the Read function itself is thread-safe, the internal reader position must not be relied on.
	//Returns null if the context has not been opened.
	UABE_Generic_API IAssetsReader *getReaderUnsafe(bool *isInherited = nullptr);
	//Creates a view of the internal reader for thread-safe access that can be freed with Free_AssetsReader.
	//Note that the reader view must not be used on several threads in parallel. Further CreateView calls on the view are required in that case.
	//Returns null if the context has not been opened or if creating a view failed.
	inline IAssetsReader *createReaderView(bool *isInherited = nullptr)
	{
		IAssetsReader *pReader = getReaderUnsafe(isInherited);
		if (pReader)
			return pReader->CreateView();
		return nullptr;
	}
	
	inline void setFreeReaderOnClose(bool free)
	{
		inheritReader = !free;
	}

	inline bool getReaderIsModified()
	{
		return readerIsModified;
	}

	EFileContextType getType();
};

enum EGenericFileOpenStatus
{
	GenericFileOpenStatus_ErrFileOpen=-1,
	GenericFileOpenStatus_OK=0,
};
class GenericFileContext : public IFileContext
{
	std::shared_ptr<IAssetsReader> pReader; bool inheritReader; bool readerIsModified;
public:
	UABE_Generic_API GenericFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> pReader = nullptr, bool readerIsModified = false);
	UABE_Generic_API GenericFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> pReader, bool readerIsModified = false);
	~GenericFileContext();

	UABE_Generic_API EGenericFileOpenStatus Open();

	UABE_Generic_API void Close();
	
	//Returns the internal reader. While the Read function itself is thread-safe, the internal reader position must not be relied on.
	//Returns null if the context has not been opened.
	UABE_Generic_API IAssetsReader *getReaderUnsafe(bool *isInherited = nullptr);
	//Creates a view of the internal reader for thread-safe access that can be freed with Free_AssetsReader.
	//Note that the reader view must not be used on several threads in parallel. Further CreateView calls on the view are required in that case.
	//Returns null if the context has not been opened or if creating a view failed.
	inline IAssetsReader *createReaderView(bool *isInherited = nullptr)
	{
		IAssetsReader *pReader = getReaderUnsafe(isInherited);
		if (pReader)
			return pReader->CreateView();
		return nullptr;
	}
	
	inline void setFreeReaderOnClose(bool free)
	{
		inheritReader = !free;
	}

	inline bool getReaderIsModified()
	{
		return readerIsModified;
	}

	EFileContextType getType();
};
