#include "AppContext.h"
#include <InternalBundleReplacer.h>
#include <assert.h>
#include <tuple>
#include <filesystem>
#include "../libStringConverter/convert.h"

AppContext::FileOpenTask::FileOpenTask(AppContext *pContext, std::shared_ptr<IAssetsReader> _pReader, bool readerIsModified, const std::string &path,
	unsigned int parentFileID, unsigned int directoryEntryIdx,
	bool tryAsBundle, bool tryAsAssets, bool tryAsResources, bool tryAsGeneric) :
	pContext(pContext), pReader(std::move(_pReader)), readerIsModified(readerIsModified), filePath(path), pFileContext(nullptr),
	parentFileID(parentFileID), directoryEntryIdx(directoryEntryIdx),
	tryAsBundle(tryAsBundle), tryAsAssets(tryAsAssets), tryAsResources(tryAsResources), tryAsGeneric(tryAsGeneric)
{
	name = "Open file: " + path;
}
void AppContext::FileOpenTask::setParentContextInfo(std::shared_ptr<BundleFileContextInfo> &pContextInfo)
{
	this->pParentContextInfo = pContextInfo;
}
const std::string &AppContext::FileOpenTask::getName()
{
	return name;
}
TaskResult AppContext::FileOpenTask::execute(TaskProgressManager &progressManager)
{
	//TODO (maybe) : make this task cancelable
	this->pFileContext = nullptr;
	this->bundleOpenStatus = (EBundleFileOpenStatus)0;
	this->assetsOpenStatus = (EAssetsFileOpenStatus)0;
	if (tryAsBundle)
	{
		progressManager.setProgressDesc("Opening as a bundle file");
		BundleFileContext *pBundleContext = new BundleFileContext(filePath, pReader, readerIsModified);
		EBundleFileOpenStatus bundleOpenStatus = pBundleContext->OpenInsideTask(&progressManager, 0, 200);
		if (bundleOpenStatus >= 0 && bundleOpenStatus != BundleFileOpenStatus_Pend)
		{
			progressManager.setProgress(200, 200);
			this->pFileContext = pBundleContext;
			this->bundleOpenStatus = bundleOpenStatus;
			return 1;
		}
		delete pBundleContext;
	}
	if (tryAsAssets)
	{
		progressManager.setProgressDesc("Opening as a .assets file");
		AssetsFileContext *pAssetsContext = new AssetsFileContext(filePath, pReader, readerIsModified);
		EAssetsFileOpenStatus assetsOpenStatus = pAssetsContext->OpenInsideTask(&progressManager, true, 100, 200);
		if (assetsOpenStatus >= 0 && assetsOpenStatus != AssetsFileOpenStatus_Pend)
		{
			progressManager.setProgress(200, 200);
			this->pFileContext = pAssetsContext;
			this->assetsOpenStatus = assetsOpenStatus;
			return 2;
		}
		delete pAssetsContext;
	}
	if (tryAsResources/* && ((filePath.size() >= 5 && !filePath.compare(filePath.size() - 5, std::string::npos, ".resS"))
		|| (filePath.size() >= 9 && !filePath.compare(filePath.size() - 9, std::string::npos, ".resource"))
		|| (filePath.size() >= 10 && !filePath.compare(filePath.size() - 10, std::string::npos, ".resources")))*/)
	{
		progressManager.setProgressDesc("Opening as a resources file");
		ResourcesFileContext *pResourcesContext = new ResourcesFileContext(filePath, pReader, readerIsModified);
		EResourcesFileOpenStatus resourcesOpenStatus = pResourcesContext->Open();
		if (resourcesOpenStatus >= 0)
		{
			progressManager.setProgress(200, 200);
			this->pFileContext = pResourcesContext;
			return 3;
		}
		delete pResourcesContext;
	}
	if (tryAsGeneric)
	{
		progressManager.setProgressDesc("Opening as a generic file");
		GenericFileContext *pGenericContext = new GenericFileContext(filePath, pReader, readerIsModified);
		EGenericFileOpenStatus genericOpenStatus = pGenericContext->Open();
		if (genericOpenStatus >= 0)
		{
			progressManager.setProgress(200, 200);
			this->pFileContext = pGenericContext;
			return 4;
		}
		delete pGenericContext;
	}
	this->pReader.reset();
	return -1;
}

AppContext::AppContext()
	: taskManager(1), maxFileID(0), lastError(0), autoDetectDependencies(true)
{
	taskManager.addCallback(this);
}
AppContext::~AppContext(void)
{
	//contextInfo.clear();
	//contextInfoByFileID.clear();
}

