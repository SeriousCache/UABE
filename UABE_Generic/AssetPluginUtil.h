#pragma once
#include "AssetIterator.h"
#include "AsyncTask.h"
#include "../AssetsTools/AssetTypeClass.h"
#include "IAssetBatchImportDesc.h"
#include "api.h"
#include <string>
#include <unordered_map>
#include <exception>
#include <functional>

struct AssetUtilDesc
{
	AssetIdentifier asset;
	std::string assetsFileName;
	std::string assetName;
	//Generates an export file path for the current asset.
	//nameCountBuf: Counts how often each assetName has been processed to avoid duplicates in file names.
	// -> Only used if asset.pathID == 0.
	//extension: e.g. ".dat", or "" for no extension.
	//baseDir: Base directory without trailing directory separator.
	UABE_Generic_API std::string makeExportFilePath(std::unordered_map<std::string, size_t>& nameCountBuffer,
		const std::string &extension, std::string baseDir = "") const;
};

//Exception type for AssetExportTask and AssetImportTask.
class AssetUtilError : public std::exception
{
	std::string desc;
	bool mayStop;
public:
	//mayStop: For AssetExportTask::execute and AssetImportTask::execute -
	//         Stop the task if stopOnError is set for the task type.
	inline AssetUtilError(std::string _desc, bool mayStop = false)
		: desc(std::move(_desc)), mayStop(mayStop)
	{}
	inline const char* what()
	{
		return desc.c_str();
	}
	inline bool getMayStop()
	{
		return mayStop;
	}
};

inline void FilterNameForExportInplace(std::string& str)
{
	for (size_t i = 0; i < str.size(); i++)
	{
		switch (str[i])
		{
		case '\\': case '/': case ':': case '?': case '\"': case '<': case '>': case '|':
		case '-':
			str[i] = '_';
			break;
		default:
			break;
		}
	}
}
UABE_Generic_API std::string MakeAssetExportName(pathid_t pathID, std::string assetName,
	std::unordered_map<std::string, size_t>& nameCountBuffer,
	std::string assetsFileName = "");

//Finds the loaded resources file with the given name based on a referencing asset,
// or throws an AssetUtilError if no matches are found.
//If several candidates are found, an arbitrary one is returned.
//If progressManager is given, a warning will be logged if several candidates are found.
UABE_Generic_API std::shared_ptr<ResourcesFileContextInfo> FindResourcesFile(class AppContext& appContext,
	const std::string& streamDataFileName, AssetIdentifier& asset,
	std::optional<std::reference_wrapper<TaskProgressManager>> progressManager);

//Thread-safe cache for type templates.
//If a plugin processes many assets of the same type,
// this class can be used to reduce the total overhead of type template generation.
class TypeTemplateCache
{
	struct ClassIdentifier
	{
		unsigned int fileID;
		int32_t classID;
		inline bool operator==(const ClassIdentifier& other) const
		{
			return (fileID == other.fileID)
				&& (classID == other.classID);
		}
	};
	struct ClassIdentifierHasher
	{
		inline size_t operator()(const ClassIdentifier& entry) const
		{
			return std::hash<uint64_t>()(entry.classID | (((uint64_t)entry.fileID) << 32));
		}
	};
	std::shared_mutex templateCacheMutex;
	std::unordered_map<ClassIdentifier, std::unique_ptr<AssetTypeTemplateField>, ClassIdentifierHasher> templateCache;
public:
	//Throws an AssetUtilError if desc.asset cannot be resolved.
	//The caller can modify the template field,
	// but has to take concurrent callers accessing the same field into account.
	inline AssetTypeTemplateField& getTemplateField(class AppContext& appContext, class AssetIdentifier& asset)
	{
		return getTemplateField(appContext, asset, [](AssetTypeTemplateField&) {});
	}

	//Throws an AssetUtilError if desc.asset cannot be resolved.
	//Calls newTemplateCallback if a template is first encountered.
	// (Concurrent calls with a new asset fileID/classID combo may trigger two calls for equivalent templates).
	UABE_Generic_API AssetTypeTemplateField& getTemplateField(class AppContext& appContext, class AssetIdentifier& asset,
		std::function<void(AssetTypeTemplateField&)> newTemplateCallback);
};

