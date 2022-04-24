#include "FileContextInfo.h"
#include "AppContext.h"
#include <InternalBundleReplacer.h>
#include <assert.h>

FileContextInfo::FileContextInfo(unsigned int fileID, unsigned int parentFileID)
	: fileID(fileID), parentFileID(parentFileID)
{}
FileContextInfo::~FileContextInfo()
{}
std::string FileContextInfo::getFileName()
{
	{
		std::lock_guard fileNameLock(fileNameOverrideMutex);
		if (!fileNameOverride.empty())
			return fileNameOverride;
	}
	if (getFileContext())
		return getFileContext()->getFileName();
	return "";
}
void FileContextInfo::setFileName(std::string name)
{
	std::lock_guard fileNameLock(fileNameOverrideMutex);
	fileNameOverride = std::move(name);
}

AssetsFileContextInfo::AssetsFileContextInfo(AssetsFileContext *pContext, unsigned int fileID, unsigned int parentFileID)
	: FileContextInfo(fileID, parentFileID), pContext(pContext), pClassDatabase(nullptr, ClassDatabaseFileDeleter_Dummy)
{
	assert(pContext->getAssetsFile());
	if (pContext->getAssetsFile())
	{
		references.resize(pContext->getAssetsFile()->dependencies.dependencyCount, 0);
		dependencies.assign(&pContext->getAssetsFile()->dependencies.pDependencies[0],
			&pContext->getAssetsFile()->dependencies.pDependencies[references.size()]);
	}
}
AssetsFileContextInfo::~AssetsFileContextInfo()
{
	IAssetsReader *pReader = this->pContext->getReaderUnsafe();
	this->pContext->Close();
	delete this->pContext;
	this->pContext = nullptr;
}
IFileContext *AssetsFileContextInfo::getFileContext()
{
	return this->pContext;
}
void AssetsFileContextInfo::getChildFileIDs(std::vector<unsigned int> &childFileIDs)
{
	childFileIDs.clear();
}
void AssetsFileContextInfo::onCloseChild(unsigned int childFileID)
{
}
bool AssetsFileContextInfo::hasAnyChanges(AppContext &appContext)
{
	if (permanentChangedFlag)
		return true;
	if (this->getAssetsFileContext() != nullptr && this->getAssetsFileContext()->getReaderIsModified())
		return true;
	this->lockReplacersRead();
	bool ret = !this->pReplacersByPathID.empty();
	this->unlockReplacersRead();
	if (ret)
		return true;
	auto refLock = this->lockReferencesRead();
	ret = this->dependenciesChanged;
	return ret;
}
bool AssetsFileContextInfo::hasNewChanges(AppContext &appContext)
{
	return this->changedFlag;
}
uint64_t AssetsFileContextInfo::write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag)
{
	auto refLock = this->lockReferencesRead();
	this->lockReplacersRead();
	if (this->pContext == nullptr || this->pContext->getAssetsFile() == nullptr)
	{
		this->unlockReplacersRead();
		return 0;
	}
	std::vector<AssetsReplacer*> replacers; replacers.reserve(this->pReplacersByPathID.size());
	for (auto it = this->pReplacersByPathID.begin(); it != this->pReplacersByPathID.end(); ++it)
	{
		assert(it->second.pReplacer != nullptr);
		replacers.push_back(it->second.pReplacer.get());
	}
	std::unique_ptr<AssetsDependenciesReplacer> pDependenciesReplacer_raii;
	if (dependenciesChanged)
	{
		std::vector<AssetsFileDependency> dependencies = this->getDependenciesRead(refLock);
		pDependenciesReplacer_raii.reset(MakeAssetsDependenciesReplacer(0, std::move(dependencies)));
		replacers.push_back(pDependenciesReplacer_raii.get());
	}
	uint64_t ret = this->pContext->getAssetsFile()->Write(pWriter, start, replacers.data(), replacers.size(), (uint32_t)-1, this->pClassDatabase.get());
	if (resetChangedFlag && ret != 0)
		this->changedFlag = false;
	this->unlockReplacersRead();
	return ret;
}
std::unique_ptr<BundleReplacer> AssetsFileContextInfo::makeBundleReplacer(class AppContext &appContext, 
	const char *oldName, const char *newName, uint32_t bundleIndex, 
	bool resetChangedFlag)
{
	auto refLock = this->lockReferencesRead();
	this->lockReplacersRead();
	if (this->pContext == nullptr || this->pContext->getAssetsFile() == nullptr
		|| (this->pReplacersByPathID.empty() && !dependenciesChanged))
	{
		this->unlockReplacersRead();
		return nullptr;
	}
	std::shared_lock classDatabaseLock(this->classDatabaseMutex);
	ClassDatabaseFile_sharedptr pClassDatabase = this->pClassDatabase;
	classDatabaseLock.unlock();
	std::vector<std::shared_ptr<AssetsReplacer>> pReplacers;
	pReplacers.reserve(this->pReplacersByPathID.size());
	for (auto replacerIt = this->pReplacersByPathID.begin(); replacerIt != this->pReplacersByPathID.end(); ++replacerIt)
	{
		if (replacerIt->second.pReplacer != nullptr
			&& (replacerIt->second.pReplacer->GetFileID() == 0 || replacerIt->second.pReplacer->GetFileID() == this->getFileID()))
			pReplacers.push_back(replacerIt->second.pReplacer);
		else
			assert(false); //Replacer is nullptr, or for another file.
	}
	if (dependenciesChanged)
	{
		std::vector<AssetsFileDependency> dependencies = this->getDependenciesRead(refLock);
		pReplacers.emplace_back(MakeAssetsDependenciesReplacer(0, std::move(dependencies)));
	}
	if (resetChangedFlag)
		this->changedFlag = false;
	this->unlockReplacersRead();
	return MakeBundleEntryModifierFromAssets(oldName, newName, 
		std::move(pClassDatabase), std::move(pReplacers), 
		this->getFileID(), bundleIndex);
}


std::vector<std::shared_ptr<AssetsReplacer>> AssetsFileContextInfo::getAllReplacers()
{
	std::vector<std::shared_ptr<AssetsReplacer>> ret;
	auto refLock = lockReferencesRead();
	lockReplacersRead();
	for (auto it = this->pReplacersByPathID.begin(); it != this->pReplacersByPathID.end(); ++it)
		ret.push_back(it->second.pReplacer);
	if (dependenciesChanged)
	{
		std::vector<AssetsFileDependency> dependencies = this->getDependenciesRead(refLock);
		ret.emplace_back(MakeAssetsDependenciesReplacer(0, std::move(dependencies)));
	}
	unlockReplacersRead();
	return ret;
}
std::shared_ptr<AssetsEntryReplacer> AssetsFileContextInfo::getReplacer(pathid_t pathID)
{
	std::shared_ptr<AssetsEntryReplacer> ret;
	lockReplacersRead();
	auto it = this->pReplacersByPathID.find(pathID);
	if (it != this->pReplacersByPathID.end())
		ret = it->second.pReplacer;
	unlockReplacersRead();
	return ret;
}
void AssetsFileContextInfo::addReplacer(std::shared_ptr<AssetsEntryReplacer> replacer, AppContext &appContext, bool reuseTypeMetaFromOldReplacer, bool signalMainThread) //Removes any previous replacers for that path ID.
{
	assert(this->pContext && this->pContext->getAssetsFileTable());
	uint64_t pathID = replacer->GetPathID();
	bool isRemover = replacer->GetType() == AssetsReplacement_Remove;
	lockReplacersWrite();
	auto it = this->pReplacersByPathID.find((pathid_t)replacer->GetPathID());
	if (it != this->pReplacersByPathID.end())
	{
		if (!it->second.replacesExistingAsset && replacer->GetType() == AssetsReplacement_Remove)
			this->pReplacersByPathID.erase(it); //The asset was created by a replacer => the remover does not need to be stored.
		else
		{
			if (reuseTypeMetaFromOldReplacer && replacer->GetType() != AssetsReplacement_Remove)
			{
				Hash128 origPropertiesHash;
				if (it->second.pReplacer->GetPropertiesHash(origPropertiesHash))
					replacer->SetPropertiesHash(origPropertiesHash);
				Hash128 origScriptIDHash;
				if (it->second.pReplacer->GetScriptIDHash(origScriptIDHash))
					replacer->SetScriptIDHash(origScriptIDHash);
				assert(replacer->GetMonoScriptID() == it->second.pReplacer->GetMonoScriptID());

				const AssetPPtr *pPreloadList; size_t preloadListLen = 0;
				if (it->second.pReplacer->GetPreloadDependencies(pPreloadList, preloadListLen))
					replacer->SetPreloadDependencies(pPreloadList, preloadListLen);
				
				std::shared_ptr<ClassDatabaseFile> pOrigClassFile;
				ClassDatabaseType *pOrigClassType = nullptr;
				if (it->second.pReplacer->GetTypeInfo(pOrigClassFile, pOrigClassType))
					replacer->SetTypeInfo(std::move(pOrigClassFile), pOrigClassType);
			}
			it->second.pReplacer = std::move(replacer); //The previous replacer was overridden.
		}
	}
	else
	{
		ReplacerEntry entry;
		//Insert or overwrite the replacer entry for the path ID.
		auto insertResult = this->pReplacersByPathID.insert(std::make_pair(replacer->GetPathID(), entry));
		insertResult.first->second.replacesExistingAsset = (this->pContext->getAssetsFileTable()->getAssetInfo(replacer->GetPathID()) != nullptr);
		insertResult.first->second.pReplacer = std::move(replacer);
	}
	this->changedFlag = true;
	unlockReplacersWrite();
	if (signalMainThread)
		appContext.OnChangeAsset_Async(this, pathID, isRemover);
}

std::vector<std::shared_ptr<ClassDatabaseFile>> &AssetsFileContextInfo::lockScriptDatabases()
{
	this->scriptDatabasesMutex.lock();
	return this->pScriptDatabases;
}
void AssetsFileContextInfo::unlockScriptDatabases()
{
	this->scriptDatabasesMutex.unlock();
}

