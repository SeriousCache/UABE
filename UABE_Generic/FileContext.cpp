#include "FileContext.h"
#include "../libStringConverter/convert.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shlwapi.h>
#include <assert.h>

IFileOpenCallback::IFileOpenCallback()
{}
IFileOpenCallback::~IFileOpenCallback()
{}

IFileContext::IFileContext(const std::string &filePath, IFileContext *pParent)
{
	this->filePath.assign(filePath);
	if (pParent != nullptr)
		this->fileName.assign(filePath);
	else
	{
		const char *fullPathC = this->filePath.c_str();
		size_t fileNameIndex;
		for (fileNameIndex = this->filePath.size(); fileNameIndex > 0; fileNameIndex--)
		{
			if (fullPathC[fileNameIndex-1] == '/' || fullPathC[fileNameIndex-1] == '\\')
				break;
		}
		this->fileName.assign(this->filePath.substr(fileNameIndex));
	}
	this->pParent = pParent;
}
IFileContext::~IFileContext()
{}

const std::string &IFileContext::getFileName()
{
	return this->fileName;
}
const std::string &IFileContext::getFilePath()
{
	return this->filePath;
}
std::string IFileContext::getFileDirectoryPath()
{
	size_t slash = std::string::npos; size_t slashA = this->filePath.rfind('/'); size_t slashB = this->filePath.rfind('\\');
	if (slashA != std::string::npos && (slashB == std::string::npos || slashB <= slashA))
		slash = slashA;
	else if (slashB != std::string::npos)
		slash = slashB;
	return this->filePath.substr(0, slash);
}
IFileContext *IFileContext::getParent()
{
	return this->pParent;
}

#pragma region BundleFileContext
BundleFileContext::OpenTaskCallback::OpenTaskCallback(BundleFileContext *pContext)
	: pContext(pContext)
{}
void BundleFileContext::OpenTaskCallback::OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result)
{
	if (result >= 0)
		this->pContext->openState.OnCompletion();
	else
		this->pContext->openState.OnFailure();
	if (this->pContext->pOpenCallback)
		this->pContext->pOpenCallback->OnFileOpenResult(this->pContext, result);
}

BundleFileContext::OpenTask::OpenTask(BundleFileContext *pContext)
	: pContext(pContext)
{
	name = "Open bundle : " + pContext->fileName;
}
const std::string &BundleFileContext::OpenTask::getName()
{
	return name;
}
TaskResult BundleFileContext::OpenTask::execute(TaskProgressManager &progressManager)
{
	return pContext->OpenSync(&progressManager);
}

BundleFileContext::BundleFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, nullptr),
	openTask(this), openTaskCallback(this),
	pOpenCallback(nullptr), pDecompressCallback(nullptr),
	pReader(std::move(_pReader)), inheritReader(pReader != nullptr), readerIsModified(pReader != nullptr && readerIsModified),
	lastOpenStatus(BundleFileOpenStatus_OK), lastDecompressStatus(BundleFileDecompressStatus_OK)
{
}
BundleFileContext::BundleFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, pParent),
	openTask(this), openTaskCallback(this),
	pOpenCallback(nullptr), pDecompressCallback(nullptr),
	pReader(std::move(_pReader)), inheritReader(true), readerIsModified(pReader != nullptr && readerIsModified),
	lastOpenStatus(BundleFileOpenStatus_OK), lastDecompressStatus(BundleFileDecompressStatus_OK)
{
	assert(pParent && this->pReader);
}
BundleFileContext::~BundleFileContext()
{
	this->Close();
}