void AppContext::OnCompletion(std::shared_ptr<ITask> &pTask, TaskResult result)
{
	if (std::shared_ptr<FileOpenTask> pFileOpenTask = std::dynamic_pointer_cast<FileOpenTask>(pTask))
	{
		switch (result)
		{
		case -1:
			signalMainThread(AppContextMsg_OnFileOpenFail, new std::shared_ptr<FileOpenTask>(pFileOpenTask));
			break;
		case 1: //bundle
			signalMainThread(AppContextMsg_OnFileOpenAsBundle, new std::shared_ptr<FileOpenTask>(pFileOpenTask));
			break;
		case 2: //.assets
			signalMainThread(AppContextMsg_OnFileOpenAsAssets, new std::shared_ptr<FileOpenTask>(pFileOpenTask));
			break;
		case 3: //resources
			signalMainThread(AppContextMsg_OnFileOpenAsResources, new std::shared_ptr<FileOpenTask>(pFileOpenTask));
			break;
		case 4: //generic
			signalMainThread(AppContextMsg_OnFileOpenAsGeneric, new std::shared_ptr<FileOpenTask>(pFileOpenTask));
			break;
		default: assert(false);
		}
	}
	else if (auto pContainersTask = std::dynamic_pointer_cast<AssetsFileContextInfo::ContainersTask>(pTask))
	{
		signalMainThread(AppContextMsg_OnContainersLoaded, new std::shared_ptr<AssetsFileContextInfo::ContainersTask>(pContainersTask));
	}
	else if (auto pDecompressTask = std::dynamic_pointer_cast<BundleFileContextInfo::DecompressTask>(pTask))
	{
		signalMainThread(AppContextMsg_OnBundleDecompressed,
			new std::tuple<std::shared_ptr<BundleFileContextInfo::DecompressTask>,TaskResult>(pDecompressTask, result));
	}
}
bool AppContext::processMessage(EAppContextMsg message, void *args)
{
	switch (message)
	{
	case AppContextMsg_OnFileOpenFail:
		{
			auto ppTask = (std::shared_ptr<FileOpenTask>*)args;
			this->OnFileOpenFail(*ppTask, (*ppTask)->logText);
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnFileOpenAsBundle:
		{
			auto ppTask = (std::shared_ptr<FileOpenTask>*)args;
			FileOpenTask *_pTask = (*ppTask).get();
			this->OnFileOpenAsBundle(*ppTask, (BundleFileContext*)_pTask->pFileContext, _pTask->bundleOpenStatus, _pTask->parentFileID, _pTask->directoryEntryIdx);
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnFileOpenAsAssets:
		{
			auto ppTask = (std::shared_ptr<FileOpenTask>*)args;
			FileOpenTask *_pTask = (*ppTask).get();
			this->OnFileOpenAsAssets(*ppTask, (AssetsFileContext*)_pTask->pFileContext, _pTask->assetsOpenStatus, _pTask->parentFileID, _pTask->directoryEntryIdx);
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnFileOpenAsResources:
		{
			auto ppTask = (std::shared_ptr<FileOpenTask>*)args;
			FileOpenTask *_pTask = (*ppTask).get();
			this->OnFileOpenAsResources(*ppTask, (ResourcesFileContext*)_pTask->pFileContext, _pTask->parentFileID, _pTask->directoryEntryIdx);
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnFileOpenAsGeneric:
		{
			auto ppTask = (std::shared_ptr<FileOpenTask>*)args;
			FileOpenTask *_pTask = (*ppTask).get();
			this->OnFileOpenAsGeneric(*ppTask, (GenericFileContext*)_pTask->pFileContext, _pTask->parentFileID, _pTask->directoryEntryIdx);
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnContainersLoaded:
		{
			auto ppTask = (std::shared_ptr<AssetsFileContextInfo::ContainersTask>*)args;
			OnGenerateContainers((*ppTask)->getFileContextInfo());
			delete ppTask;
		}
		return true;
	case AppContextMsg_OnBundleDecompressed:
		{
			auto *pInfo = (std::tuple<std::shared_ptr<BundleFileContextInfo::DecompressTask>,TaskResult>*)args;
			auto pTask = std::get<0>(*pInfo);
			OnDecompressBundle(pTask.get(), std::get<1>(*pInfo));
			delete pInfo;
		}
		return true;
	case AppContextMsg_OnAssetChanged:
		{
			auto *pInfo = (std::tuple<unsigned int,pathid_t,bool>*)args;
			std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
			auto contextInfoIt = contextInfoByFileID.find(std::get<0>(*pInfo));
			if (contextInfoIt != contextInfoByFileID.end())
			{
				std::shared_ptr<AssetsFileContextInfo> pFile = std::dynamic_pointer_cast<AssetsFileContextInfo>(contextInfoIt->second);
				contextInfoMapLock.unlock();
				assert(pFile);
				if (pFile)
					OnChangeAsset(pFile.get(), std::get<1>(*pInfo), std::get<2>(*pInfo));
			}
			delete pInfo;
		}
		return true;
	case AppContextMsg_DoMainThreadCallback:
		{
			auto *pInfo = (std::tuple<void(*)(uintptr_t,uintptr_t), uintptr_t, uintptr_t>*)args;
			std::get<0>(*pInfo)(std::get<1>(*pInfo), std::get<2>(*pInfo));
			delete pInfo;
		}
		return true;
	case AppContextMsg_OnBundleEntryChanged:
		{
			auto *pInfo = (std::tuple<unsigned int,size_t>*)args;
			std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
			auto contextInfoIt = contextInfoByFileID.find(std::get<0>(*pInfo));
			if (contextInfoIt != contextInfoByFileID.end())
			{
				std::shared_ptr<BundleFileContextInfo> pFile = std::dynamic_pointer_cast<BundleFileContextInfo>(contextInfoIt->second);
				contextInfoMapLock.unlock();
				assert(pFile);
				if (pFile)
					OnChangeBundleEntry(pFile.get(), std::get<1>(*pInfo));
			}
			else
				contextInfoMapLock.unlock();
			delete pInfo;
		}
		return true;
	default:
#ifdef _DEBUG
		assert(false);
#endif
		return false;
	}
}
void AppContext::OnDecompressBundle(BundleFileContextInfo::DecompressTask *pTask, TaskResult result)
{}
std::shared_ptr<FileContextInfo> AppContext::OnFileOpenAsBundle(std::shared_ptr<FileOpenTask> pTask, BundleFileContext *pContext, EBundleFileOpenStatus openStatus, unsigned int parentFileID, unsigned int directoryEntryIdx)
{
	BundleFileContextInfo *pBundleInfo = new BundleFileContextInfo(pContext, 0, parentFileID);
	std::shared_ptr<FileContextInfo> pInfo(pBundleInfo);
	
	//Carry on the VisibleFileEntry from the FileOpenTask.
	//-> The bundle file itself applies the replacer modifications
	pBundleInfo->modificationsToApply = std::move(pTask->modificationsToApply);

	AddContextInfo(pInfo, directoryEntryIdx);
	return pInfo;
}
std::shared_ptr<FileContextInfo> AppContext::OnFileOpenAsAssets(std::shared_ptr<FileOpenTask> pTask, AssetsFileContext *pContext, EAssetsFileOpenStatus openStatus, unsigned int parentFileID, unsigned int directoryEntryIdx)
{
	AssetsFileContextInfo *pAssetsInfo = new AssetsFileContextInfo(pContext, 0, parentFileID);
	std::shared_ptr<FileContextInfo> pInfo(pAssetsInfo);

	//Apply all AssetsReplacers for this file from a VisibleFileEntry, if existent.
	for (size_t i = 0; pTask->modificationsToApply && i < pTask->modificationsToApply->replacers.size(); ++i)
	{
		std::shared_ptr<GenericReplacer> &pGenericReplacer = pTask->modificationsToApply->replacers[i].pReplacer;
		if (std::shared_ptr<AssetsReplacer> pAssetsReplacer = std::dynamic_pointer_cast<AssetsReplacer>(pGenericReplacer))
		{
			if (pAssetsReplacer->GetType() == AssetsReplacement_AddOrModify
				|| pAssetsReplacer->GetType() == AssetsReplacement_Remove)
			{
				pAssetsInfo->addReplacer(std::reinterpret_pointer_cast<AssetsEntryReplacer>(pAssetsReplacer), *this, true, false);
			}
			else if (pAssetsReplacer->GetType() == AssetsReplacement_Dependencies)
			{
				AssetsDependenciesReplacer *pReplacer = reinterpret_cast<AssetsDependenciesReplacer*>(pAssetsReplacer.get());
				auto refLock = pAssetsInfo->lockReferencesWrite();
				const std::vector<AssetsFileDependency> &dependencies = pReplacer->GetDependencies();
				pAssetsInfo->getDependenciesWrite(refLock) = dependencies;
				pAssetsInfo->getReferencesWrite(refLock).clear();
				pAssetsInfo->getReferencesWrite(refLock).resize(dependencies.size(), 0);
				pAssetsInfo->setDependenciesChanged();
			}
		}
	}

	AddContextInfo(pInfo, directoryEntryIdx);
	return pInfo;
}
std::shared_ptr<FileContextInfo> AppContext::OnFileOpenAsResources(std::shared_ptr<FileOpenTask> pTask, ResourcesFileContext *pContext, unsigned int parentFileID, unsigned int directoryEntryIdx)
{
	ResourcesFileContextInfo *pResourcesInfo = new ResourcesFileContextInfo(pContext, 0, parentFileID);
	std::shared_ptr<FileContextInfo> pInfo(pResourcesInfo);

	//Apply the BundleEntryModifierByResources for this file from a VisibleFileEntry, if existent.
	if (pTask->modificationsToApply && pTask->modificationsToApply->replacers.size() == 1
		&& dynamic_cast<BundleEntryModifierByResources*>(pTask->modificationsToApply->replacers[0].pReplacer.get()) != nullptr)
	{
		std::shared_ptr<GenericReplacer> pGenericReplacer = pTask->modificationsToApply->replacers[0].pReplacer;
		bool result = pResourcesInfo->setByReplacer(*this, reinterpret_cast<BundleReplacer*>(pGenericReplacer.get()));
		assert(result);
	}

	AddContextInfo(pInfo, directoryEntryIdx);
	return pInfo;
}
std::shared_ptr<FileContextInfo> AppContext::OnFileOpenAsGeneric(std::shared_ptr<FileOpenTask> pTask, GenericFileContext *pContext, unsigned int parentFileID, unsigned int directoryEntryIdx)
{
	GenericFileContextInfo *pGenericInfo = new GenericFileContextInfo(pContext, 0, parentFileID);
	std::shared_ptr<FileContextInfo> pInfo(pGenericInfo);

	AddContextInfo(pInfo, directoryEntryIdx);
	return pInfo;
}
void AppContext::OnFileOpenFail(std::shared_ptr<FileOpenTask> pTask, std::string &logText)
{
}
std::shared_ptr<ITask> AppContext::CreateFileOpenTask(const std::string &path, bool basedOnExistingFile)
{
	std::string actualPath = path;
	std::shared_ptr<IAssetsReader> pReader;
	if (actualPath.size() >= 7 && !actualPath.compare(actualPath.size() - 7, std::string::npos, ".split0"))
	{
		actualPath = actualPath.substr(0, actualPath.size() - 7);
		pReader = std::shared_ptr<IAssetsReader>(
			Create_AssetsReaderFromSplitFile(actualPath.c_str(), true, false, RWOpenFlags_Immediately), 
			Free_AssetsReader);
	}
	else
	{
		pReader = std::shared_ptr<IAssetsReader>(
			Create_AssetsReaderFromFile(actualPath.c_str(), true, RWOpenFlags_Immediately), 
			Free_AssetsReader);
		if (!pReader)
		{
			std::string splitPath = path + ".split0";
			pReader = std::shared_ptr<IAssetsReader>(
				Create_AssetsReaderFromSplitFile(splitPath.c_str(), true, false, RWOpenFlags_Immediately), 
				Free_AssetsReader);
		}
	}
	if (!pReader && !basedOnExistingFile)
	{
		//Create an empty reader.
		pReader = std::shared_ptr<IAssetsReader>(
			Create_AssetsReaderFromMemory(nullptr, 0, false, nullptr),
			Free_AssetsReader);
	}
	if (!pReader)
	{
		lastError = AppContextErr_FileNotFound;
		return nullptr;
	}
	bool tryAsResources = false;
	if ((actualPath.size() >= 5 && !strnicmp(&path.data()[path.size() - 5], ".ress", 5))
		|| (actualPath.size() >= 10 && !strnicmp(&path.data()[path.size() - 10], ".resources", 10))
		|| (actualPath.size() >= 9 && !strnicmp(&path.data()[path.size() - 9], ".resource", 9)))
		tryAsResources = true;
	return std::shared_ptr<ITask>(new FileOpenTask(this, std::move(pReader), false, path, 0, 0, true, true, tryAsResources, false));
}
std::shared_ptr<ITask> AppContext::CreateBundleEntryOpenTask(std::shared_ptr<BundleFileContextInfo> &pBundleContextInfo, unsigned int directoryEntryIdx)
{
	{
		BundleFileContext *pBundleContext = pBundleContextInfo->getBundleFileContext();
		std::string entryName = pBundleContextInfo->getNewEntryName(directoryEntryIdx);
		//pBundleContextInfo->modificationsToApply.replacers
		bool readerIsModified = false;
		std::shared_ptr<IAssetsReader> pChildReader = pBundleContextInfo->makeEntryReader(directoryEntryIdx, readerIsModified);
		if (!pChildReader)
			return nullptr;
		bool tryAsResources = false;
		if ((entryName.size() >= 5 && !entryName.compare(entryName.size() - 5, std::string::npos, ".resS"))
			|| (entryName.size() >= 9 && !entryName.compare(entryName.size() - 9, std::string::npos, ".resource"))
			|| (entryName.size() >= 10 && !entryName.compare(entryName.size() - 10, std::string::npos, ".resources")))
			tryAsResources = true;
		FileOpenTask *ret = new FileOpenTask(this, std::move(pChildReader), readerIsModified, entryName, pBundleContextInfo->getFileID(), directoryEntryIdx, true, true, tryAsResources, true);
		ret->setParentContextInfo(pBundleContextInfo);
		
		if (pBundleContextInfo->modificationsToApply != nullptr)
		{
			//If we have a matching subFile in the VisibleFileEntry, move it to the entry open task.
			// -> Each subFile entry is generated by parsing a BundleReplacer based on an existing file:
			//    => BundleEntryModifierFromAssets
			//    => BundleEntryModifierFromBundle
			//    => BundleEntryModifierByResources if at least one resource has fromOriginalFile set to true.
			// -> Note: BundleFileContextInfo::onDirectoryReady handles BundleEntryModifierByResources replacer entries
			//          by creating a bundle directory entry with an empty reader
			//          and then adding a new subFile entry to modificationsToApply with the same replacer.
			//TODO: Put some faster name lookup table in the BundleFileContextInfo (running this loop for every bundle entry -> O(N²) time).
			for (size_t _i = pBundleContextInfo->modificationsToApply->subFiles.size(); _i > 0; --_i)
			{
				size_t i = _i - 1;
				VisibleFileEntry &subFile = pBundleContextInfo->modificationsToApply->subFiles[i];
				if (!stricmp(entryName.c_str(), subFile.pathNull ? subFile.newName.c_str() : subFile.pathOrName.c_str()))
				{
					this->OpenTask_SetModifications(ret, std::make_unique<VisibleFileEntry>(std::move(subFile)));
					pBundleContextInfo->modificationsToApply->subFiles.erase(pBundleContextInfo->modificationsToApply->subFiles.begin() + i);
				}
			}
		}
		return std::shared_ptr<ITask>(ret);
	}
	return nullptr;
}
void AppContext::OpenTask_SetModifications(ITask *pTask, std::unique_ptr<class VisibleFileEntry> modificationsToApply) 
{
	FileOpenTask *pFileOpenTask = dynamic_cast<FileOpenTask*>(pTask);
	if (pFileOpenTask == nullptr)
		throw std::invalid_argument("OpenTask_SetModifications: pTask is not a FileOpenTask.");
	pFileOpenTask->modificationsToApply = std::move(modificationsToApply);
}
static const char *getDependencyFileName(const char *dependency)
{
	const char *fileName = dependency;
	//if (!strncmp(dependency, "archive:/", 9))
	//	fileName = &dependency[9];
	const char *subFileName = strrchr(fileName, '/');
	if (subFileName != nullptr)
		fileName = subFileName + 1;
	return fileName;
}
unsigned int AppContext::TryResolveDependency(AssetsFileContextInfo* pFileFrom, const AssetsFileDependency& dependency, bool allowSeveral)
{
	if (dependency.type != 0)
		return 0;
	const char* depFileName = getDependencyFileName(dependency.assetPath);
	bool hasSeveralCandidates = false;
	size_t candidateIdx = (size_t)-1;
	for (size_t k = 0; k < contextInfo.size(); k++)
	{
		IFileContext* pFileContext = contextInfo[k]->getFileContext();
		if (pFileContext->getType() == FileContext_Assets &&
			!stricmp(depFileName, contextInfo[k]->getFileName().c_str()))
		{
			if (candidateIdx != (size_t)-1)
			{
				hasSeveralCandidates = true;
				break;
			}
			candidateIdx = k;
		}
	}
	//Only resolve the dependency if there is exactly one match.
	if (candidateIdx != (size_t)-1 && (!hasSeveralCandidates || allowSeveral))
	{
		return contextInfo[candidateIdx]->getFileID();
	}
	return 0;
}
bool AppContext::AddContextInfo(std::shared_ptr<FileContextInfo> &info, unsigned int directoryEntryIdx)
{
	std::vector<std::pair<AssetsFileContextInfo*,size_t>> dependencyCallbackArgs; //Receives arguments for OnUpdateDependencies calls.
	bool ret = true;
	info->fileID = ++maxFileID;
	if (autoDetectDependencies && info->getFileContext() && info->getFileContext()->getType() == FileContext_Assets
		&& static_cast<AssetsFileContext*>(info->getFileContext())->getAssetsFile())
	{
		if (AssetsFileContextInfo *pNewAssetsInfo = dynamic_cast<AssetsFileContextInfo*>(info.get()))
		{
			auto refLock = pNewAssetsInfo->lockReferencesWrite();
			std::vector<unsigned int> &references = pNewAssetsInfo->getReferencesWrite(refLock);
			const std::vector<AssetsFileDependency> &dependencies = pNewAssetsInfo->getDependenciesWrite(refLock);
			assert(references.size() == dependencies.size());
			//Resolve references for the newly loaded file.
			for (size_t i = 0; i < dependencies.size(); i++)
			{
				//For each dependency, look for a previously loaded file that matches the name.
				if (i >= references.size())
					break;
				const AssetsFileDependency *pDependency = &dependencies[i];
				references[i] = TryResolveDependency(pNewAssetsInfo, dependencies[i], false);
			}
			refLock.unlock();

			//Resolve references for previously loaded files.
			std::string newFileName = info->getFileName();
			for (size_t iContext = 0; iContext < contextInfo.size(); iContext++)
			{
				//For each loaded Assets file context, check if the new file resolves a missing dependency.
				IFileContext *pPrevFileContext = contextInfo[iContext]->getFileContext();
				if (pPrevFileContext->getType() != FileContext_Assets
					|| !static_cast<AssetsFileContext*>(pPrevFileContext)->getAssetsFile())
					continue;
				if (AssetsFileContextInfo *pPrevAssetsInfo = dynamic_cast<AssetsFileContextInfo*>(contextInfo[iContext].get()))
				{
					auto prevRefLock = pPrevAssetsInfo->lockReferencesWrite();
					std::vector<unsigned int> &prevReferences = pPrevAssetsInfo->getReferencesWrite(prevRefLock);
					const std::vector<AssetsFileDependency> &prevDependencies = pPrevAssetsInfo->getDependenciesWrite(prevRefLock);
					assert(prevReferences.size() == prevDependencies.size());
					for (size_t iRef = 0; iRef < prevReferences.size(); iRef++)
					{
						if (iRef >= prevDependencies.size())
							break;
						const AssetsFileDependency *pDependency = &prevDependencies[iRef];
						if (pDependency->type != 0)
							continue;
						bool issueDependencyCallback = false;
						if (prevReferences[iRef] != 0)
						{
							//Only update references that aren't set already.
							if (contextInfoByFileID.find(prevReferences[iRef]) == contextInfoByFileID.end())
							{
								//References may be set to a closed file. In that case, treat it like it isn't set.
								prevReferences[iRef] = 0; //While we're at it, set it to 0.
								issueDependencyCallback = true;
							}
							else
								continue;
						}
						bool solvesReference = false;
						const char *depFileName = getDependencyFileName(pDependency->assetPath);
						if (!stricmp(newFileName.c_str(), depFileName))
						{
							prevReferences[iRef] = info->fileID; //Assign the new file as a dependency.
							
							//Mark the dependant file as a container source, if it has any containers.
							AssetContainerList &prevContainers = pPrevAssetsInfo->lockContainersRead();
							if (prevContainers.getContainerCount() > 0)
								pNewAssetsInfo->getContainerSources().push_back(pPrevAssetsInfo->getFileID());
							pPrevAssetsInfo->unlockContainersRead();

							issueDependencyCallback = true;
							solvesReference = true;
						}
						if (issueDependencyCallback)
							dependencyCallbackArgs.push_back(std::make_pair(pPrevAssetsInfo, iRef));
						if (solvesReference)
							break; //Each assets file should only have one entry per dependency
					}
					prevRefLock.unlock();
				}
			}
		}
	}
	contextInfo.push_back(info);
	std::unique_lock contextInfoMapLock(this->contextInfoMapMutex);
	if (info->parentFileID == 0)
	{
		contextInfoByFileID[info->fileID] = info;
		if (info->getFileContext())
		{
			info->lastFileName = info->getFileName();
			contextInfoByFileName.insert({ info->lastFileName, info });
		}
		contextInfoMapLock.unlock();
	}
	else
	{
		auto parentIt = contextInfoByFileID.find(info->parentFileID);
		if (parentIt != contextInfoByFileID.end())
		{
			//contextInfo still keeps a FileContextInfo reference for the main thread.
			FileContextInfo *pContextInfo = parentIt->second.get();
			contextInfoByFileID[info->fileID] = info;
			if (info->getFileContext())
			{
				info->lastFileName = info->getFileName();
				contextInfoByFileName.insert({ info->lastFileName, info });
			}
			contextInfoMapLock.unlock();
			if (pContextInfo->getFileContext()->getType() == FileContext_Bundle)
			{
				BundleFileContextInfo *pBundleContextInfo = (BundleFileContextInfo*)pContextInfo;
				contextInfoMapLock.lock();
				if (directoryEntryIdx < pBundleContextInfo->directoryRefs.size())
				{
					pBundleContextInfo->directoryRefs[directoryEntryIdx].fileID = info->fileID;
				}
				else
					assert(false);
				contextInfoMapLock.unlock();
			}
			else
				assert(false);
		}
		else
		{
			//Parent file has closed already.
			return false;
		}
	}
	assert(!contextInfoMapLock.owns_lock());
	//Call the dependency callbacks after the file has been registered properly.
	for (auto it = dependencyCallbackArgs.begin(); it != dependencyCallbackArgs.end(); ++it)
		this->OnUpdateDependencies(it->first, it->second, it->second);
	return true;
}
void AppContext::RemoveContextInfo(FileContextInfo *info)
{
	std::unique_lock contextInfoMapLock(this->contextInfoMapMutex);
	if (info->parentFileID != 0)
	{
		auto parentIt = contextInfoByFileID.find(info->parentFileID);
		if (parentIt != contextInfoByFileID.end())
		{
			parentIt->second->onCloseChild(info->getFileID());
		}
	}
	contextInfoByFileID.erase(info->getFileID());
	auto fileMapEntryIt = contextInfoByFileName.end();
	if (info->getFileContext())
	{
		auto rangeItPair = contextInfoByFileName.equal_range(info->lastFileName);
		for (auto it = rangeItPair.first; it != rangeItPair.second; ++it)
		{
			if (it->second.get() == info)
			{
				fileMapEntryIt = it;
				break;
			}
		}
	}
	if (fileMapEntryIt == contextInfoByFileName.end())
	{
		for (auto it = contextInfoByFileName.begin(); it != contextInfoByFileName.end(); ++it)
		{
			if (it->second.get() == info)
			{
				fileMapEntryIt = it;
				break;
			}
		}
	}
	if (fileMapEntryIt != contextInfoByFileName.end())
		contextInfoByFileName.erase(fileMapEntryIt);
	contextInfoMapLock.unlock();
	for (size_t i = 0; i < contextInfo.size(); i++)
	{
		if (contextInfo[i].get() == info)
		{
			contextInfo.erase(contextInfo.begin() + i);
			break;
		}
	}
}
void AppContext::OnGenerateContainers(AssetsFileContextInfo *info)
{
	OnUpdateContainers(info);
	const std::vector<unsigned int> references = info->getReferences();
	for (size_t i = 0; i < references.size(); i++)
	{
		std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
		auto ref = contextInfoByFileID.find(references[i]);
		FileContextInfo *pContextInfo = nullptr;
		if (ref != contextInfoByFileID.end())
			pContextInfo = ref->second.get();
		contextInfoMapLock.unlock();
		if (pContextInfo && pContextInfo->getFileContext()->getType() == FileContext_Assets)
		{
			AssetsFileContextInfo *pTarget = static_cast<AssetsFileContextInfo*>(pContextInfo);
			bool addAsSource = true;
			for (size_t k = 0; k < pTarget->containerSources.size(); k++)
			{
				if (pTarget->containerSources[k] == info->getFileID())
				{
					addAsSource = false;
					break;
				}
			}
			if (addAsSource)
				pTarget->containerSources.push_back(info->getFileID());
			OnUpdateContainers(pTarget);
		}
	}
}
void AppContext::OnChangeAsset_Async(AssetsFileContextInfo *info, pathid_t pathID, bool removed)
{
	signalMainThread(AppContextMsg_OnAssetChanged, new std::tuple<unsigned int, pathid_t, bool>(info->getFileID(), pathID, removed));
}
void AppContext::OnChangeBundleEntry_Async(BundleFileContextInfo *info, size_t index)
{
	signalMainThread(AppContextMsg_OnBundleEntryChanged, new std::tuple<unsigned int, size_t>(info->getFileID(), index));
}
FileContextInfo_ptr AppContext::getContextInfo(unsigned int fileID)
{
	if (fileID == 0)
		return nullptr;
	std::shared_ptr<FileContextInfo> ret(nullptr);
	std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
	auto ref = contextInfoByFileID.find(fileID);
	if (ref != contextInfoByFileID.end())
		ret = ref->second;
	return ret;
}
std::vector<FileContextInfo_ptr> AppContext::getContextInfo(const std::string& relFileName, FileContextInfo* pFileFrom)
{
	if (relFileName.starts_with("archive:/"))
	{
		size_t lastSlashPos = relFileName.rfind('/', std::string::npos);
		if (lastSlashPos == std::string::npos || lastSlashPos == 0)
		{
			assert(false); //"archive:/" contains a '/'
			return {};
		}
		//Find all candidates (file name match).
		std::vector<FileContextInfo_ptr> ret_unfiltered = getContextInfo(relFileName.substr(lastSlashPos + 1));
		std::vector<FileContextInfo_ptr> ret;
		for (size_t i = 0; i < ret_unfiltered.size(); ++i)
		{
			FileContextInfo_ptr pCurFile = ret_unfiltered[i];
			size_t cutoffPos = lastSlashPos;
			size_t searchPos = lastSlashPos;
			bool success = true;
			while ((searchPos = relFileName.rfind('/', searchPos - 1)) != std::string::npos && searchPos != 0)
			{
				unsigned int parentFileID = pCurFile->getParentFileID();
				FileContextInfo_ptr pParentFile;
				if (parentFileID == 0
					|| (pParentFile = getContextInfo(parentFileID)) == nullptr)
				{
					success = false;
					break;
				}
				std::string parentFileName;
				if (pParentFile->getFileContext() && pParentFile->getFileContext()->getType() == FileContext_Bundle)
				{
					//Bundle name is determined based on its first directory entry.
					auto pBundleParent = reinterpret_cast<BundleFileContextInfo*>(pParentFile.get());
					parentFileName = pBundleParent->getBundlePathName();
				}
				else
				{
					//Shouldn't normally happen (Unity appears to use "archive:/" references only for bundled files).
					parentFileName = pParentFile->getFileName();
				}
				//Weird error in Debug: "cannot seek string iterator because the iterator was invalidated" when incrementing the string iterator?
				//-> string::data() should do for now.
				if (!std::equal(relFileName.data() + (searchPos + 1), relFileName.data() + cutoffPos, parentFileName.begin(), parentFileName.end()))
				{
					success = false;
					break;
				}

				cutoffPos = searchPos;
			}
			if (success)
				ret.push_back(std::move(ret_unfiltered[i]));
		}
		return ret;
	}
	std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
	std::vector<FileContextInfo_ptr> candidates;
	auto range = contextInfoByFileName.equal_range(relFileName);
	//Only place the value (i.e. second) of each contextInfoByFileName iterator in ret.
	std::transform(range.first, range.second, std::back_inserter(candidates), [](auto x) {return x.second; });

	if (pFileFrom == nullptr || pFileFrom->getFileContext() == nullptr)
		return candidates;

	//Search relative to a given file context.
	if (candidates.empty())
		return {};
	const std::string& fromFilePathStr = pFileFrom->getFileContext()->getFilePath();
	//char8_t: Interpret string as UTF-8 even on Win32.
	std::filesystem::path fromFilePath(
		reinterpret_cast<const char8_t*>(&fromFilePathStr.data()[0]),
		reinterpret_cast<const char8_t*>(&fromFilePathStr.data()[fromFilePathStr.size()]));
	std::vector<FileContextInfo_ptr> ret;
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (candidates[i]->getParentFileID() != 0
			|| candidates[i]->getFileContext() == nullptr)
			continue;
		const std::string& candidateFilePathStr = candidates[i]->getFileContext()->getFilePath();
		std::filesystem::path candidateFilePath(
			reinterpret_cast<const char8_t*>(&candidateFilePathStr.data()[0]),
			reinterpret_cast<const char8_t*>(&candidateFilePathStr.data()[candidateFilePathStr.size()]));
		if (std::filesystem::equivalent(fromFilePath.parent_path(), candidateFilePath.parent_path()))
			ret.push_back(std::move(candidates[i]));
	}
	return ret;
}
void AppContext::OnUpdateContainers(AssetsFileContextInfo *info) {}
void AppContext::OnUpdateDependencies(AssetsFileContextInfo *info, size_t from, size_t to) {} //from/to: indices for info->references
void AppContext::OnChangeAsset(AssetsFileContextInfo *pFile, pathid_t pathID, bool wasRemoved) {}
void AppContext::OnChangeBundleEntry(BundleFileContextInfo *pFile, size_t index)
{
	// Locate the opened child file (if present).
	std::vector<unsigned int> childFileIDs;
	pFile->getChildFileIDs(childFileIDs);
	FileContextInfo_ptr pChildInfo = nullptr;
	if (childFileIDs.size() > index && childFileIDs[index] != 0
		&& (pChildInfo = getContextInfo(childFileIDs[index])))
	{
		auto newFileName = pChildInfo->getFileName();
		bool nameChanged = newFileName != pChildInfo->lastFileName;
		std::shared_lock contextInfoMapLock(this->contextInfoMapMutex);
		if (nameChanged)
		{
			//Erase the existing entry for the renamed file.
			auto range = contextInfoByFileName.equal_range(pChildInfo->lastFileName);
			for (auto it = range.first; it != range.second; ++it)
			{
				if (it->second.get() == pChildInfo.get())
				{
					contextInfoByFileName.erase(it);
					break;
				}
			}
		}
		if (nameChanged)
		{
			//Insert a new entry for the renamed file.
			pChildInfo->lastFileName = std::move(newFileName);
			if (!pChildInfo->lastFileName.empty())
				contextInfoByFileName.insert({ pChildInfo->lastFileName, pChildInfo });
		}
	}
}

bool AppContext::LoadClassDatabasePackage(const std::string &appBaseDir, std::string &errorMessage)
{
	bool ret = true;
	IAssetsReader *pDatabaseFileReader = Create_AssetsReaderFromFile("classdata.tpk", true, RWOpenFlags_Immediately);
	if (pDatabaseFileReader == NULL)
	{
		std::string targetDir = appBaseDir + "classdata.tpk";
		pDatabaseFileReader = Create_AssetsReaderFromFile(targetDir.c_str(), true, RWOpenFlags_Immediately);
	}
	if (pDatabaseFileReader != NULL)
	{
		if (!classPackage.Read(pDatabaseFileReader))
		{
			ret = false;
			errorMessage = "Invalid type database package!";
		}
		Free_AssetsReader(pDatabaseFileReader);
	}
	else
	{
		ret = false;
		errorMessage = "Unable to open the class database package file!";
	}
	return ret;
}