AssetsFileContextInfo::ContainersTask::ContainersTask(AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> &pContextInfo)
	: appContext(appContext), pFileContextInfo(pContextInfo)
{
	assert(pFileContextInfo->getAssetsFileContext() && pFileContextInfo->getAssetsFileContext()->getAssetsFile());
	name = "Resolve containers : " + pContextInfo->getFileName() + "";
}
const std::string &AssetsFileContextInfo::ContainersTask::getName()
{
	return name;
}
TaskResult AssetsFileContextInfo::ContainersTask::execute(TaskProgressManager &progressManager)
{
	if (!pFileContextInfo->getAssetsFileContext() || !pFileContextInfo->getAssetsFileContext()->getAssetsFile())
	{
		progressManager.logMessage("Assets file not loaded!");
		return -1;
	}

	progressManager.setProgressDesc("Searching for container assets");
	progressManager.setProgress(0, 0);
	AssetIterator iterator(this->pFileContextInfo.get());
	AssetIdentifier identifier;
	identifier.pFile = this->pFileContextInfo;

	int32_t resmgrClassID = this->pFileContextInfo->GetClassByName("ResourceManager");
	if (resmgrClassID == -1) resmgrClassID = ASSETTYPE_RESOURCEMANAGER;
	AssetTypeTemplateField resmgrTemplateBase;
	bool hasResmgrTemplate = this->pFileContextInfo->MakeTemplateField(&resmgrTemplateBase, this->appContext, resmgrClassID);
	
	int32_t bundleClassID = this->pFileContextInfo->GetClassByName("AssetBundle");
	if (bundleClassID == -1) bundleClassID = ASSETTYPE_ASSETBUNDLE;
	AssetTypeTemplateField bundleTemplateBase;
	bool hasBundleTemplate = this->pFileContextInfo->MakeTemplateField(&bundleTemplateBase, this->appContext, bundleClassID);
	
	AssetContainerList newContainerList;
	TaskResult result = 0;
	for (; !iterator.isEnd(); ++iterator)
	{
		iterator.get(identifier);
		if (identifier.resolve(this->appContext))
		{
			int32_t curClassID = identifier.getClassID();
			if (curClassID == resmgrClassID)
			{
				ResourceManagerFile resMgrFile;
				if (hasResmgrTemplate)
				{
					IAssetsReader_ptr pReader = identifier.makeReader();
					AssetTypeTemplateField *pResmgrTemplateBase = &resmgrTemplateBase;
					AssetTypeInstance resmgrInstance(1, &pResmgrTemplateBase, identifier.getDataSize(), pReader.get(), identifier.isBigEndian());
					AssetTypeValueField *pInstanceBase = resmgrInstance.GetBaseField();
					if (pInstanceBase)
					{
						resMgrFile.Read(pInstanceBase);
					}
					else
					{
						progressManager.logMessage("Unable to deserialize a ResourceManager asset!");
						result = 1;
						continue;
					}
				}
				else
				{
					uint64_t dataSize = identifier.getDataSize();
					if (dataSize > SIZE_MAX)
					{
						progressManager.logMessage("Data size invalid!");
						result = 2;
						continue;
					}
					std::unique_ptr<uint8_t[]> resmgrBuf(new uint8_t[(size_t)dataSize]);
					if (identifier.read(dataSize, resmgrBuf.get()) != dataSize)
					{
						progressManager.logMessage("Unable to lock the reader!");
						result = 3;
						continue;
					}
					size_t filePos = 0;
					resMgrFile.Read(resmgrBuf.get(), (size_t)dataSize, &filePos, pFileContextInfo->getAssetsFileContext()->getAssetsFile()->header.format, identifier.isBigEndian());
				}
				if (resMgrFile.IsRead())
				{
					if (!newContainerList.LoadFrom(resMgrFile))
					{
						progressManager.logMessage("Unable to populate the containers list with a ResourceManager asset!");
						result = 4;
					}
				}
				else
				{
					progressManager.logMessage("Unable to deserialize a ResourceManager asset!");
					result = 5;
				}
			}
			else if (curClassID == bundleClassID)
			{
				AssetBundleAsset assetBundleFile;

				uint64_t dataSize = identifier.getDataSize();
				if (dataSize > SIZE_MAX)
				{
					progressManager.logMessage("Data size invalid!");
					result = 6;
					continue;
				}
				std::unique_ptr<uint8_t[]> bundleBuf(new uint8_t[(size_t)dataSize]);
				if (identifier.read(dataSize, bundleBuf.get()) != dataSize)
				{
					progressManager.logMessage("Unable to read the data!");
					result = 7;
					continue;
				}
				if (hasBundleTemplate)
				{
					size_t filePos = 0;
					assetBundleFile.ReadBundleFile(bundleBuf.get(), (size_t)dataSize, &filePos, &bundleTemplateBase, identifier.isBigEndian());
				}
				else
				{
					size_t filePos = 0;
					assetBundleFile.ReadBundleFile(bundleBuf.get(), (size_t)dataSize, &filePos, pFileContextInfo->getAssetsFileContext()->getAssetsFile()->header.format, identifier.isBigEndian());
				}
				if (assetBundleFile.IsRead())
				{
					if (!newContainerList.LoadFrom(assetBundleFile))
					{
						progressManager.logMessage("Unable to populate the containers list with an AssetBundle asset!");
						result = 8;
					}
				}
				else
				{
					progressManager.logMessage("Unable to deserialize an AssetBundle asset!");
					result = 9;
				}
			}
		}
		else
			progressManager.logMessage("Unable to resolve an asset!");
	}
	this->pFileContextInfo->lockContainersWrite();
	this->pFileContextInfo->containers = std::move(newContainerList);
	this->pFileContextInfo->unlockContainersWrite();
	return result;
}
bool AssetsFileContextInfo::EnqueueContainersTask(AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> selfPointer)
{
	assert(selfPointer.get() == this);
	std::shared_ptr<ContainersTask> pTask = std::make_shared<ContainersTask>(appContext, selfPointer);
	if (appContext.taskManager.enqueue(pTask))
		return true;
	return false;
}