EBundleFileOpenStatus BundleFileContext::OpenSync(TaskProgressManager *pProgressManager, unsigned int initProgress, unsigned int progressScale)
{
	if (pProgressManager) pProgressManager->setProgress(initProgress, progressScale);
	if (!this->inheritReader)
	{
		IAssetsReader *pReader = Create_AssetsReaderFromFile(this->filePath.c_str(), true, RWOpenFlags_Immediately);
		if (pReader == nullptr)
		{
			if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
			if (pProgressManager) pProgressManager->logMessage("[ERROR] Unable to open the bundle file.");
			return BundleFileOpenStatus_ErrFileOpen;
		}
		this->pReader = std::shared_ptr<IAssetsReader>(pReader, Free_AssetsReader);
	}
	else
		assert(this->pReader != nullptr);
	if (pProgressManager) pProgressManager->setProgress(initProgress + 25, progressScale);
	if (pProgressManager) pProgressManager->setProgressDesc("Processing bundle file");
	if (!this->bundle.Read(pReader.get(), nullptr, true))
	{
		if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
		if (pProgressManager) pProgressManager->logMessage("Open as bundle: [ERROR] Unable to process the bundle file header or lists.");
		this->pReader.reset();
		return BundleFileOpenStatus_ErrInvalid;
	}
	if (pProgressManager) pProgressManager->setProgressDesc("Processing bundle directory");
	if (this->bundle.bundleHeader3.fileVersion >= 6)
	{
		if (!this->bundle.bundleInf6)
		{
			if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
			if ((this->bundle.bundleHeader6.flags & 0x3F) != 0)
				return BundleFileOpenStatus_CompressedDirectory;
			if (pProgressManager) pProgressManager->logMessage("[ERROR] Unable to process the bundle directory.");
			this->bundle.Close();
			this->pReader.reset();
			return BundleFileOpenStatus_ErrInvalid;
		}
		else
		{
			for (DWORD i = 0; i < this->bundle.bundleInf6->blockCount; i++)
			{
				if ((this->bundle.bundleInf6->blockInf[i].flags & 0x3F) != 0)
				{
					if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
					return BundleFileOpenStatus_CompressedData;
				}
			}
			if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
			return BundleFileOpenStatus_OK;
		}
	}
	else if (this->bundle.bundleHeader3.fileVersion == 3)
	{
		if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
		if (!strcmp(this->bundle.bundleHeader3.signature, "UnityWeb"))
			return BundleFileOpenStatus_CompressedDirectory;
		else if (!this->bundle.assetsLists3)
		{
			if (pProgressManager) pProgressManager->logMessage("[ERROR] Unable to process the bundle directory.");
			this->bundle.Close();
			this->pReader.reset();
			return BundleFileOpenStatus_ErrInvalid;
		}
		else
			return BundleFileOpenStatus_OK;
	}
	else
	{
		if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
		if (pProgressManager) pProgressManager->logMessage("Open as bundle: [ERROR] Unknown bundle file version.");
		this->bundle.Close();
		this->pReader.reset();
		return BundleFileOpenStatus_ErrUnknownVersion;
	}
}

EBundleFileOpenStatus BundleFileContext::Open()
{
	if (this->openState.Start())
	{
		EBundleFileOpenStatus ret = OpenSync(nullptr);
		if (ret >= 0)
			this->openState.OnCompletion();
		else
			this->openState.OnFailure();
		return ret;
	}
	if (this->openState.isReady())
		return lastOpenStatus;
	return BundleFileOpenStatus_Pend;
}
EBundleFileOpenStatus BundleFileContext::OpenInsideTask(TaskProgressManager *pProgressManager, unsigned int initProgress, unsigned int progressScale)
{
	if (this->openState.Start())
	{
		EBundleFileOpenStatus ret = OpenSync(pProgressManager, initProgress, progressScale);
		if (ret >= 0)
			this->openState.OnCompletion();
		else
			this->openState.OnFailure();
		return ret;
	}
	if (this->openState.isReady())
		return lastOpenStatus;
	return BundleFileOpenStatus_Pend;
}

void BundleFileContext::Close()
{
	this->decompressState.Close();
	if (this->openState.Close())
	{
		bundle.Close();
		pReader.reset();
		//TODO: Free any open resources.
	}
}

EBundleFileDecompressStatus BundleFileContext::DecompressSync(TaskProgressManager *pProgressManager, const std::string &outPath)
{
	if (!this->openState.isReady())
		return BundleFileDecompressStatus_ErrBundleNotOpened;
	IAssetsWriter *pWriter = Create_AssetsWriterToFile(outPath.c_str(), true, true, RWOpenFlags_Immediately);
	if (!pWriter)
		return BundleFileDecompressStatus_ErrOutFileOpen;
	EBundleFileDecompressStatus ret = BundleFileDecompressStatus_OK;
	if (!bundle.Unpack(this->pReader.get(), pWriter))
		ret = BundleFileDecompressStatus_ErrDecompress;
	Free_AssetsWriter(pWriter);
	return ret;
}