class AssetExportTask : public ITask
{
	std::vector<AssetUtilDesc> assets;
	std::string taskName;
	std::string baseDir;
	std::string extension;
	bool stopOnError;
	bool writeOnCompletionOnly;
public:
	//If assets.size() == 1, baseDir will be used as the full path for the asset.
	//Otherwise, the output paths will be generated via AssetUtilDesc::makeExportFilePath(..).
	//extension: File extension, e.g. ".dat", or "" for no extension.
	//writeOnCompletionOnly: If set, no individual output paths will be generated regardless of whether assets.size() > 1.
	//                       The given baseDir is passed on to onCompletion unless the task was stopped due to an error.
	UABE_Generic_API AssetExportTask(std::vector<AssetUtilDesc> assets, std::string taskName,
		std::string extension, std::string baseDir = "", bool stopOnError = false,
		bool writeOnCompletionOnly = false);
	UABE_Generic_API const std::string& getName();
	UABE_Generic_API TaskResult execute(TaskProgressManager& progressManager);
	//Can be called directly, without a preceding execute call.
	//asset: Asset to export (not required to be within the assets vector).
	//path: Output file path.
	UABE_Generic_API virtual bool exportAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {}) = 0;
	UABE_Generic_API virtual void onCompletion(const std::string &outputPath, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager);
};

class AssetExportRawTask : public AssetExportTask
{
public:
	UABE_Generic_API AssetExportRawTask(std::vector<AssetUtilDesc> assets, std::string taskName,
		std::string extension, std::string baseDir = "", bool stopOnError = false);
	inline ~AssetExportRawTask() noexcept(true) {}
	UABE_Generic_API bool exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {});
};

class AssetExportDumpTask : public AssetExportTask
{
	class AppContext& appContext;
	virtual void dumpAsset(IAssetsReader* pReader, AssetTypeValueField* pBaseField, IAssetsWriter* pDumpWriter)=0;
public:
	UABE_Generic_API AssetExportDumpTask(class AppContext& appContext,
		std::vector<AssetUtilDesc> assets, std::string taskName,
		std::string extension, std::string baseDir = "", bool stopOnError = false);
	inline ~AssetExportDumpTask() noexcept(true) {}
	UABE_Generic_API bool exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {});
};

class AssetExportTextDumpTask : public AssetExportDumpTask
{
	void recursiveDumpAsset(IAssetsReader* pReader, AssetTypeValueField* pField, size_t depth,
		IAssetsWriter *pDumpWriter, std::string &lineBuf);
	void dumpAsset(IAssetsReader* pReader, AssetTypeValueField* pBaseField, IAssetsWriter* pDumpWriter);
public:
	UABE_Generic_API AssetExportTextDumpTask(class AppContext& appContext,
		std::vector<AssetUtilDesc> assets, std::string taskName,
		std::string extension, std::string baseDir = "", bool stopOnError = false);
};

class AssetExportJSONDumpTask : public AssetExportDumpTask
{
	void recursiveDumpAsset(IAssetsReader* pReader, AssetTypeValueField* pField, size_t depth,
		IAssetsWriter* pDumpWriter, std::string& lineBuf, bool dumpValueOnly = false);
	void dumpAsset(IAssetsReader* pReader, AssetTypeValueField* pBaseField, IAssetsWriter* pDumpWriter);
public:
	UABE_Generic_API AssetExportJSONDumpTask(class AppContext& appContext,
		std::vector<AssetUtilDesc> assets, std::string taskName,
		std::string extension, std::string baseDir = "", bool stopOnError = false);
};

inline std::string MakeImportFileNameRegex(const std::string& extension)
{
	//(?:assetName-)?(assetsFileName)-(pathId)
	//With repeatCount : regexBase = "(?:.*?)-((?:.)*)-((?:\\d)*)(?:-(?:\\d)*)?";
	return std::string("(?:.*?)-([^-]*)-((?:|-)(?:\\d)*)") + extension;
}
inline bool RetrieveImportRegexInfo(IN std::vector<const char*>& capturingGroups, OUT const char*& assetsFileName, OUT pathid_t& pathID)
{
	assetsFileName = nullptr; pathID = 0;
	if (capturingGroups.size() < 2) return false;
	if (capturingGroups[0] == nullptr || capturingGroups[1] == nullptr) return false;
	assetsFileName = capturingGroups[0];

	*_errno() = 0;
	const char* pathIDGroup = capturingGroups[1];
	pathID = static_cast<pathid_t>(_strtoi64(pathIDGroup, NULL, 0));
	if (errno == ERANGE)
	{
		*_errno() = 0;
		pathID = _strtoui64(pathIDGroup, NULL, 0);
	}
	if (errno == ERANGE)
	{
		pathID = 0;
		return false;
	}
	return true;
}