bool AssetsFileContextInfo::_GetMonoBehaviourScriptInfo(AssetIdentifier &asset, AppContext &appContext,
	std::string &fullClassName, std::string &assemblyName,
	std::string &className, std::string &namespaceName,
	AssetIdentifier &scriptAsset)
{
	if (!pContext || !pContext->getAssetsFile())
		return false;
	bool ret = false;
	//Don't use class data from the .assets file (especially for bundled ones) as it might resolve MonoBehaviour to -1 or -2. 
	//int monoBehaviourClass = -1;//pInterface->GetClassByName(pAssetsFile, "MonoBehaviour");
	if (asset.pathID != 0)
	{
		AssetTypeTemplateField behaviourBase;
		AssetTypeTemplateField *pBehaviourBase = &behaviourBase;
		std::shared_lock classDatabaseLock(this->classDatabaseMutex);
		bool failed = true;
		if (pClassDatabase != nullptr)
		{
			for (size_t i = 0; i < pClassDatabase->classes.size(); i++)
			{
				if (!strcmp(pClassDatabase->classes[i].name.GetString(pClassDatabase.get()), "MonoBehaviour"))
				{
					failed = !pBehaviourBase->FromClassDatabase(pClassDatabase.get(), &pClassDatabase->classes[i], 0);
					//monoBehaviourClass = pFile->classes[i].classId;
					break;
				}
			}
		}
		classDatabaseLock.unlock();
		if (!failed)
		{
			//Try to find and read the script asset.
			IAssetsReader_ptr pReader = asset.makeReader();
			unsigned __int64 fileSize = asset.getDataSize();
			AssetTypeInstance behaviourInstance(1, &pBehaviourBase, fileSize, pReader.get(), asset.isBigEndian());
			pReader.reset();

			AssetTypeValueField *pBehaviourBase = behaviourInstance.GetBaseField();
			AssetTypeValueField *pScriptFileIDField;
			AssetTypeValueField *pScriptPathIDField;
			if ((pBehaviourBase != NULL) && 
				(pScriptFileIDField = pBehaviourBase->Get("m_Script")->Get("m_FileID"))->GetValue() && 
				(pScriptPathIDField = pBehaviourBase->Get("m_Script")->Get("m_PathID"))->GetValue())
			{
				unsigned int scriptFileID = asset.pFile->resolveRelativeFileID(pScriptFileIDField->GetValue()->AsInt());
				long long int scriptPathID = pScriptPathIDField->GetValue()->AsInt64();
				scriptAsset = AssetIdentifier(scriptFileID, scriptPathID);
				if (scriptAsset.resolve(appContext))
				{
					AssetsFileContextInfo *pScriptAssetsContextInfo = scriptAsset.pFile.get();
					bool foundScript = false;
					int monoScriptClass = GetClassByName("MonoScript");
					AssetTypeTemplateField scriptTemplateBase;
					if ((monoScriptClass >= 0) && pScriptAssetsContextInfo->MakeTemplateField(&scriptTemplateBase, appContext, monoScriptClass))
					{
						unsigned long long curFileSize = scriptAsset.getDataSize();
						IAssetsReader_ptr pReader = scriptAsset.makeReader();
						if (pReader != nullptr)
						{
							AssetTypeTemplateField *pScriptTemplateBase = &scriptTemplateBase;
							AssetTypeInstance scriptInstance(1, &pScriptTemplateBase, curFileSize, pReader.get(), scriptAsset.isBigEndian());
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
								if ((pScriptAssemblyNameField = pScriptBase->Get("m_AssemblyName"))->GetValue()
									&& (scriptAssemblyName = pScriptAssemblyNameField->GetValue()->AsString()))
								{}
								else
									scriptAssemblyName = "";
								fullClassName = std::string(scriptNamespace);
								if (fullClassName.size() > 0)
									fullClassName += ".";
								fullClassName += scriptClassName;
								assemblyName = std::string(scriptAssemblyName);
								className = std::string(scriptClassName);
								namespaceName = std::string(scriptNamespace);
								ret = true;
							}
						}
					}
				}
			}
		}
	}
	return ret;
}
bool AssetsFileContextInfo::FindScriptClassDatabaseEntry(ClassDatabaseFile *&pClassFile, ClassDatabaseType *&pClassType, AssetIdentifier &asset, AppContext &appContext, Hash128 *pScriptID)
{
	pClassFile = nullptr;
	pClassType = nullptr;
	{
		//Try to find a matching script class database entry.
		std::string fullScriptName;
		std::string assemblyName;
		std::string scriptName;
		std::string namespaceName;
		AssetIdentifier scriptAsset;
		if (_GetMonoBehaviourScriptInfo(asset, appContext, fullScriptName, assemblyName, scriptName, namespaceName, scriptAsset))
		{
			//Look for the script database in the .assets file where the MonoScript asset lies.
			auto &scriptDatabases = scriptAsset.pFile->lockScriptDatabases();
			bool found = false;
			for (size_t i = 0; i < scriptDatabases.size(); i++)
			{
				std::shared_ptr<ClassDatabaseFile> &pCurDatabase = scriptDatabases[i];
				if (!(pCurDatabase->header.flags & 1))
					continue;
				for (size_t k = 0; k < pCurDatabase->classes.size(); k++)
				{
					ClassDatabaseType &curType = pCurDatabase->classes[k];
					const char *curAssemblyName = assemblyName.size() > 0 ? curType.assemblyFileName.GetString(pCurDatabase.get()) : "";
					const char *curFullClassName = curType.name.GetString(pCurDatabase.get());
					if (curType.fields.size() > 1 && !stricmp(curAssemblyName, assemblyName.c_str()) && !strcmp(curFullClassName, fullScriptName.c_str()))
					{
						if (pScriptID)
						{
							*pScriptID = MakeScriptID(scriptName.c_str(), namespaceName.c_str(), curType.assemblyFileName.GetString(pCurDatabase.get()));
						}
						pClassFile = pCurDatabase.get();
						pClassType = &curType;
						scriptAsset.pFile->unlockScriptDatabases();
						return true;
					}
				}
			}
			scriptAsset.pFile->unlockScriptDatabases();
		}
	}
	if (pScriptID) *pScriptID = Hash128();
	return false;
}
bool AssetsFileContextInfo::MakeTemplateField(AssetTypeTemplateField *pTemplateBase, AppContext &appContext, int32_t classID, uint16_t scriptIndex, AssetIdentifier *pAsset,
	std::optional<std::reference_wrapper<bool>> missingScriptTypeInfo)
{
	if (missingScriptTypeInfo.has_value())
		missingScriptTypeInfo->get() = false;
	if (!pContext || !pContext->getAssetsFile())
		return false;
	pTemplateBase->Clear();
	if (pAsset != nullptr && pAsset->pReplacer != nullptr)
	{
		ClassDatabaseFile_sharedptr pCldbFile; ClassDatabaseType* pCldbType = nullptr;
		if (pAsset->pReplacer->GetTypeInfo(pCldbFile, pCldbType))
		{
			pTemplateBase->Clear();
			if (pTemplateBase->FromClassDatabase(pCldbFile.get(), pCldbType, 0))
				return true;
		}
	}
	AssetsFile *pAssetsFile = pContext->getAssetsFile();

	union {
		Type_07 *pU4TypeEntry;
		Type_0D *pU5TypeEntry;
	} typeEntry = {NULL};
	if (classID < 0 && missingScriptTypeInfo.has_value())
		missingScriptTypeInfo->get() = true;
	if (classID < 0 && (pAssetsFile->header.format >= 0x10))
	{
		int monoBehaviourClass = this->GetClassByName("MonoBehaviour");
		for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
		{
			if ((pAssetsFile->typeTree.pTypes_Unity5[i].classId == monoBehaviourClass) && 
				pAssetsFile->typeTree.pTypes_Unity5[i].scriptIndex == scriptIndex)
			{
				typeEntry.pU5TypeEntry = &pAssetsFile->typeTree.pTypes_Unity5[i];
				break;
			}
		}
	}
	else if (pAssetsFile->header.format >= 0x0D)
	{
		for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
		{
			Type_0D &type = pAssetsFile->typeTree.pTypes_Unity5[i];
			if (type.classId == classID && (type.scriptIndex == 0xFFFF || scriptIndex != 0xFFFF || classID < 0))
			{
				typeEntry.pU5TypeEntry = &pAssetsFile->typeTree.pTypes_Unity5[i];
				break;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
		{
			if (pAssetsFile->typeTree.pTypes_Unity4[i].classId == classID)
			{
				typeEntry.pU4TypeEntry = &pAssetsFile->typeTree.pTypes_Unity4[i];
				break;
			}
		}
	}
	
	bool failed = false;
	if ((typeEntry.pU5TypeEntry != NULL) || (typeEntry.pU4TypeEntry != NULL))
	{
		if (missingScriptTypeInfo.has_value())
			missingScriptTypeInfo->get() = false;
		if (pAssetsFile->header.format >= 0x0D)
		{
			if (typeEntry.pU5TypeEntry->typeFieldsExCount > 0)
				failed = !pTemplateBase->From0D(typeEntry.pU5TypeEntry, 0);
		}
		else
		{
			failed = !pTemplateBase->From07(&typeEntry.pU4TypeEntry->base);
		}
		if (failed)
		{
			pTemplateBase->Clear();
			//return false;
		}
	}

	if (pTemplateBase->children.size() == 0)
	{
		pTemplateBase->Clear();
		std::shared_lock classDatabaseLock(classDatabaseMutex);
		if (pClassDatabase != nullptr)
		{
			ClassDatabaseFile *pFile = pClassDatabase.get();
			failed = false;
			for (size_t i = 0; i < pFile->classes.size(); i++)
			{
				if (pFile->classes[i].classId == classID)
				{
					failed = !pTemplateBase->FromClassDatabase(pFile, &pFile->classes[i], 0);
					break;
				}
			}
			if (!failed && (pTemplateBase->children.size() == 0) && (classID < 0))
			{
				int monoBehaviourClass = this->GetClassByName("MonoBehaviour");
				if (monoBehaviourClass >= 0)
				{
					failed = true;
					for (size_t i = 0; i < pFile->classes.size(); i++)
					{
						if (pFile->classes[i].classId == monoBehaviourClass)
						{
							failed = !pTemplateBase->FromClassDatabase(pFile, &pFile->classes[i], 0);
							break;
						}
					}
					classDatabaseLock.unlock();
					if (!failed && pAsset)
					{
						assert(classID == pAsset->getClassID());
						assert(scriptIndex == pAsset->getMonoScriptID());

						ClassDatabaseFile *pScriptDatabase = nullptr;
						ClassDatabaseType *pScriptType = nullptr;
						//Try to find a matching script class database entry and append the resulting template fields to the MonoBehaviour template.
						if (FindScriptClassDatabaseEntry(pScriptDatabase, pScriptType, *pAsset, appContext))
						{
							if (missingScriptTypeInfo.has_value())
								missingScriptTypeInfo->get() = false;
							AssetTypeTemplateField tempBase;
							if (tempBase.FromClassDatabase(pScriptDatabase, pScriptType, 0) && tempBase.children.size() > 0)
							{
								uint32_t targetOffset = (uint32_t)pTemplateBase->children.size();
								pTemplateBase->AddChildren(tempBase.children.size());
								std::copy(tempBase.children.begin(), tempBase.children.end(), pTemplateBase->children.begin() + targetOffset);
							}
						}
						else if (missingScriptTypeInfo.has_value())
								missingScriptTypeInfo->get() = true;
					}
				}
			}
		}
		if (failed)
		{
			pTemplateBase->Clear();
			return false;
		}
		if (pTemplateBase->children.empty())
		{
			pTemplateBase->Clear();
			return false;
		}
	}
	return true;
}
int32_t AssetsFileContextInfo::GetClassByName(const char *name)
{
	union {
		Type_07 *pU4TypeEntry;
		Type_0D *pU5TypeEntry;
	} typeEntry = {NULL};
	AssetsFile *pAssetsFile;
	if (!this->pContext || !(pAssetsFile = this->pContext->getAssetsFile()))
		return -1;
	if (pAssetsFile->header.format >= 0x0D)
	{
		for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
		{
			Type_0D *pType = &pAssetsFile->typeTree.pTypes_Unity5[i];
			if (pType->typeFieldsExCount > 0 && 
				!strcmp(pType->pTypeFieldsEx[0].GetTypeString(pType->pStringTable, pType->stringTableLen), name))
			{
				return pType->classId;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
		{
			if (!strcmp(pAssetsFile->typeTree.pTypes_Unity4[i].base.type, name))
			{
				return pAssetsFile->typeTree.pTypes_Unity4[i].classId;
			}
		}
	}

	std::shared_lock classDatabaseLock(classDatabaseMutex);
	if (pClassDatabase != nullptr)
	{
		for (size_t i = 0; i < pClassDatabase->classes.size(); i++)
		{
			if (!strcmp(pClassDatabase->classes[i].name.GetString(pClassDatabase.get()), name))
			{
				return pClassDatabase->classes[i].classId;
			}
		}
	}
	return -1;
}
std::string AssetsFileContextInfo::GetClassName_(AppContext &appContext, int32_t classID, uint16_t scriptIndex, AssetIdentifier *pAsset)
{
	union {
		Type_07 *pU4TypeEntry;
		Type_0D *pU5TypeEntry;
	} typeEntry = {NULL};
	AssetsFile *pAssetsFile;
	if (!this->pContext || !(pAssetsFile = this->pContext->getAssetsFile()))
		return std::string();
	if (classID >= 0 && scriptIndex == 0xFFFF)
	{
		if (pAssetsFile->header.format >= 0x0D)
		{
			if (pAssetsFile->typeTree.hasTypeTree)
				for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
				{
					Type_0D *pType = &pAssetsFile->typeTree.pTypes_Unity5[i];
					if (pType->typeFieldsExCount > 0 && pType->classId == classID && pType->scriptIndex == scriptIndex)
					{
						const char *className = pType->pTypeFieldsEx->GetTypeString(pType->pStringTable, pType->stringTableLen);
						const char *baseName = pType->pTypeFieldsEx->GetNameString(pType->pStringTable, pType->stringTableLen);
						if (!stricmp(baseName, "Base"))
							return std::string(className);
						else
							break;
					}
				}
		}
		else
		{
			for (size_t i = 0; i < pAssetsFile->typeTree.fieldCount; i++)
			{
				Type_07 *pType = &pAssetsFile->typeTree.pTypes_Unity4[i];
				if (pType->classId == classID)
				{
					if (!strnicmp(pType->base.name, "Base", 5))
					{
						return std::string(&pType->base.type[0],
							&pType->base.type[strnlen(pType->base.type, sizeof(pType->base.type) / sizeof(char))]);
					}
					else
						break;
				}
			}
		}

		std::shared_lock classDatabaseLock(classDatabaseMutex);
		if (pClassDatabase != nullptr)
		{
			for (size_t i = 0; i < pClassDatabase->classes.size(); i++)
			{
				if (pClassDatabase->classes[i].classId == classID)
				{
					return std::string(pClassDatabase->classes[i].name.GetString(pClassDatabase.get()));
				}
			}
		}
		classDatabaseLock.unlock();

		const char *typeName = NULL;
		char sprntTmp[12];
		switch (classID)
		{
			case 0x01:
				typeName = "GameObject";
				break;
			case 0x04:
				typeName = "Transform";
				break;
			case 0x14:
				typeName = "Camera";
				break;
			case 0x15:
				typeName = "Material";
				break;
			case 0x17:
				typeName = "MeshRenderer";
				break;
			case 0x1C:
				typeName = "Texture2D";
				break;
			case 0x21:
				typeName = "MeshFilter";
				break;
			case 0x30:
				typeName = "Shader";
				break;
			case 0x31:
				typeName = "Text";
				break;
			case 0x41:
				typeName = "BoxCollider";
				break;
			case 0x53:
				typeName = "Audio";
				break;
			case 0x68:
				typeName = "RenderSettings";
				break;
			case 0x6C:
				typeName = "Light";
				break;
			case 0x7C:
				typeName = "Behaviour";
				break;
			case 0x7F:
				typeName = "LevelGameManager";
				break;
			case 0x87:
				typeName = "SphereCollider";
				break;
			case ASSETTYPE_ASSETBUNDLE:
				typeName = "AssetBundle file table";
				break;
			case 0x93:
				typeName = "ResourceManager file table";
				break;
			case 0x96:
				typeName = "PreloadData";
				break;
			case 0x9D:
				typeName = "LightmapSettings";
				break;
			case 0xD4:
				typeName = "SpriteRenderer";
				break;
			case 0xD5:
				typeName = "Sprite";
				break;
			case 0x122:
				typeName = "AssetBundleManifest";
				break;
			default:
				sprintf_s(sprntTmp, "0x%08X", classID);
				typeName = sprntTmp;
				break;
		}
		return std::string(typeName);
	}
	else
	{
		if (pAsset != NULL && classID < 0)
		{
			std::string className;
			std::string assemblyName;
			std::string scriptName;
			std::string namespaceName;
			AssetIdentifier scriptAsset;
			if (_GetMonoBehaviourScriptInfo(*pAsset, appContext, className, assemblyName, scriptName, namespaceName, scriptAsset))
			{
				return std::string("MonoBehaviour : ") + className + " (" + assemblyName + ")"; 
			}
		}
		return std::string("MonoBehaviour");
	}
	assert(false);
	return std::string();
}

struct VersionRange
{
	char lastSeparator; //0 as a wildcard, otherwise typically '.', 'b', 'f', 'p'.
	uint32_t start;
	uint32_t end;
	VersionRange(uint32_t start = 0, uint32_t end = UINT32_MAX)
		: lastSeparator(0), start(start), end(end)
	{}
};
//Supports normal engine version strings (5.4.3p1, 2019.3.13f1, ...) and range strings (5.4.*, 5.4.2+, 5.4.3p*, 5.4.3p1-5.4.4p4, etc.).
// '*' matches any [uint]; value:=[uint] '+' matches any [uint] >= value; value:=[uint] matches only [uint] = value;
// Separators such as '.','b','f','p' must be equal, 
//  unless the current separator or a previous one in a range string of the form (from'-'to) differs between from and to.
// 
//String format (char matches any non-digit including whitespace but not '-','+','*') :
// Version: Range {char Range} ('-' uint {char uint})?
// Range: uint | uint '+' | '*'
inline bool parseRange(const std::string &versionString, size_t start, size_t &next, VersionRange &range)
{
	if (versionString.size() <= start)
		return false;
	if (versionString[start] == '*')
	{
		next = start + 1;
		range.start = 0;
		range.end = (uint32_t)-1;
		return true;
	}
	if (versionString[start] >= '0' && versionString[start] <= '9')
	{
		size_t uintEnd = start + 1;
		for (uintEnd = start + 1; uintEnd < versionString.size(); uintEnd++)
		{
			if (versionString[uintEnd] < '0' || versionString[uintEnd] > '9')
				break;
		}
		bool hasPlusSign = false;
		if (uintEnd < versionString.size() && versionString[uintEnd] == '+')
		{
			hasPlusSign = true;
			next = uintEnd + 1;
		}
		else
			next = uintEnd;
		uint32_t uintVal = 0;
		try {
			uintVal = (uint32_t)std::stoul(versionString.substr(start, uintEnd - start));
		} catch (...) { //Invalid number format, out of range, ...
			return false;
		}
		range.start = uintVal;
		range.end = (hasPlusSign ? UINT32_MAX : uintVal);
		return true;
	}
	return false;
}
static std::vector<VersionRange> parseVersionString(std::string versionString)
{
	std::vector<VersionRange> ret;
	VersionRange curRange(0, UINT32_MAX);
	size_t pos = 0;
	while (parseRange(versionString, pos, pos, curRange))
	{
		ret.push_back(curRange);
		//Match a char or '-'
		if (pos >= versionString.length())
			return ret;
		if (versionString[pos] == '+' || versionString[pos] == '*'
			|| (versionString[pos] >= '0' && versionString[pos] <= '9'))
			return ret;
		if (versionString[pos] == '-')
			break;
		curRange = VersionRange();
		curRange.lastSeparator = versionString[pos++];
	}
	if (pos < versionString.length() && versionString[pos] == '-')
	{
		//The version string has the format from'-'to.
		//Also match ranges here for simplicity (i.e. allowing the uint'+' and '*' cases).
		size_t i = 0;
		bool anySeparator = false;
		while (parseRange(versionString, pos, pos, curRange))
		{
			if (ret.size() > i)
			{
				if (curRange.lastSeparator != ret[i].lastSeparator)
					anySeparator = true;
				if (anySeparator)
					ret[i].lastSeparator = 0; //wildcard
				ret[i].end = curRange.end;
			}
			else
			{
				//Wildcard the separator, since there is no strict order between 'f' (final?) and 'p' (patch) releases.
				curRange.lastSeparator = 0; 
				curRange.start = 0;
				ret.push_back(curRange);
			}
			if (pos >= versionString.length())
				return ret;
			//Match a char.
			if (versionString[pos] == '+' || versionString[pos] == '*' || versionString[pos] == '-' 
				|| (versionString[pos] >= '0' && versionString[pos] <= '9'))
				return ret;
			curRange = VersionRange();
			curRange.lastSeparator = versionString[pos++];
			i++;
		}
	}
	return ret;
}

bool versionRangesOverlap(const std::vector<VersionRange> &a, const std::vector<VersionRange> &b)
{
	if (a.size() > b.size())
		return versionRangesOverlap(b, a);
	for (size_t i = 0; i < a.size(); i++)
	{
		if (i >= b.size())
			return true;
		//Compare the separators and allow wildcards.
		if (a[i].lastSeparator && b[i].lastSeparator && a[i].lastSeparator != b[i].lastSeparator)
			return false;
		//Cases :
		//Range from a starts before range from b but still overlaps
		//Range from a starts inside range from b
		if (a[i].end >= a[i].start && b[i].end >= b[i].start && (
			(a[i].start <= b[i].start && a[i].end >= b[i].start)
			|| (a[i].start >= b[i].start && a[i].start <= b[i].end)))
		{}
		else
			return false;
	}
	return true;
}

bool AssetsFileContextInfo::FindClassDatabase(ClassDatabasePackage &package)
{
	AssetsFile *pAssetsFile;
	if (!this->pContext || !(pAssetsFile = this->pContext->getAssetsFile()))
		return false;

	//Assuming that all Unity version strings are of the form 'Major.Minor.Hotfix<b|f|p>Build'.
	std::vector<VersionRange> targetVersion = parseVersionString(std::string(pAssetsFile->typeTree.unityVersion));
	if (targetVersion.size() == 4) //Safety measure against unexpected results (e.g. if parsing failed, all class databases would match).
	{
		for (uint32_t k = 0; k < package.header.fileCount; k++)
		{
			for (int l = 0; l < package.files[k]->header.unityVersionCount; l++)
			{
				char *query = package.files[k]->header.pUnityVersions[l];
				std::vector<VersionRange> referenceRange = parseVersionString(std::string(query));

				if (versionRangesOverlap(targetVersion, referenceRange))
				{
					std::scoped_lock classDatabaseLock(this->classDatabaseMutex);
					pClassDatabase = ClassDatabaseFile_sharedptr(package.files[k], ClassDatabaseFileDeleter_Dummy);
					break;
				}
			}
			if (pClassDatabase)
				break;
		}
		if (pClassDatabase)
			return true; //Found a matching class database.
	}
	if (pAssetsFile->AssetCount == 0 || (pAssetsFile->typeTree.fieldCount > 0 && pAssetsFile->typeTree.hasTypeTree))
		return true; //The type information is stored (hopefully for all types).
	return false;
}

void AssetsFileContextInfo::SetClassDatabase(ClassDatabaseFile_sharedptr pClassDatabase)
{
	std::scoped_lock classDatabaseLock(this->classDatabaseMutex);
	this->pClassDatabase = std::move(pClassDatabase);
}
ClassDatabaseFile_sharedptr AssetsFileContextInfo::GetClassDatabase()
{
	std::shared_lock classDatabaseLock(this->classDatabaseMutex);
	ClassDatabaseFile_sharedptr ret = this->pClassDatabase;
	return ret;
}

BundleFileContextInfo::BundleFileContextInfo(BundleFileContext *pContext, unsigned int fileID, unsigned int parentFileID)
	: FileContextInfo(fileID, parentFileID), pContext(pContext), isDecompressed(false)
{
}
BundleFileContextInfo::~BundleFileContextInfo()
{
	if (this->pContext)
		CloseContext();
}
void BundleFileContextInfo::CloseContext()
{
	IAssetsReader *pReader = this->pContext->getReaderUnsafe();
	this->pContext->Close();
	delete this->pContext;
	this->pContext = nullptr;
}
IFileContext *BundleFileContextInfo::getFileContext()
{
	return this->pContext;
}
void BundleFileContextInfo::getChildFileIDs(std::vector<unsigned int> &childFileIDs)
{
	std::shared_lock directoryLock(this->directoryMutex);
	childFileIDs.resize(directoryRefs.size());
	for (size_t i = 0; i < directoryRefs.size(); ++i)
		childFileIDs[i] = directoryRefs[i].fileID;
}
void BundleFileContextInfo::onCloseChild(unsigned int childFileID)
{
	std::scoped_lock directoryLock(this->directoryMutex);
	for (size_t i = 0; i < directoryRefs.size(); i++)
	{
		if (directoryRefs[i].fileID == childFileID)
			directoryRefs[i].fileID = 0;
	}
}
bool BundleFileContextInfo::hasAnyChanges(AppContext &appContext)
{
	if (this->getBundleFileContext() != nullptr && this->getBundleFileContext()->getReaderIsModified())
		return true;
	std::shared_lock directoryLock(this->directoryMutex);
	for (size_t i = 0; i < directoryRefs.size(); i++)
	{
		unsigned int refFileID = directoryRefs[i].fileID;
		bool hasChanges = false;
		if (refFileID != 0)
		{
			FileContextInfo_ptr pContextInfo = appContext.getContextInfo(refFileID);
			if (pContextInfo && pContextInfo->hasAnyChanges(appContext))
				hasChanges = true;
		}
		else if ((!directoryRefs[i].entryRemoved && directoryRefs[i].pOverrideReader != nullptr)
			|| (getBundleFileContext() != nullptr
			    && (directoryRefs[i].entryRemoved || (!directoryRefs[i].entryRemoved && directoryRefs[i].entryNameOverridden))
				&& i < getBundleFileContext()->getEntryCount()))
			hasChanges = true;
		if (hasChanges)
			return true;
	}
	return false;
}
bool BundleFileContextInfo::hasNewChanges(AppContext &appContext)
{
	std::shared_lock directoryLock(this->directoryMutex);
	for (size_t i = 0; i < directoryRefs.size(); i++)
	{
		unsigned int refFileID = directoryRefs[i].fileID;
		bool hasChanges = false;
		if (refFileID != 0)
		{
			FileContextInfo_ptr pContextInfo = appContext.getContextInfo(refFileID);
			if (pContextInfo && pContextInfo->hasNewChanges(appContext))
				hasChanges = true;
		}
		else if (directoryRefs[i].newChangeFlag)
			hasChanges = true;
		if (hasChanges)
		{
			return true;
		}
	}
	return false;
}
std::string BundleFileContextInfo::getNewEntryName(size_t index, bool acquireLock)
{
	//Note: May be called while the caller has a exclusive lock on directoryLock.
	std::shared_lock<std::shared_mutex> directoryLock(this->directoryMutex, std::defer_lock);
	if (acquireLock) directoryLock.lock();
	if (index < directoryRefs.size() && directoryRefs[index].entryNameOverridden)
	{
		std::string ret = directoryRefs[index].newEntryName;
		return ret;
	}
	if (acquireLock) directoryLock.unlock();
	if (!getBundleFileContext()) return "";
	auto res = getBundleFileContext()->getEntryName(index);
	if (res == nullptr) return "";
	return res;
}
std::string BundleFileContextInfo::getBundlePathName()
{
	std::string firstEntryName = getNewEntryName(0);
	if (firstEntryName.starts_with("BuildPlayer-") || firstEntryName.starts_with("CustomAssetBundle") || firstEntryName.starts_with("CAB"))
	{
		size_t iSlash = firstEntryName.rfind('/');
		if (iSlash != std::string::npos)
			firstEntryName = firstEntryName.substr(iSlash + 1);
		size_t iExtension = firstEntryName.rfind('.');
		if (iExtension != std::string::npos)
			firstEntryName = firstEntryName.substr(0, iExtension);
		return firstEntryName;
	}
	return "";
}
//Returns nullptr on error (e.g. index out of range, I/O error, ...), or if the entry has been deleted.
std::shared_ptr<IAssetsReader> BundleFileContextInfo::makeEntryReader(size_t index, bool &readerIsModified)
{
	readerIsModified = false;
	BundleFileContext *pFileContext = this->getBundleFileContext();
	if (pFileContext == nullptr)
		return nullptr;
	struct CustomDeleter {
		CustomDeleter(std::shared_ptr<IAssetsReader> _parentReader)
			: parentReader(std::move(_parentReader))
		{}
		std::shared_ptr<IAssetsReader> parentReader;
		void operator()(IAssetsReader *pReaderToFree)
		{
			Free_AssetsReader(pReaderToFree);
			parentReader.reset();
		}
	};
	std::shared_lock directoryLock(this->directoryMutex);
	if (index < directoryRefs.size())
	{
		bool doRet = false;
		std::shared_ptr<IAssetsReader> ret;
		if (directoryRefs[index].entryRemoved)
			doRet = true;
		else if (directoryRefs[index].pOverrideReader != nullptr)
		{
			doRet = true;
			ret = std::shared_ptr<IAssetsReader>(
				directoryRefs[index].pOverrideReader->CreateView(),
				CustomDeleter(directoryRefs[index].pOverrideReader));
			readerIsModified = true;
		}
		if (doRet)
		{
			return ret;
		}
	}
	directoryLock.unlock();
	if (index < pFileContext->getEntryCount())
	{
		std::shared_ptr<IAssetsReader> ret = pFileContext->makeEntryReader(index);
		if (ret != nullptr)
		{
			IAssetsReader *retView = ret->CreateView();
			readerIsModified = pFileContext->getReaderIsModified();
			return std::shared_ptr<IAssetsReader>(retView, CustomDeleter(std::move(ret)));
		}
	}
	return nullptr;
}
std::vector<std::unique_ptr<BundleReplacer>> BundleFileContextInfo::makeEntryReplacers(class AppContext &appContext, bool resetChangedFlag)
{
	BundleFileContext *pFileContext = this->getBundleFileContext();
	assert(pFileContext != nullptr);

	std::unique_lock<std::shared_mutex> directoryLockExclusive(this->directoryMutex, std::defer_lock);
	std::shared_lock<std::shared_mutex> directoryLockShared(this->directoryMutex, std::defer_lock);
	if (resetChangedFlag)
		directoryLockExclusive.lock();
	else
		directoryLockShared.lock();
	std::vector<std::unique_ptr<BundleReplacer>> pReplacers;
	uint32_t nEntries = (uint32_t)std::min<size_t>(UINT_MAX, directoryRefs.size());
	for (uint32_t i = 0; i < nEntries; i++)
	{
		const char *oldName = pFileContext->getEntryName(i);
		assert(oldName != nullptr || i >= pFileContext->getEntryCount());
		if (oldName == nullptr)
		{
			if (i < pFileContext->getEntryCount())
				continue;
		}
		std::string newName = getNewEntryName(i, false);
		assert(!newName.empty() || oldName == nullptr);
		if (newName.empty()) newName.assign(oldName);
		unsigned int refFileID = directoryRefs[i].fileID;
		uint32_t bundleEntryIndex = (i < pFileContext->getEntryCount()) ? (uint32_t)i : (uint32_t)-1;
		FileContextInfo_ptr pContextInfo = (refFileID != 0) ? appContext.getContextInfo(refFileID) : nullptr;
		if (pContextInfo && pContextInfo->hasAnyChanges(appContext))
		{
			const char *replacerOldName = oldName;
			if (pContextInfo->getFileContext())
			{
				IAssetsReader *pModifiedReader = nullptr;
				IFileContext *pContext = pContextInfo->getFileContext();
				switch (pContext->getType())
				{
				case FileContext_Assets:
					if (reinterpret_cast<AssetsFileContext*>(pContext)->getReaderIsModified())
						pModifiedReader = reinterpret_cast<AssetsFileContext*>(pContext)->createReaderView();
					break;
				case FileContext_Bundle:
					if (reinterpret_cast<BundleFileContext*>(pContext)->getReaderIsModified())
						pModifiedReader = reinterpret_cast<BundleFileContext*>(pContext)->createReaderView();
					break;
				case FileContext_Resources:
					if (reinterpret_cast<ResourcesFileContext*>(pContext)->getReaderIsModified())
						pModifiedReader = reinterpret_cast<ResourcesFileContext*>(pContext)->createReaderView();
					break;
				case FileContext_Generic:
					if (reinterpret_cast<GenericFileContext*>(pContext)->getReaderIsModified())
						pModifiedReader = reinterpret_cast<GenericFileContext*>(pContext)->createReaderView();
					break;
				}
				if (pModifiedReader != nullptr)
				{
					QWORD size = 0;
					if (!pModifiedReader->Seek(AssetsSeek_End, 0))
						assert(false);
					if (!pModifiedReader->Tell(size))
						assert(false);
					if (!pModifiedReader->Seek(AssetsSeek_Begin, 0))
						assert(false);

					struct {
						std::shared_ptr<FileContextInfo> contextInfoRef;
						void operator()(IAssetsReader* pReader) { Free_AssetsReader(pReader); contextInfoRef.reset(); }
					} fullReaderDeleter;
					fullReaderDeleter.contextInfoRef = pContextInfo;

					pReplacers.push_back(std::unique_ptr<BundleReplacer>(MakeBundleEntryModifier(oldName, newName.c_str(),
						(pContext->getType() == FileContext_Assets),
						std::shared_ptr<IAssetsReader>(pModifiedReader, fullReaderDeleter), size,
						0, 0, bundleEntryIndex)));
					replacerOldName = newName.c_str();
				}
			}
			std::unique_ptr<BundleReplacer> curReplacer = pContextInfo->makeBundleReplacer(appContext, replacerOldName, newName.c_str(), bundleEntryIndex, resetChangedFlag);
			if (curReplacer)
				pReplacers.push_back(std::move(curReplacer));
		}
		else if (directoryRefs[i].entryRemoved)
		{
			if (i < pFileContext->getEntryCount())
				pReplacers.push_back(std::unique_ptr<BundleReplacer>(MakeBundleEntryRemover(oldName, bundleEntryIndex)));
		}
		else if (directoryRefs[i].pOverrideReader != nullptr)
		{
			IAssetsReader *pReaderView = directoryRefs[i].pOverrideReader->CreateView();
			QWORD size = 0;
			if (!pReaderView->Seek(AssetsSeek_End, 0))
				assert(false);
			if (!pReaderView->Tell(size))
				assert(false);
			if (!pReaderView->Seek(AssetsSeek_Begin, 0))
				assert(false);
			struct {
				std::shared_ptr<IAssetsReader> baseReaderRef;
				void operator()(IAssetsReader* pReader) { Free_AssetsReader(pReader); baseReaderRef.reset(); }
			} fullReaderDeleter;
			fullReaderDeleter.baseReaderRef = directoryRefs[i].pOverrideReader;
			pReplacers.push_back(std::unique_ptr<BundleReplacer>(MakeBundleEntryModifier(oldName, newName.c_str(), 
				directoryRefs[i].hasSerializedData, std::shared_ptr<IAssetsReader>(pReaderView, fullReaderDeleter),
				size, 0, 0, bundleEntryIndex)));
		}
		else if (directoryRefs[i].entryNameOverridden && (bundleEntryIndex != (uint32_t)-1))
		{
			pReplacers.push_back(std::unique_ptr<BundleReplacer>(MakeBundleEntryRenamer(oldName, newName.c_str(), 
				directoryRefs[i].hasSerializedData, bundleEntryIndex)));
		}
		if (resetChangedFlag)
			directoryRefs[i].newChangeFlag = false;
	}
	return pReplacers;
}
uint64_t BundleFileContextInfo::write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag)
{
	assert(this->getBundleFileContext() != nullptr);
	if (this->getBundleFileContext() == nullptr || this->getBundleFileContext()->getBundleFile() == nullptr)
		return 0;
	BundleFileContext *pFileContext = this->getBundleFileContext();
	IAssetsReader *pReaderView = pFileContext->getReaderUnsafe()->CreateView();
	assert(pReaderView != nullptr);
	if (pReaderView == nullptr)
		return 0;
	IAssetsWriter *pWriterOffset = Create_AssetsWriterToWriterOffset(pWriter, start);

	std::vector<std::unique_ptr<BundleReplacer>> pReplacers = makeEntryReplacers(appContext, resetChangedFlag);
	std::vector<BundleReplacer*> pReplacers_raw(pReplacers.size());
	for (size_t i = 0; i < pReplacers.size(); ++i)
		pReplacers_raw[i] = pReplacers[i].get();
	bool written = pFileContext->getBundleFile()->Write(pReaderView, pWriter, pReplacers_raw.data(), pReplacers_raw.size());

	Free_AssetsReader(pReaderView);
	uint64_t endPos = 0;
	if (written && (!pWriterOffset->Seek(AssetsSeek_End, 0) || !pWriterOffset->Tell(endPos)))
		assert(false);
	Free_AssetsWriter(pWriterOffset);
	return endPos;
}
std::unique_ptr<BundleReplacer> BundleFileContextInfo::makeBundleReplacer(class AppContext &appContext, 
	const char *oldName, const char *newName, uint32_t bundleIndex, 
	bool resetChangedFlag)
{
	assert(this->getBundleFileContext() != nullptr);
	if (this->getBundleFileContext() == nullptr || this->getBundleFileContext()->getBundleFile() == nullptr)
		return nullptr;
	std::vector<std::unique_ptr<BundleReplacer>> pReplacers = makeEntryReplacers(appContext, resetChangedFlag);
	if (pReplacers.empty())
		return nullptr;
	return MakeBundleEntryModifierFromBundle(oldName, newName, std::move(pReplacers), bundleIndex);
}
void BundleFileContextInfo::onDirectoryReady(class AppContext &appContext)
{
	{
		std::scoped_lock directoryLock(this->directoryMutex);
		this->directoryRefs.clear();
		this->directoryRefs.resize(pContext->getEntryCount());
		for (size_t i = 0; i < pContext->getEntryCount(); ++i)
			this->directoryRefs[i].hasSerializedData = pContext->hasSerializedData(i);
	}
	
	for (size_t i = 0; this->modificationsToApply && i < this->modificationsToApply->replacers.size(); ++i)
	{
		std::shared_ptr<GenericReplacer> pReplacer = this->modificationsToApply->replacers[i].pReplacer;
		BundleReplacer *pBundleReplacer = reinterpret_cast<BundleReplacer*>(this->modificationsToApply->replacers[i].pReplacer.get());
		const char *origEntryName = pBundleReplacer->GetOriginalEntryName();
		size_t index = (size_t)-1;
		if (origEntryName != nullptr)
		{
			for (size_t k = 0; k < pContext->getEntryCount(); ++k)
			{
				const char *curEntryName = pContext->getEntryName(k);
				if (curEntryName != nullptr && !stricmp(curEntryName, origEntryName))
				{
					index = k;
					break;
				}
			}
			std::shared_lock directoryLock(this->directoryMutex);
			for (size_t k = 0; k < this->directoryRefs.size(); ++k)
			{
				if (this->directoryRefs[k].entryNameOverridden && !this->directoryRefs[k].newEntryName.compare(origEntryName))
				{
					index = k;
					break;
				}
			}
		}
		//std::shared_ptr<IAssetsReader> pOrigEntryReader = pContext->makeEntryReader();
		switch (pBundleReplacer->GetType())
		{
		case BundleReplacement_Remove:
			assert(index != (size_t)-1);
			if (index != (size_t)-1)
				this->removeEntry(appContext, index);
			break;
		case BundleReplacement_Rename:
			{
				const char *newEntryName = pBundleReplacer->GetEntryName();
				assert(index != (size_t)-1 && newEntryName != nullptr);
				if (index != (size_t)-1 && newEntryName != nullptr)
					this->renameEntry(appContext, index, newEntryName);
			}
			break;
		case BundleReplacement_AddOrModify:
			{
				const char *newEntryName = pBundleReplacer->GetEntryName();
				assert(newEntryName != nullptr);
				if (!newEntryName)
					newEntryName = origEntryName;
				if (auto* pResourceModifier = dynamic_cast<BundleEntryModifierByResources*>(pBundleReplacer))
				{
					//Generate an empty reader, and then add a new child file with the resource replacer.
					assert(!pResourceModifier->RequiresEntryReader());
					std::shared_ptr<IAssetsReader> pReader = std::shared_ptr<IAssetsReader>(
						Create_AssetsReaderFromMemory(nullptr, 0, false, nullptr),
						Free_AssetsReader);
					std::shared_ptr<BundleReplacer> pBundleReplacer = std::reinterpret_pointer_cast<BundleReplacer>(pReplacer);
					this->modificationsToApply->subFiles.emplace_back(appContext, pBundleReplacer, BundleReplacer_BundleEntryModifierByResources);
					if (index != (size_t)-1)
					{
						this->overrideEntryReader(appContext, index, pReader, pBundleReplacer->HasSerializedData(), std::string(newEntryName));
					}
					else
					{
						this->addEntry(appContext, pReader, pBundleReplacer->HasSerializedData(), std::string(newEntryName));
					}
				}
				else
				{
					std::shared_ptr<IAssetsReader> pReader = MakeReaderFromBundleEntryModifier(std::shared_ptr<BundleReplacer>(pReplacer, pBundleReplacer));
					assert(pReader != nullptr);
					if (pReader != nullptr)
					{
						if (index != (size_t)-1)
						{
							this->overrideEntryReader(appContext, index, pReader, pBundleReplacer->HasSerializedData(), std::string(newEntryName));
						}
						else
						{
							this->addEntry(appContext, pReader, pBundleReplacer->HasSerializedData(), std::string(newEntryName));
						}
					}
				}
			}
			break;
		}
	}
	if (this->modificationsToApply)
		this->modificationsToApply->replacers.clear();
}
bool BundleFileContextInfo::renameEntry(class AppContext &appContext, size_t index, std::string newEntryName)
{
	bool ret = false;
	std::unique_lock directoryLock(this->directoryMutex);
	if (index < this->directoryRefs.size() && (this->directoryRefs[index].fileID != 0 || !this->directoryRefs[index].entryRemoved))
	{
		this->directoryRefs[index].newEntryName = newEntryName;
		this->directoryRefs[index].entryNameOverridden = true;
		this->directoryRefs[index].newChangeFlag = true;
		ret = true;
		auto pChildInfo = appContext.getContextInfo(this->directoryRefs[index].fileID);
		if (pChildInfo)
		{
			pChildInfo->setFileName(std::move(newEntryName));
		}
	}
	directoryLock.unlock();
	appContext.OnChangeBundleEntry_Async(this, index);
	return ret;
}
bool BundleFileContextInfo::overrideEntryReader(AppContext &appContext, size_t index, std::shared_ptr<IAssetsReader> pReader, bool hasSerializedData)
{
	bool ret = false;
	std::unique_lock directoryLock(this->directoryMutex);
	if (index < this->directoryRefs.size() && (this->directoryRefs[index].fileID != 0 || !this->directoryRefs[index].entryRemoved))
	{
		this->directoryRefs[index].pOverrideReader = std::move(pReader);
		this->directoryRefs[index].hasSerializedData = hasSerializedData;
		this->directoryRefs[index].newChangeFlag = true;
		ret = true;
	}
	directoryLock.unlock();
	appContext.OnChangeBundleEntry_Async(this, index);
	return ret;
}
bool BundleFileContextInfo::removeEntry(class AppContext &appContext, size_t index)
{
	bool ret = false;
	std::unique_lock directoryLock(this->directoryMutex);
	if (index < this->directoryRefs.size())
	{
		if (!this->directoryRefs[index].entryRemoved)
		{
			this->directoryRefs[index].entryRemoved = true;
			this->directoryRefs[index].newChangeFlag = true;
		}
		ret = true;
	}
	directoryLock.unlock();
	appContext.OnChangeBundleEntry_Async(this, index);
	return ret;
}
size_t BundleFileContextInfo::addEntry(class AppContext &appContext, std::shared_ptr<IAssetsReader> pReader, bool hasSerializedData, std::string entryName)
{
	std::unique_lock directoryLock(this->directoryMutex);

	this->directoryRefs.push_back(BundleFileDirectoryInfo());
	size_t index = this->directoryRefs.size() - 1;
	this->directoryRefs[index].pOverrideReader = std::move(pReader);
	this->directoryRefs[index].hasSerializedData = hasSerializedData;
	this->directoryRefs[index].newEntryName = std::move(entryName);
	this->directoryRefs[index].entryNameOverridden = true;
	this->directoryRefs[index].newChangeFlag = true;

	directoryLock.unlock();
	appContext.OnChangeBundleEntry_Async(this, index);
	return index;
}
//Checks whether an entry has been removed. Can return true even if the child file still is open.
bool BundleFileContextInfo::entryIsRemoved(size_t index)
{
	bool ret = false;
	std::shared_lock directoryLock(this->directoryMutex);
	if (index < this->directoryRefs.size())
	{
		ret = this->directoryRefs[index].entryRemoved;
	}
	return ret;
}
//Checks whether an entry has changed (renamed, reader overridden, removed, added). Does not check for changes in the child FileContextInfo.
bool BundleFileContextInfo::entryHasChanged(size_t index)
{
	bool ret = false;
	std::shared_lock directoryLock(this->directoryMutex);
	if (index < this->directoryRefs.size())
	{
		if (this->getBundleFileContext() && index >= this->getBundleFileContext()->getEntryCount())
			ret = true;
		else if (this->directoryRefs[index].entryNameOverridden || this->directoryRefs[index].entryRemoved
			|| this->directoryRefs[index].pOverrideReader != nullptr)
			ret = true;
	}
	return ret;
}
size_t BundleFileContextInfo::getEntryCount()
{
	std::shared_lock directoryLock(this->directoryMutex);
	size_t ret = this->directoryRefs.size();
	return ret;
}
BundleFileContextInfo::DecompressTask::DecompressTask(AppContext &appContext, std::shared_ptr<BundleFileContextInfo> &pContextInfo, std::string outputPath)
	: appContext(appContext), pFileContextInfo(pContextInfo), outputPath(outputPath),
	decompressStatus(static_cast<EBundleFileDecompressStatus>(TaskResult_Canceled)), 
	reopenStatus(static_cast<EBundleFileOpenStatus>(TaskResult_Canceled))
{
	assert(pFileContextInfo->getBundleFileContext() && pFileContextInfo->getBundleFileContext()->getBundleFile());
	name = "Decompress bundle : " + pContextInfo->getFileName() + "";
}
const std::string &BundleFileContextInfo::DecompressTask::getName()
{
	return name;
}
TaskResult BundleFileContextInfo::DecompressTask::execute(TaskProgressManager &progressManager)
{
	if (!this->pFileContextInfo->getBundleFileContext() || !this->pFileContextInfo->getBundleFileContext()->getBundleFile())
	{
		progressManager.logMessage("Bundle file not loaded!");
		return -1;
	}
	progressManager.setProgress(0, 250);
	progressManager.setProgressDesc("Decompressing the bundle file.");
	//Assuming that this is the first and only time DecompressSync is called.
	this->decompressStatus = this->pFileContextInfo->getBundleFileContext()->DecompressSync(nullptr, this->outputPath);
	if (this->decompressStatus != BundleFileDecompressStatus_OK)
	{
		switch (this->decompressStatus)
		{
		case BundleFileDecompressStatus_ErrOutFileOpen:
			progressManager.logMessage("Unable to open the output file for decompression!");
			break;
		case BundleFileDecompressStatus_ErrDecompress:
			progressManager.logMessage("Bundle file not loaded!");
			break;
		default:
			progressManager.logMessage("An unknown error occured during decompression!");
		}
		progressManager.setProgress(250, 250);
		return -3;
	}
	std::shared_ptr<IAssetsReader> pDecompressedReader = std::shared_ptr<IAssetsReader>(
		Create_AssetsReaderFromFile(this->outputPath.c_str(), true, RWOpenFlags_Immediately),
		Free_AssetsReader);
	if (pDecompressedReader == nullptr)
	{
		progressManager.logMessage("Cannot reopen the decompressed file!");
		return -2;
	}
	progressManager.setProgress(150, 250);
	progressManager.setProgressDesc("Reopening the bundle file.");

	IFileContext *pParentFileContext = this->pFileContextInfo->getBundleFileContext()->getParent();
	BundleFileContext *pNewBundleContext;
	if (pParentFileContext)
		pNewBundleContext = new BundleFileContext(this->pFileContextInfo->getBundleFileContext()->getFilePath(), pParentFileContext, pDecompressedReader);
	else
		pNewBundleContext = new BundleFileContext(this->pFileContextInfo->getBundleFileContext()->getFilePath(), pDecompressedReader);

	this->reopenStatus = pNewBundleContext->OpenInsideTask(&progressManager, 150, 250);
	progressManager.setProgress(250, 250);
	if (this->reopenStatus >= 0 && this->reopenStatus != BundleFileOpenStatus_Pend)
	{
		if (this->reopenStatus == BundleFileOpenStatus_CompressedDirectory ||
			this->reopenStatus == BundleFileOpenStatus_CompressedData)
		{
			progressManager.logMessage("The decompressed file still appears to be compressed!");
		}
		else
		{
			this->pFileContextInfo->CloseContext();
			this->pFileContextInfo->pContext = pNewBundleContext;
			this->pFileContextInfo->isDecompressed = true;
			this->pFileContextInfo->onDirectoryReady(appContext);
			return 0;
		}
	}
	delete pNewBundleContext;

	return -1;
}
std::shared_ptr<BundleFileContextInfo::DecompressTask> BundleFileContextInfo::EnqueueDecompressTask(
	AppContext &appContext, std::shared_ptr<BundleFileContextInfo> &selfPointer, std::string outputPath)
{
	assert(selfPointer.get() == this);
	std::shared_ptr<DecompressTask> pTask = std::make_shared<DecompressTask>(appContext, selfPointer, outputPath);
	if (appContext.taskManager.enqueue(pTask))
		return pTask;
	return nullptr;
}

#pragma region ResourcesFileContextInfo
ResourcesFileContextInfo::ResourcesFileContextInfo(ResourcesFileContext *pContext, unsigned int fileID, unsigned int parentFileID)
	: FileContextInfo(fileID, parentFileID), pContext(pContext), changedFlag(false)
{
	IAssetsReader* pReader = this->pContext->getReaderUnsafe();
	if (pReader == nullptr)
		throw std::invalid_argument("ResourcesFileContextInfo: Context has a null reader!");
	IAssetsReader* pReaderView = pReader->CreateView();
	if (pReaderView == nullptr)
		throw std::runtime_error("ResourcesFileContextInfo: Unable to create a reader view!");
	pReaderView->Seek(AssetsSeek_End, 0);
	originalFileSize = 0;
	pReaderView->Tell(originalFileSize);
	Free_AssetsReader(pReaderView);

	ReplacedResourceDesc placeholderDesc = {};
	placeholderDesc.outRangeBegin = 0;
	placeholderDesc.rangeSize = originalFileSize;
	placeholderDesc.reader = nullptr;
	placeholderDesc.inRangeBegin = 0;
	placeholderDesc.fromOriginalFile = true;
	resources.push_front(placeholderDesc);
}
ResourcesFileContextInfo::~ResourcesFileContextInfo()
{
	IAssetsReader *pReader = this->pContext->getReaderUnsafe();
	this->pContext->Close();
	delete this->pContext;
	this->pContext = nullptr;
}
IFileContext *ResourcesFileContextInfo::getFileContext()
{
	return this->pContext;
}
void ResourcesFileContextInfo::getChildFileIDs(std::vector<unsigned int> &childFileIDs)
{
	childFileIDs.clear();
}
void ResourcesFileContextInfo::onCloseChild(unsigned int childFileID)
{
}
bool ResourcesFileContextInfo::hasNewChanges(class AppContext &appContext)
{
	return this->changedFlag;
}
bool ResourcesFileContextInfo::hasAnyChanges(class AppContext &appContext)
{
	if (this->getResourcesFileContext() != nullptr && this->getResourcesFileContext()->getReaderIsModified())
		return true;
	std::shared_lock resourcesLock(this->resourcesMutex);
	uint64_t curOutRange = 0;
	for (auto resourcesIt = this->resources.begin(); resourcesIt != this->resources.end(); ++resourcesIt)
	{
		if (resourcesIt->rangeSize > 0)
		{
			if (!resourcesIt->fromOriginalFile || resourcesIt->reader != nullptr)
				return true;
			if (resourcesIt->inRangeBegin != resourcesIt->outRangeBegin)
				return true;
		}
		if (resourcesIt->outRangeBegin != curOutRange)
		{
			assert(false);
			return true;
		}
		curOutRange += resourcesIt->rangeSize;
	}
	if (curOutRange != originalFileSize)
		return true;
	return false;
}
uint64_t ResourcesFileContextInfo::write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag)
{
	assert(this->pContext != nullptr);
	if (this->pContext == nullptr)
		return 0;
	IAssetsReader* pOrigReader = this->pContext->getReaderUnsafe()->CreateView();
	std::vector<uint8_t> buffer(1024 * 1024); //1MiB

	std::shared_lock resourcesLock(this->resourcesMutex);
	uint64_t curOutPos = 0;
	bool errorsOccured = false;
	for (auto resourcesIt = this->resources.begin(); resourcesIt != this->resources.end(); ++resourcesIt)
	{
		assert(resourcesIt->outRangeBegin == curOutPos);
		IAssetsReader* pCurReader = resourcesIt->reader.get();
		if (pCurReader == nullptr && resourcesIt->fromOriginalFile)
			pCurReader = this->pContext->getReaderUnsafe();
		if (pCurReader == nullptr)
		{
			//Fill buffer with zeroes.
			size_t bufSize = buffer.size();
			buffer.clear();
			buffer.resize(bufSize, 0);
		}
		else
		{
			pCurReader = pCurReader->CreateView();
			if (pCurReader == nullptr)
			{
				assert(false);
				errorsOccured = true;
				break;
			}
			pCurReader->SetPosition(resourcesIt->inRangeBegin);
		}
		for (uint64_t i = 0; i < resourcesIt->rangeSize; )
		{
			uint64_t curRead = 0;
			//Read a block of data.
			if (pCurReader != nullptr)
				curRead = pCurReader->Read(std::min<uint64_t>(buffer.size(), resourcesIt->rangeSize - i), buffer.data());
			else
				curRead = std::min<uint64_t>(buffer.size(), resourcesIt->rangeSize - i); //Zeroes already in the buffer.
			if (curRead == 0)
			{
				assert(false);
				errorsOccured = true;
				break;
			}
			//Write the data
			uint64_t curWritten = pWriter->Write(start + curOutPos, curRead, buffer.data());
			curOutPos += curWritten;
			if (curWritten != curRead)
			{
				assert(false);
				errorsOccured = true;
				break;
			}
			i += curWritten;
		}
		if (pCurReader != nullptr)
			Free_AssetsReader(pCurReader);
	}
	if (!errorsOccured && resetChangedFlag)
		this->changedFlag = false;
	return curOutPos;
}
std::unique_ptr<BundleReplacer> ResourcesFileContextInfo::makeBundleReplacer(class AppContext &appContext, 
	const char *oldName, const char *newName, uint32_t bundleIndex, 
	bool resetChangedFlag)
{
	assert(this->pContext != nullptr);
	if (this->pContext == nullptr)
		return nullptr;
	std::shared_lock resourcesLock(this->resourcesMutex);
	if (resetChangedFlag)
		this->changedFlag = false;
	return MakeBundleEntryModifierByResources(oldName, newName,
		std::vector<ReplacedResourceDesc>(this->resources.begin(), this->resources.end()), 0, bundleIndex);
}
bool ResourcesFileContextInfo::setByReplacer(class AppContext& appContext, BundleReplacer* pReplacer)
{
	if (BundleEntryModifierByResources* pBundleModifier = dynamic_cast<BundleEntryModifierByResources*>(pReplacer))
	{
		std::shared_lock resourcesLock(this->resourcesMutex);
		resources.clear();
		const auto& resourcesIn = pBundleModifier->getResources();
		resources.assign(resourcesIn.begin(), resourcesIn.end());
		this->changedFlag = hasAnyChanges(appContext);
		return true;
	}
	return false;
}
void ResourcesFileContextInfo::addResource(std::shared_ptr<IAssetsReader> pReader, uint64_t readerOffs, uint64_t size, uint64_t &resourcesFilePos)
{
	std::unique_lock resourcesLock(this->resourcesMutex);
	resourcesFilePos = 0;
	if (!resources.empty())
		resourcesFilePos = resources.back().outRangeBegin + resources.back().rangeSize;
	resources.emplace_back();
	resources.back().outRangeBegin = resourcesFilePos;
	resources.back().rangeSize = size;
	resources.back().reader = pReader;
	resources.back().inRangeBegin = readerOffs;
	resources.back().fromOriginalFile = false;
	this->changedFlag = (size > 0);
}
std::shared_ptr<IAssetsReader> ResourcesFileContextInfo::getResource(std::shared_ptr<ResourcesFileContextInfo> selfRef, uint64_t offs, uint64_t size)
{
	if (selfRef.get() != this)
		throw std::invalid_argument("ResourcesFileContextInfo::getResource: selfRef does not point to this!");
	std::shared_lock resourcesLock(this->resourcesMutex);
	//TODO: Implement a faster lookup by file position, e.g. using a map (probably not necessary for now).
	
	//Find the first resource in range (returns resources.end() if none are found).
	auto itFirstInRange = std::find_if(resources.begin(), resources.end(),
		[offs](const ReplacedResourceDesc& a) {return a.outRangeBegin + a.rangeSize >= offs; });
	//Find the last resource in range.
	uint64_t rangeEnd = (size < std::numeric_limits<uint64_t>::max()-offs) ? (offs + size) : std::numeric_limits<uint64_t>::max();
	auto itFirstOutOfRange = std::find_if(itFirstInRange, resources.end(),
		[rangeEnd](const ReplacedResourceDesc& a) {return a.outRangeBegin >= rangeEnd; });

	if (itFirstInRange == resources.end() || itFirstInRange == itFirstOutOfRange)
		return nullptr;
	class ResourcesReader : public IAssetsReader
	{
		std::vector<ReplacedResourceDesc> resources;
		std::shared_ptr<IAssetsReader> origReader;

		std::unique_ptr<IAssetsReader> origReaderView;
		std::vector<std::unique_ptr<IAssetsReader>> readerViews;

		std::recursive_mutex positionMutex;
		QWORD pos = 0;
		size_t resourceIdx = 0;

		void createViews()
		{
			origReaderView.reset();
			readerViews.clear();
			if (origReader)
			{
				origReaderView.reset(origReader->CreateView());
				if (origReaderView == nullptr)
					throw std::runtime_error("Unable to open a reader view!");
			}
			readerViews.resize(resources.size());
			for (size_t i = 0; i < resources.size(); ++i)
			{
				if (resources[i].reader != nullptr)
				{
					readerViews[i].reset(resources[i].reader->CreateView());
					if (readerViews[i] == nullptr)
						throw std::runtime_error("Unable to open a reader view!");
				}
			}
		}
		inline QWORD getSize()
		{
			return resources.empty() ? 0 : (resources.back().outRangeBegin + resources.back().rangeSize);
		}
		inline IAssetsReader* getReaderViewFor(size_t resourceIdx)
		{
			IAssetsReader* pReader = nullptr;
			if (resources[resourceIdx].reader)
			{
				//Customized resource.
				pReader = readerViews[resourceIdx].get();
				if (pReader == nullptr)
				{
					assert(false);
					return nullptr;
				}
			}
			else if (resources[resourceIdx].fromOriginalFile)
			{
				//Resource from the original file.
				pReader = origReaderView.get();
				if (pReader == nullptr)
				{
					assert(false);
					return nullptr;
				}
			}
			return pReader;
		}
	public:
		ResourcesReader(std::vector<ReplacedResourceDesc> _resources, std::shared_ptr<IAssetsReader> _origReader)
			: resources(std::move(_resources)), origReader(std::move(_origReader))
		{
			createViews();
		}
		ResourcesReader(const ResourcesReader& other)
		{
			(*this) = other;
		}
		ResourcesReader& operator=(const ResourcesReader& other)
		{
			this->resources = other.resources;
			this->origReader = other.origReader;
			this->createViews();
			return *this;
		}

		bool Reopen()
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			if (resourceIdx < resources.size())
			{
				if (readerViews[resourceIdx] != nullptr)
					return readerViews[resourceIdx]->Reopen();
				else if (resources[resourceIdx].fromOriginalFile)
					return origReaderView->Reopen();
			}
			return true;
		}
		bool IsOpen()
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			if (resourceIdx < resources.size())
			{
				if (readerViews[resourceIdx] != nullptr)
					return readerViews[resourceIdx]->IsOpen();
				else if (resources[resourceIdx].fromOriginalFile)
					return origReaderView->IsOpen();
			}
			return true;
		}
		bool Close() { return false; }

		AssetsRWTypes GetType() { return AssetsRWType_Reader; }
		AssetsRWClasses GetClass() { return AssetsRWClass_Unknown; }
		bool IsView() { return true; }

		bool Tell(QWORD& pos)
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			pos = this->pos;
			return true;
		}
		bool Seek(AssetsSeekTypes origin, long long offset)
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			bool ret = false;
			QWORD newPos = this->pos;
			switch (origin)
			{
			case AssetsSeek_Begin:
				if (offset < 0) return false;
				newPos = offset;
				break;
			case AssetsSeek_Cur:
				if (offset < 0)
				{
					offset = -offset;
					if (offset < 0) return false; //INT64_MIN
					if ((unsigned long long)offset > newPos) return false;
					newPos -= (unsigned long long)offset;
				}
				else
					newPos += (unsigned long long)offset;
				break;
			case AssetsSeek_End:
				if (offset > 0) return false;
				offset = -offset;
				if (offset < 0) return false; //INT64_MIN
				if ((unsigned long long)offset > getSize())
					return false;
				newPos = resources.empty()
					? 0
					: (resources.back().outRangeBegin + resources.back().rangeSize) - (unsigned long long)offset;
				break;
			}
			return SetPosition(newPos);
		}
		bool SetPosition(QWORD pos)
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			if (pos > getSize())
				return false;
			if (pos == getSize())
			{
				this->pos = pos;
				this->resourceIdx = resources.size();
				return true;
			}
			//Get the first resource with outRangeBegin + rangeSize > pos.
			//-> Since resources is guaranteed to be sorted by out position and without overlapping regions (and start with 0),
			//   this should always return the resource corresponding to pos, or resources.end() if out of range.
			auto itMatchingResource = std::upper_bound(resources.begin(), resources.end(), pos,
				[](QWORD pos, const ReplacedResourceDesc& resource)
				{
					return pos < (resource.outRangeBegin + resource.rangeSize);
				});
			if (itMatchingResource == resources.end())
				return false;
			if (itMatchingResource->outRangeBegin > pos)
			{
				assert(false); //Should not happen due to the requirements for resources.
				return false;
			}
			this->pos = pos;
			this->resourceIdx = std::distance(resources.begin(), itMatchingResource);
			IAssetsReader* pReader = getReaderViewFor(resourceIdx);
			if (pReader != nullptr)
				return pReader->SetPosition(itMatchingResource->inRangeBegin + (pos - itMatchingResource->outRangeBegin));
			return true;
		}

		QWORD Read(QWORD pos, QWORD size, void* outBuffer, bool nullUnread = true)
		{
			std::lock_guard<decltype(positionMutex)> posLock(positionMutex);
			if (pos == (QWORD)-1)
				pos = this->pos;
			QWORD numRead = 0;
			if ((pos != (QWORD)-1) && !SetPosition(pos))
				numRead = 0;
			else
			{
				QWORD remaining = size;
				while (remaining > 0)
				{
					if (resourceIdx >= resources.size())
						break;
					ReplacedResourceDesc& curResource = resources[resourceIdx];
					if (pos >= curResource.outRangeBegin + curResource.rangeSize)
					{
						resourceIdx++;
						continue;
					}
					assert(pos >= curResource.outRangeBegin);
					QWORD bytesToRead = std::min(remaining, curResource.rangeSize - (pos - curResource.outRangeBegin));
					bytesToRead = std::min<QWORD>(bytesToRead, std::numeric_limits<size_t>::max());
					QWORD curBytesRead = 0;
					IAssetsReader* pReader = getReaderViewFor(resourceIdx);
					if (pReader != nullptr)
					{
						curBytesRead = pReader->Read(
							pos - curResource.outRangeBegin + curResource.inRangeBegin,
							bytesToRead,
							&((uint8_t*)outBuffer)[numRead],
							false);
					}
					else
					{
						//Pseudo-resource filled with zeroes.
						memset(&((uint8_t*)outBuffer)[numRead], 0, (size_t)bytesToRead);
						curBytesRead = bytesToRead;
					}
					if (curBytesRead == 0)
						break;
					numRead += curBytesRead;
					remaining -= curBytesRead;
				}
			}
			if (nullUnread && (numRead < size))
				memset(&((uint8_t*)outBuffer)[numRead], 0, size - numRead);
			this->pos = pos + numRead;
			return numRead;
		}
		IAssetsReader *CreateView()
		{
			return new ResourcesReader(*this);
		}
	};
	std::vector<ReplacedResourceDesc> resourcesInRange(itFirstInRange, itFirstOutOfRange);
	if (resourcesInRange.size() > 0)
	{
		//Correct the offsets and resource size of the first entry to match the chosen range start.
		resourcesInRange.front().inRangeBegin += (offs - resourcesInRange.front().outRangeBegin);
		resourcesInRange.front().rangeSize -= (offs - resourcesInRange.front().outRangeBegin);
		resourcesInRange.front().outRangeBegin = offs;
	}
	if (resourcesInRange.size() > 0)
	{
		//Correct the resource size of the last entry to match the chosen range end position.
		if ((resourcesInRange.back().outRangeBegin - offs) + resourcesInRange.back().rangeSize > size)
			resourcesInRange.back().rangeSize = size - (resourcesInRange.back().outRangeBegin - offs);
	}
	//Subtract the start offset from each entry to match the reader positions.
	for (ReplacedResourceDesc& resourceInRange : resourcesInRange)
		resourceInRange.outRangeBegin -= offs;
	return std::make_shared<ResourcesReader>(std::move(resourcesInRange),
		std::shared_ptr<IAssetsReader>(selfRef, this->pContext->getReaderUnsafe()));
}
#pragma endregion ResourcesFileContextInfo