IAssetsReader *BundleFileContext::getReaderUnsafe(bool *isInherited)
{
	if (isInherited)
		*isInherited = this->inheritReader;
	if (this->openState.isReady())
		return this->pReader.get();
	return nullptr;
}
AssetBundleFile *BundleFileContext::getBundleFile()
{
	if (this->openState.isReady())
		return &this->bundle;
	return nullptr;
}
//For v3 bundles: Returns false. For v6 bundles: Returns the directory flag "has serialized data".
//-> If true, the file is supposed to be an .assets file.
bool BundleFileContext::hasSerializedData(size_t index)
{
	if (this->openState.isReady())
	{
		if (index >= getEntryCount())
			return false;
		if (this->bundle.bundleHeader6.fileVersion >= 6)
			return (this->bundle.bundleInf6->dirInf[index].flags & 4) != 0;
	}
	return false;
}
std::shared_ptr<IAssetsReader> BundleFileContext::makeEntryReader(size_t index)
{
	if (this->openState.isReady())
	{
		if (index >= getEntryCount())
			return nullptr;
		if (this->bundle.bundleHeader6.fileVersion >= 6)
			return std::shared_ptr<IAssetsReader>(
				this->bundle.MakeAssetsFileReader(this->pReader.get(), &this->bundle.bundleInf6->dirInf[index]),
				FreeAssetBundle_FileReader);
		else if (this->bundle.bundleHeader6.fileVersion == 3)
			return std::shared_ptr<IAssetsReader>(
				this->bundle.MakeAssetsFileReader(this->pReader.get(), this->bundle.assetsLists3->ppEntries[index]),
				FreeAssetBundle_FileReader);
	}
	return nullptr;
}
const char *BundleFileContext::getEntryName(size_t index)
{
	if (this->openState.isReady())
	{
		if (index >= getEntryCount())
			return nullptr;
		if (this->bundle.bundleHeader6.fileVersion >= 6)
			return this->bundle.bundleInf6->dirInf[index].name;
		else if (this->bundle.bundleHeader6.fileVersion == 3)
			return this->bundle.assetsLists3->ppEntries[index]->name;
	}
	return nullptr;
}
size_t BundleFileContext::getEntryCount()
{
	if (this->openState.isReady())
	{
		if (this->bundle.bundleHeader6.fileVersion >= 6 && this->bundle.bundleInf6 != nullptr)
			return this->bundle.bundleInf6->directoryCount;
		else if (this->bundle.bundleHeader6.fileVersion == 3 && this->bundle.assetsLists3 != nullptr)
			return this->bundle.assetsLists3->count;
	}
	return 0;
}

EFileContextType BundleFileContext::getType()
{
	return FileContext_Bundle;
}
#pragma endregion BundleFileContext

#pragma region AssetsFileContext
AssetsFileContext::OpenTaskCallback::OpenTaskCallback(AssetsFileContext *pContext)
	: pContext(pContext)
{}
void AssetsFileContext::OpenTaskCallback::OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result)
{
	if (result >= 0)
		this->pContext->openState.OnCompletion();
	else
		this->pContext->openState.OnFailure();
	if (this->pContext->pOpenCallback)
		this->pContext->pOpenCallback->OnFileOpenResult(this->pContext, result);
}

AssetsFileContext::OpenTask::OpenTask(AssetsFileContext *pContext)
	: pContext(pContext)
{
	name = "Open .assets : " + pContext->fileName;
}
const std::string &AssetsFileContext::OpenTask::getName()
{
	return name;
}
TaskResult AssetsFileContext::OpenTask::execute(TaskProgressManager &progressManager)
{
	return pContext->OpenSync(&progressManager, pContext->doMakeBinaryTable, 0, pContext->getProgressScale());
}

AssetsFileContext::AssetsFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, nullptr),
	openTask(this), openTaskCallback(this), pOpenCallback(nullptr),
	pReader(std::move(_pReader)), inheritReader(pReader != nullptr), readerIsModified(pReader != nullptr && readerIsModified),
	lastOpenStatus(AssetsFileOpenStatus_OK), pAssetsFile(nullptr), pAssetsFileTable(nullptr)
{}
AssetsFileContext::AssetsFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, pParent),
	openTask(this), openTaskCallback(this), pOpenCallback(nullptr),
	pReader(std::move(_pReader)), inheritReader(true), readerIsModified(pReader != nullptr && readerIsModified),
	lastOpenStatus(AssetsFileOpenStatus_OK), pAssetsFile(nullptr), pAssetsFileTable(nullptr)
{
	assert(pParent && this->pReader);
}
AssetsFileContext::~AssetsFileContext()
{
	this->Close();
}