class CGenericBatchImportDialogDesc : public IAssetBatchImportDesc
{
	inline std::string& getElementDescription(size_t i)
	{
		return elements[i].assetName;
	}
	std::vector<AssetUtilDesc> elements;
	std::unordered_multimap<pathid_t, size_t> elementByPathID;
public:
	inline const std::vector<AssetUtilDesc>& getElements() const
	{
		return elements;
	}
	std::string regex;

	std::vector<std::string> importFilePaths;
	std::vector<std::string> importFilePathOverrides;

	inline std::vector<AssetUtilDesc> clearAndGetElements()
	{
		elementByPathID.clear();
		importFilePaths.clear();
		importFilePathOverrides.clear();
		std::vector<AssetUtilDesc> ret;
		elements.swap(ret);
		return ret;
	}
public:
	UABE_Generic_API CGenericBatchImportDialogDesc(std::vector<AssetUtilDesc> _elements, const std::string& extensionRegex);
	UABE_Generic_API bool GetImportableAssetDescs(OUT std::vector<AssetDesc>& nameList);
	UABE_Generic_API bool GetFilenameMatchStrings(OUT std::vector<const char*>& regexList, OUT bool& checkSubDirs);
	UABE_Generic_API bool GetFilenameMatchInfo(IN const char* filename, IN std::vector<const char*>& capturingGroups, OUT size_t& matchIndex);
	UABE_Generic_API void SetInputFilepath(IN size_t matchIndex, IN const char* filepath);
	UABE_Generic_API bool HasFilenameOverride(IN size_t matchIndex, OUT std::string& filenameOverride, OUT bool& relativeToBasePath);
	//Returns full file paths.
	inline std::vector<std::string> getImportFilePaths()
	{
		std::vector<std::string> ret(importFilePaths.size());
		for (size_t i = 0; i < importFilePaths.size(); ++i)
		{
			ret[i] = getImportFilePath(i);
		}
		return ret;
	}
	inline std::string getImportFilePath(size_t i)
	{
		if (importFilePathOverrides.size() > i && !importFilePathOverrides[i].empty())
			return importFilePathOverrides[i];
		else
			return importFilePaths[i];
	}
};

class AssetImportTask : public ITask
{
	std::vector<AssetUtilDesc> assets;
	std::vector<std::string> importFilePaths;
	std::string taskName;
	bool stopOnError;
public:
	//If assets.size() == 1, baseDir will be used as the full path for the asset.
	//Otherwise, the output paths will be generated via AssetUtilDesc::makeExportFilePath(..).
	//extension: File extension, e.g. ".dat", or "" for no extension.
	UABE_Generic_API AssetImportTask(std::vector<AssetUtilDesc> assets, std::vector<std::string> importFilePaths,
		std::string taskName, bool stopOnError = false);
	UABE_Generic_API const std::string& getName();
	UABE_Generic_API TaskResult execute(TaskProgressManager& progressManager);
	//Can be called directly, without a preceding execute call.
	//asset: Asset to export (not required to be within the assets vector).
	//path: Output file path.
	UABE_Generic_API virtual bool importAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {}) = 0;
};

class AssetImportRawTask : public AssetImportTask
{
	AppContext& appContext;
public:
	UABE_Generic_API AssetImportRawTask(AppContext& appContext,
		std::vector<AssetUtilDesc> assets, std::vector<std::string> importFilePaths,
		std::string taskName, bool stopOnError = false);
	UABE_Generic_API bool importAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {});
};

class AssetImportDumpTask : public AssetImportTask
{
	AppContext& appContext;
public:
	UABE_Generic_API AssetImportDumpTask(AppContext& appContext,
		std::vector<AssetUtilDesc> assets, std::vector<std::string> importFilePaths,
		std::string taskName, bool stopOnError = false);
	UABE_Generic_API bool importAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager = {});

	UABE_Generic_API void parseTextDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter);
	UABE_Generic_API void parseJSONDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter);
	UABE_Generic_API void parseDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter);
};