#pragma region GenericFileContextInfo
GenericFileContextInfo::GenericFileContextInfo(GenericFileContext *pContext, unsigned int fileID, unsigned int parentFileID)
	: FileContextInfo(fileID, parentFileID), pContext(pContext), changedFlag(false)
{
}
GenericFileContextInfo::~GenericFileContextInfo()
{
	IAssetsReader *pReader = this->pContext->getReaderUnsafe();
	this->pContext->Close();
	delete this->pContext;
	this->pContext = nullptr;
}
IFileContext *GenericFileContextInfo::getFileContext()
{
	return this->pContext;
}
void GenericFileContextInfo::getChildFileIDs(std::vector<unsigned int> &childFileIDs)
{
	childFileIDs.clear();
}
void GenericFileContextInfo::onCloseChild(unsigned int childFileID)
{
}
bool GenericFileContextInfo::hasAnyChanges(class AppContext &appContext)
{
	if (this->getGenericFileContext() != nullptr && this->getGenericFileContext()->getReaderIsModified())
		return true;
	std::scoped_lock replacementReaderLock(this->replacementReaderMutex);
	bool ret = (!this->replacementReaderHistory.empty());
	return ret;
}
bool GenericFileContextInfo::hasNewChanges(class AppContext &appContext)
{
	return changedFlag;
}
uint64_t GenericFileContextInfo::write(class AppContext &appContext, IAssetsWriter *pWriter, uint64_t start, bool resetChangedFlag)
{
	assert(this->pContext != nullptr);
	if (this->pContext == nullptr)
		return 0;
	std::unique_lock replacementReaderLock(this->replacementReaderMutex);
	IAssetsReader *pReader;
	if (!this->replacementReaderHistory.empty())
		pReader = this->replacementReaderHistory.back()->CreateView();
	else
		pReader = this->pContext->getReaderUnsafe()->CreateView();
	replacementReaderLock.unlock();
	uint64_t fileSize = 0;
	if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(fileSize) || !pReader->Seek(AssetsSeek_Begin, 0))
	{
		Free_AssetsReader(pReader);
		return 0;
	}
	std::vector<uint8_t> buffer(1024 * 1024); //1MiB
	for (uint64_t i = 0; i < fileSize; )
	{
		uint64_t curRead = pReader->Read(std::min<uint64_t>(buffer.size(), fileSize - i), buffer.data());
		if (curRead == 0)
		{
			assert(false);
			fileSize = i;
			break;
		}
		uint64_t curWritten = pWriter->Write(start + i, curRead, buffer.data());
		if (curWritten < curRead)
		{
			assert(false);
			fileSize = curWritten;
			break;
		}
	}
	Free_AssetsReader(pReader);
	if (resetChangedFlag)
		changedFlag = false;
	return fileSize;
}
std::unique_ptr<BundleReplacer> GenericFileContextInfo::makeBundleReplacer(class AppContext &appContext, 
	const char *oldName, const char *newName, uint32_t bundleIndex, 
	bool resetChangedFlag)
{
	assert(this->pContext != nullptr);
	if (this->pContext == nullptr)
		return nullptr;

	std::unique_lock replacementReaderLock(this->replacementReaderMutex);
	IAssetsReader *pReader = nullptr;
	if (!this->replacementReaderHistory.empty())
		pReader = this->replacementReaderHistory.back()->CreateView();
	else
		pReader = this->pContext->getReaderUnsafe()->CreateView();
	replacementReaderLock.unlock();

	if (pReader == nullptr)
		return nullptr;
	assert(pReader->Seek(AssetsSeek_End, 0));
	QWORD totalSize = 0;
	assert(pReader->Tell(totalSize));
	assert(pReader->Seek(AssetsSeek_Begin, 0));
	if (resetChangedFlag)
		changedFlag = false;
	return std::unique_ptr<BundleReplacer>(MakeBundleEntryModifier(oldName, newName, false, pReader, Free_AssetsReader, totalSize, 0, 0, bundleIndex));
}
#pragma endregion GenericFileContextInfo