int AssetsFileContext::getProgressScale()
{
	return 100;
}
EAssetsFileOpenStatus AssetsFileContext::OpenSync(TaskProgressManager *pProgressManager, bool makeBinaryTable, unsigned int initProgress, unsigned int progressScale)
{
	if (pProgressManager) pProgressManager->setProgress(initProgress, progressScale);
	if (!this->inheritReader)
	{
		IAssetsReader *pReader = Create_AssetsReaderFromFile(this->filePath.c_str(), true, RWOpenFlags_Immediately);
		if (pReader == nullptr)
		{
			if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
			if (pProgressManager) pProgressManager->logMessage("[ERROR] Unable to open the .assets file.");
			return AssetsFileOpenStatus_ErrFileOpen;
		}
		this->pReader = std::shared_ptr<IAssetsReader>(pReader, Free_AssetsReader);
	}
	else
		assert(this->pReader != nullptr);
	if (pProgressManager) pProgressManager->setProgress(initProgress + (makeBinaryTable ? 15 : 25), progressScale);
	if (pProgressManager) pProgressManager->setProgressDesc("Processing .assets file");
	AssetsFile *pAssetsFile = new AssetsFile(this->pReader.get());
	if (!pAssetsFile->VerifyAssetsFile())
	{
		delete pAssetsFile;
		this->pReader.reset();
		if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
		if (pProgressManager) pProgressManager->logMessage("Open as .assets: [ERROR] .assets file is invalid or unsupported.");
		return AssetsFileOpenStatus_ErrInvalidOrUnsupported;
	}
	this->pAssetsFile = pAssetsFile;
	if (pProgressManager) pProgressManager->setProgress(initProgress + (makeBinaryTable ? 25 : 50), progressScale);
	if (pProgressManager) pProgressManager->setProgressDesc("Processing asset list");
	
	this->pAssetsFileTable = new AssetsFileTable(pAssetsFile);
	if (makeBinaryTable)
	{
		if (pProgressManager) pProgressManager->setProgress(initProgress + 50, progressScale);
		if (pProgressManager) pProgressManager->setProgressDesc("Generating asset lookup tree");
		if (!this->pAssetsFileTable->GenerateQuickLookupTree())
		{
			if (pProgressManager) pProgressManager->logMessage("[WARNING] Failed to generate the asset quick lookup tree.");
		}
	}
	
	if (pProgressManager) pProgressManager->setProgress(initProgress + 100, progressScale);
	return AssetsFileOpenStatus_OK;
}

EAssetsFileOpenStatus AssetsFileContext::Open(bool makeBinaryTable)
{
	if (this->openState.Start())
	{
		EAssetsFileOpenStatus ret = OpenSync(nullptr, makeBinaryTable, 0, this->getProgressScale());
		if (ret >= 0)
			this->openState.OnCompletion();
		else
			this->openState.OnFailure();
		return ret;
	}
	if (this->openState.isReady())
		return lastOpenStatus;
	return AssetsFileOpenStatus_Pend;
}
EAssetsFileOpenStatus AssetsFileContext::OpenInsideTask(TaskProgressManager *pProgressManager, bool makeBinaryTable, unsigned int initProgress, unsigned int progressScale)
{
	if (this->openState.Start())
	{
		EAssetsFileOpenStatus ret = OpenSync(pProgressManager, makeBinaryTable, initProgress, progressScale);
		if (ret >= 0)
			this->openState.OnCompletion();
		else
			this->openState.OnFailure();
		return ret;
	}
	if (this->openState.isReady())
		return lastOpenStatus;
	return AssetsFileOpenStatus_Pend;
}
void AssetsFileContext::Close()
{
	if (this->openState.Close())
	{
		delete this->pAssetsFile;
		delete this->pAssetsFileTable;
		this->pAssetsFile = nullptr;
		this->pAssetsFileTable = nullptr;
		this->pReader.reset();
	}
}
IAssetsReader *AssetsFileContext::getReaderUnsafe(bool *isInherited)
{
	if (isInherited)
		*isInherited = this->inheritReader;
	if (this->openState.isReady())
		return this->pReader.get();
	return nullptr;
}
IAssetsReader *AssetsFileContext::createReaderView(bool *isInherited)
{
	IAssetsReader *pReader = getReaderUnsafe(isInherited);
	if (pReader)
		return pReader->CreateView();
	return nullptr;
}
AssetsFile *AssetsFileContext::getAssetsFile()
{
	if (this->openState.isReady())
		return this->pAssetsFile;
	return nullptr;
}
AssetsFileTable *AssetsFileContext::getAssetsFileTable()
{
	if (this->openState.isReady())
		return this->pAssetsFileTable;
	return nullptr;
}

EFileContextType AssetsFileContext::getType()
{
	return FileContext_Assets;
}
#pragma endregion AssetsFileContext

#pragma region ResourcesFileContext
ResourcesFileContext::ResourcesFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, nullptr),
	pReader(std::move(_pReader)), inheritReader(pReader != nullptr), readerIsModified(pReader != nullptr && readerIsModified)
{
}
ResourcesFileContext::ResourcesFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, pParent),
	pReader(std::move(_pReader)), inheritReader(true), readerIsModified(pReader != nullptr && readerIsModified)
{
	assert(pParent && this->pReader);
}
ResourcesFileContext::~ResourcesFileContext()
{
	this->Close();
}
EResourcesFileOpenStatus ResourcesFileContext::Open()
{
	if (!this->inheritReader)
	{
		IAssetsReader *pReader = Create_AssetsReaderFromFile(this->filePath.c_str(), true, RWOpenFlags_Immediately);
		if (pReader == nullptr)
		{
			return ResourcesFileOpenStatus_ErrFileOpen;
		}
		this->pReader = std::shared_ptr<IAssetsReader>(pReader, Free_AssetsReader);
	}
	else
		assert(this->pReader != nullptr);
	return ResourcesFileOpenStatus_OK;
}
void ResourcesFileContext::Close()
{
	this->pReader.reset();
}
IAssetsReader *ResourcesFileContext::getReaderUnsafe(bool *isInherited)
{
	if (isInherited)
		*isInherited = this->inheritReader;
	return this->pReader.get();
}
EFileContextType ResourcesFileContext::getType()
{
	return FileContext_Resources;
}
#pragma endregion ResourcesFileContext



#pragma region GenericFileContext
GenericFileContext::GenericFileContext(const std::string &filePath, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, nullptr),
	pReader(std::move(_pReader)), inheritReader(pReader != nullptr), readerIsModified(pReader != nullptr && readerIsModified)
{
}
GenericFileContext::GenericFileContext(const std::string &filePath, IFileContext *pParent, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified)
	: IFileContext(filePath, pParent),
	pReader(std::move(_pReader)), inheritReader(true), readerIsModified(pReader != nullptr && readerIsModified)
{
	assert(pParent && this->pReader);
}
GenericFileContext::~GenericFileContext()
{
	this->Close();
}
EGenericFileOpenStatus GenericFileContext::Open()
{
	if (!this->inheritReader)
	{
		IAssetsReader *pReader = Create_AssetsReaderFromFile(this->filePath.c_str(), true, RWOpenFlags_Immediately);
		if (pReader == nullptr)
		{
			return GenericFileOpenStatus_ErrFileOpen;
		}
		this->pReader = std::shared_ptr<IAssetsReader>(pReader, Free_AssetsReader);
	}
	else
		assert(this->pReader != nullptr);
	return GenericFileOpenStatus_OK;
}
void GenericFileContext::Close()
{
	this->pReader.reset();
}
IAssetsReader *GenericFileContext::getReaderUnsafe(bool *isInherited)
{
	if (isInherited)
		*isInherited = this->inheritReader;
	return this->pReader.get();
}
EFileContextType GenericFileContext::getType()
{
	return FileContext_Generic;
}
#pragma endregion GenericFileContext

#pragma region AsyncOperationState
AsyncOperationState::AsyncOperationState()
{
	this->state.val = 0;
	this->hOperationCompleteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}
AsyncOperationState::~AsyncOperationState()
{
	CloseHandle(this->hOperationCompleteEvent);
}
//Sets the isWorking flag if neither isWorking nor isReady is set, and returns true. Returns false otherwise.
bool AsyncOperationState::Start()
{
	State oldState; oldState.val = this->state.val;
	if (!oldState.isWorking && !oldState.isReady)
	{
		State newState = {};
		newState.isReady = false; newState.isWorking = true;
		oldState.val = InterlockedExchange(&this->state.val, newState.val);
		if (oldState.isWorking || oldState.isReady)
		{
			oldState.val = InterlockedExchange(&this->state.val, oldState.val);
			if (oldState.isReady)
			{
				this->state.isReady = true;
				std::atomic_thread_fence(std::memory_order::seq_cst);
				this->state.isWorking = false;
			}
			return false;
		}
		else
		{
			return true;
		}
	}
	return false;
}
//Waits for completion if it is in the working state. Resets the ready flag if necessary, returns true if any open resources should be freed.
bool AsyncOperationState::Close()
{
	if (this->state.isWorking)
		WaitForSingleObject(this->hOperationCompleteEvent, INFINITE);
	if (this->state.isReady)
	{
		this->state.isReady = false;
		ResetEvent(this->hOperationCompleteEvent);
		return true;
	}
	return false;
}
void AsyncOperationState::OnCompletion()
{
	this->state.isReady = true;
	std::atomic_thread_fence(std::memory_order::seq_cst);
	this->state.isWorking = false;
	SetEvent(this->hOperationCompleteEvent);
}
void AsyncOperationState::OnFailure()
{
	this->state.isWorking = false;
	SetEvent(this->hOperationCompleteEvent);
}
#pragma endregion AsyncOperationState