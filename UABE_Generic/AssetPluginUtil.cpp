#include "AssetPluginUtil.h"
#include "../AssetsTools/AssetTypeClass.h"
#include "AppContext.h"
#include <cmath>
#include <concepts>
#include <locale>
#include <codecvt>
#include <filesystem>
#include <chrono>
#include <format>

std::string AssetUtilDesc::makeExportFilePath(std::unordered_map<std::string, size_t>& nameCountBuffer,
	const std::string &extension, std::string baseDir) const
{
	if (asset.pFile == nullptr || asset.pFile->getFileContext() == nullptr)
		return "";
	if (baseDir.empty() && asset.pFile->getParentFileID() == 0)
		baseDir = asset.pFile->getFileContext()->getFileDirectoryPath();
	if (!baseDir.empty())
		baseDir += (char)std::filesystem::path::preferred_separator;
	static_assert((std::filesystem::path::preferred_separator & ~0x7F) == 0, "Path separator is outside the ASCII space");
	//std::filesystem::path appears to be a mess with std::string carrying UTF-8 chars on Windows.
	//std::filesystem::u8path is deprecated, path(std::string) interprets the string as non-UTF-8.
	//-> Only way would be to copy it to a char8_t container or reinterpret the raw string data pointers to char8_t* and pass it to path(..).
	//Plain std::string concatenation is easier and should be reliable enough, given that baseDir is good.
	std::string& ret = baseDir;
	ret += MakeAssetExportName(asset.pathID,
		assetName, nameCountBuffer,
		assetsFileName);
	ret += extension;
	return ret;
}

std::string MakeAssetExportName(pathid_t pathID, std::string assetName,
	std::unordered_map<std::string, size_t>& nameCountBuffer,
	std::string assetsFileName)
{
	FilterNameForExportInplace(assetName);
	if (assetName.empty()) assetName = "unnamed asset";

	auto previousInstance = nameCountBuffer.end();

	char identifier[32]; identifier[0] = 0;
	if (pathID != 0)
	{
		sprintf_s(identifier, "-%lld", (int64_t)pathID);
	}
	else if (!nameCountBuffer.empty())
	{
		previousInstance = nameCountBuffer.find(assetName);
		if (previousInstance != nameCountBuffer.end())
		{
			sprintf_s(identifier, "-%zu", previousInstance->second + 1);
			previousInstance->second++;
		}
		else
			nameCountBuffer.insert({ assetName, 1 });
	}
	else
		nameCountBuffer.insert({ assetName, 1 });

	std::string ret = std::move(assetName);
	if (!assetsFileName.empty())
	{
		ret += "-";
		FilterNameForExportInplace(assetsFileName);
		ret += assetsFileName;
	}
	ret += identifier;
	return ret;
}

std::shared_ptr<ResourcesFileContextInfo> FindResourcesFile(class AppContext &appContext,
	const std::string& streamDataFileName, AssetIdentifier& asset,
	std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{
	std::shared_ptr<ResourcesFileContextInfo> ret = nullptr;

	std::vector<FileContextInfo_ptr> matchingFiles = appContext.getContextInfo(streamDataFileName, asset.pFile.get());
	size_t numActualCandidates = 0;
	for (FileContextInfo_ptr& curFile : matchingFiles)
	{
		if (curFile->getFileContext() != nullptr && curFile->getFileContext()->getType() == FileContext_Resources)
		{
			numActualCandidates++;
			if (!ret) //Use the first candidate.
				ret = std::dynamic_pointer_cast<ResourcesFileContextInfo>(curFile);
		}
	}
	if (ret == nullptr)
	{
		//This case may be a candidate for on-demand loading with C++20 coroutines, e.g. co_await taskManager.enqueue(appContext.CreateFileOpenTask(...))
		//-> Support for coroutines within AssetExportTask as well as AsyncTask required for this.
		throw AssetUtilError(std::string("Unable to locate the streamed data file. Make sure to open it inside the application. Name: ") + streamDataFileName);
	}
	if (numActualCandidates > 1 && progressManager.has_value())
	{
		//If several matching resource files are found, the plugin uses the 'first one'.
		//However, 'first one' is defined by the order in an unordered_map within AppContext, i.e. is arbitrary.
		progressManager->get().logMessage(std::format(
			"Warning while exporting an asset (File ID {0}, Path ID {1}): {2}",
			asset.fileID, (int64_t)asset.pathID, "Found multiple matching streamed data files. Using an arbitrary one."));
	}
	return ret;
}
AssetTypeTemplateField& TypeTemplateCache::getTemplateField(class AppContext& appContext, class AssetIdentifier &asset,
	std::function<void(AssetTypeTemplateField&)> newTemplateCallback)
{
	if (!asset.pFile)
	{
		asset.resolve(appContext);
		if (!asset.pFile)
			throw AssetUtilError("TypeTemplateCache::getTemplateField: Cannot resolve the asset.");
	}
	unsigned int fileID = asset.pFile->getFileID();
	int32_t classID = asset.getClassID();

	{
		std::shared_lock templateCacheLock(templateCacheMutex);
		auto resultIt = templateCache.find(ClassIdentifier{ fileID, classID });
		if (resultIt != templateCache.end())
			return *resultIt->second;
	}

	auto pTemplateBase = std::make_unique<AssetTypeTemplateField>();
	if (!asset.pFile->MakeTemplateField(pTemplateBase.get(), appContext, asset.getClassID(), asset.getMonoScriptID(), &asset))
		throw AssetUtilError("Unable to extract type information.");
	newTemplateCallback(*pTemplateBase);

	{
		std::scoped_lock templateCacheLock(templateCacheMutex);
		auto insertResult = templateCache.insert({ ClassIdentifier{ fileID, classID }, std::move(pTemplateBase) });
		//insertResult.second: true if the insertion actually took place, false if someone else added it in the meantime.
		//insertResult.first: Iterator to the inserted or existing map entry (which is a .
		assert(insertResult.first->second != nullptr);
		return *insertResult.first->second;
	}
}

AssetExportTask::AssetExportTask(std::vector<AssetUtilDesc> _assets, std::string _taskName,
	std::string _extension, std::string _baseDir,
	bool stopOnError, bool writeOnCompletionOnly)
	: assets(std::move(_assets)), taskName(std::move(_taskName)),
	  extension(std::move(_extension)), baseDir(std::move(_baseDir)),
	  stopOnError(stopOnError), writeOnCompletionOnly(writeOnCompletionOnly)
{}
const std::string& AssetExportTask::getName()
{
	return taskName;
}
TaskResult AssetExportTask::execute(TaskProgressManager& progressManager)
{
	unsigned int progressRange = static_cast<unsigned int>(std::min<size_t>(assets.size(), 10000));
	size_t assetsPerProgressStep = assets.size() / progressRange;
	progressManager.setProgress(0, progressRange);
	progressManager.setCancelable();
	auto lastDescTime = std::chrono::high_resolution_clock::now();
	std::unordered_map<std::string, size_t> nameClashMap;
	bool encounteredErrors = false;
	for (size_t i = 0; i < assets.size(); ++i)
	{
		if (progressManager.isCanceled())
			return TaskResult_Canceled;
		if ((i % assetsPerProgressStep) == 0)
			progressManager.setProgress((unsigned int)(i / assetsPerProgressStep), progressRange);
		auto curTime = std::chrono::high_resolution_clock::now();
		if (i == 0 || std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastDescTime).count() >= 500)
		{
			progressManager.setProgressDesc(std::format("Exporting {}/{}", (i + 1), assets.size()));
			lastDescTime = curTime;
		}
		std::string exportPath = (assets.size() > 1) ? assets[i].makeExportFilePath(nameClashMap, extension, baseDir) : baseDir;
		try {
			bool result = exportAsset(assets[i], exportPath, progressManager);
			if (!result && stopOnError)
				return (TaskResult)-1;
			if (!result)
				encounteredErrors = true;
		}
		catch (AssetUtilError err) {
			progressManager.logMessage(std::format(
				"Error exporting an asset (File ID {0}, Path ID {1}): {2}",
				assets[i].asset.fileID, (int64_t)assets[i].asset.pathID, err.what()));
			if (err.getMayStop() && stopOnError)
				return (TaskResult)-1;
			encounteredErrors = true;
		}
	}
	try {
		onCompletion(baseDir, progressManager);
	}
	catch (AssetUtilError err) {
		progressManager.logMessage(std::format(
			"Error finishing the export task: {0}", err.what()));
		if (err.getMayStop() && stopOnError)
			return (TaskResult)-1;
		encounteredErrors = true;
	}
	return (TaskResult)(encounteredErrors ? -2 : 0);
}
void AssetExportTask::onCompletion(const std::string& outputPath, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{}

AssetExportRawTask::AssetExportRawTask(std::vector<AssetUtilDesc> _assets, std::string _taskName,
	std::string _extension, std::string _baseDir, bool stopOnError)
	: AssetExportTask(std::move(_assets), std::move(_taskName), std::move(_extension), std::move(_baseDir), stopOnError)
{}
bool AssetExportRawTask::exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{
	IAssetsReader_ptr pReader = desc.asset.makeReader();
	if (pReader == nullptr)
		throw AssetUtilError("Unable to read the asset.");
	QWORD size = 0;
	if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0))
		throw AssetUtilError("Unable to read the asset.");
	std::unique_ptr<AssetsEntryReplacer> pReplacer(MakeAssetModifierFromReader(0, 0, -1, (uint16_t)-1, pReader.get(), size));
	if (pReplacer == nullptr)
		throw AssetUtilError("Unexpected runtime error.");
	std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(path.c_str(), true, true, RWOpenFlags_Immediately));
	if (pWriter == nullptr)
		throw AssetUtilError("Unable to create the output file.");

	if (pReplacer->Write(0, pWriter.get()) != size)
		throw AssetUtilError("Unable to write the asset.");
	return true;
}

template <typename floatT>
requires std::same_as<floatT, float> || std::same_as<floatT, double>
static void formatFloatStr(floatT fval, std::string& outStr)
{
	//float and double are always IEEE-754 binary32/64.
	if (fval == std::numeric_limits<floatT>::infinity()
		|| fval == -std::numeric_limits<floatT>::infinity())
	{
		outStr.insert(outStr.size(), (fval < 0) ? "\"-" : "\"+");
		outStr.insert(outStr.size(), "Infinity\"");
	}
	else if (isinf(fval) || isnan(fval))
	{
		assert(!isinf(fval)); //Should be covered by the previous check.
		typename std::conditional<sizeof(floatT) == 4, uint32_t, uint64_t>::type f_as_uint;
		f_as_uint = *reinterpret_cast<decltype(f_as_uint)*>(&fval);
		constexpr std::string_view fmt = (sizeof(floatT) == 4) ? "\"0x{:08X}\"" : "\"0x{:016X}\"";
		std::format_to(std::back_inserter(outStr), fmt, f_as_uint);
	}
	else
	{
		constexpr std::string_view fmt = (sizeof(floatT) == 4) ? "{:.9g}" : "{:.17g}";
		std::format_to(std::back_inserter(outStr), fmt, fval);
	}
}

template <typename floatT>
requires std::same_as<floatT, float> || std::same_as<floatT, double>
static bool parseFloatStr(floatT &outFloat, const std::string& str)
{
	outFloat = 0.0;
	if (str.empty())
		return false;
	if (str[0] == '"')
	{
		if (str.length() == 1 || str.back() != '"')
			return false;
		if (0==str.compare(1, std::string::npos, "+Infinity\"")
			|| 0==str.compare(1, std::string::npos, "Infinity\""))
		{
			outFloat = std::numeric_limits<floatT>::infinity();
			return true;
		}
		if (0==str.compare(1, std::string::npos, "-Infinity\""))
		{
			outFloat = -std::numeric_limits<floatT>::infinity();
			return true;
		}
		if (0==str.compare(1, 2, "0x"))
		{
			typename std::conditional<sizeof(floatT) == 4, uint32_t, uint64_t>::type f_as_uint;
			char* endptr = nullptr;
			f_as_uint = (decltype(f_as_uint))_strtoui64(&str.c_str()[3], &endptr, 16);
			if (endptr != &str.c_str()[str.size() - 1]) //Position of closing '"'
				return false;
			outFloat = *reinterpret_cast<floatT*>(&f_as_uint);
			return true;
		}
		return false;
	}
	try {
		struct _stof_wrap
		{
			inline float operator()(const std::string &str) { return std::stof(str); }
		};
		struct _stod_wrap
		{
			inline double operator()(const std::string& str) { return std::stod(str); }
		};
		typename std::conditional<sizeof(floatT) == 4, _stof_wrap, _stod_wrap>::type converter;
		outFloat = converter(str);
		return true;
	}
	catch (std::invalid_argument) { return false; }
	catch (std::out_of_range) { return false; }
	assert(false);
	return false;
}

AssetExportDumpTask::AssetExportDumpTask(class AppContext& appContext,
	std::vector<AssetUtilDesc> _assets, std::string _taskName,
	std::string _extension, std::string _baseDir, bool stopOnError)
	: AssetExportTask(std::move(_assets), std::move(_taskName), std::move(_extension), std::move(_baseDir), stopOnError),
	appContext(appContext)
{}
bool AssetExportDumpTask::exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{
	if (desc.asset.pFile == nullptr)
		throw AssetUtilError("Unable to find the .assets file.");

	IAssetsReader_ptr pReader = desc.asset.makeReader();
	if (pReader == nullptr)
		throw AssetUtilError("Unable to read the asset.");
	QWORD size = 0;
	if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0))
		throw AssetUtilError("Unable to read the asset.");

	AssetTypeTemplateField templateBase;
	if (!desc.asset.pFile->MakeTemplateField(&templateBase, appContext, desc.asset.getClassID(), desc.asset.getMonoScriptID(), &desc.asset))
		throw AssetUtilError("Unable to extract type information.");
	AssetTypeTemplateField *pTemplateBase = &templateBase;

	AssetTypeInstance assetInstance(1, &pTemplateBase, size, pReader.get(), desc.asset.isBigEndian());
	AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
	if (pBaseField == nullptr || pBaseField->IsDummy())
		throw AssetUtilError("Unable to deserialize the asset.");

	std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(path.c_str(), true, true, RWOpenFlags_Immediately));
	if (pWriter == nullptr)
		throw AssetUtilError("Unable to create the output file.");

	dumpAsset(pReader.get(), pBaseField, pWriter.get());

	return true;
}

struct _Dump_OutputLineBufHandler
{
	std::string& lineBuf;
	IAssetsWriter* pWriter;
	_Dump_OutputLineBufHandler(std::string& lineBuf, IAssetsWriter* pWriter)
		: lineBuf(lineBuf), pWriter(pWriter)
	{}
	void operator()()
	{
		size_t size = lineBuf.size() * sizeof(decltype(lineBuf.data()[0]));
		if (size > 0 && size != pWriter->Write(size, lineBuf.data()))
			throw AssetUtilError("Unable to write the dump.");
	}
};
struct _Dump_OutputRawCharsHandler
{
	IAssetsWriter* pWriter;
	_Dump_OutputRawCharsHandler(IAssetsWriter* pWriter)
		: pWriter(pWriter)
	{}
	void operator()(const char* begin, size_t count)
	{
		size_t size = count * sizeof(decltype(begin[0]));
		if (size > 0 && size != pWriter->Write(size, begin))
			throw AssetUtilError("Unable to write the dump.");
	}
};

AssetExportTextDumpTask::AssetExportTextDumpTask(class AppContext& appContext,
	std::vector<AssetUtilDesc> _assets, std::string _taskName,
	std::string _extension, std::string _baseDir, bool stopOnError)
	: AssetExportDumpTask(appContext, std::move(_assets), std::move(_taskName), std::move(_extension), std::move(_baseDir), stopOnError)
{}
void AssetExportTextDumpTask::dumpAsset(IAssetsReader* pReader, AssetTypeValueField* pBaseField, IAssetsWriter* pDumpWriter)
{
	std::string lineBuf;
	recursiveDumpAsset(pReader, pBaseField, 0, pDumpWriter, lineBuf);
}
void AssetExportTextDumpTask::recursiveDumpAsset(IAssetsReader* pReader, AssetTypeValueField* pField, size_t depth,
	IAssetsWriter* pDumpWriter, std::string &lineBuf)
{
	if (pField == nullptr)
		throw AssetUtilError("Null field encountered.");

	_Dump_OutputLineBufHandler outputLineBuf(lineBuf, pDumpWriter);
	_Dump_OutputRawCharsHandler outputRawChars(pDumpWriter);

	lineBuf.clear();
	for (size_t i = 0; i < depth; ++i)
		lineBuf += " ";
	outputLineBuf();

	EnumValueTypes valueType = ValueType_None;
	if (pField->GetValue() != NULL)
		valueType = pField->GetValue()->GetType();
	int alignment = (int)pField->GetTemplateField()->align;
	
	switch (valueType)
	{
		case ValueType_Bool:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf), 
				"{} {} {} = {}\r\n", alignment, pField->GetType(), pField->GetName(), pField->GetValue()->AsBool() ? "true" : "false");
			outputLineBuf();
			break;
		case ValueType_Int8:
		case ValueType_Int16:
		case ValueType_Int32:
		case ValueType_Int64:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf),
				"{} {} {} = {}\r\n", alignment, pField->GetType(), pField->GetName(), pField->GetValue()->AsInt64());
			outputLineBuf();
			break;
		case ValueType_UInt8:
		case ValueType_UInt16:
		case ValueType_UInt32:
		case ValueType_UInt64:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf),
				"{} {} {} = {}\r\n", alignment, pField->GetType(), pField->GetName(), pField->GetValue()->AsUInt64());
			outputLineBuf();
			break;
		case ValueType_Float:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf),
				"{} {} {} = ", alignment, pField->GetType(), pField->GetName());
			formatFloatStr(pField->GetValue()->AsFloat(), lineBuf);
			lineBuf.insert(lineBuf.size(), "\r\n");
			outputLineBuf();
			break;
		case ValueType_Double:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf),
				"{} {} {} = ", alignment, pField->GetType(), pField->GetName());
			formatFloatStr(pField->GetValue()->AsDouble(), lineBuf);
			lineBuf.insert(lineBuf.size(), "\r\n");
			outputLineBuf();
			break;
		case ValueType_String:
			{
				if (pField->GetTemplateField()->children.size() > 0 && pField->GetTemplateField()->children[0].align)
					alignment = true;
				lineBuf.clear();
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {} = \"", alignment, pField->GetType(), pField->GetName());
				outputLineBuf();
				char *strValue = pField->GetValue()->AsString();
				//Escape the string: '\r' -> "\\r", '\n' -> "\\n" and '\\' -> "\\\\".
				// '\"' is not escaped, since the string end can be inferred from the line end.
				size_t strLen = strlen(strValue); size_t strPos = 0;
				for (size_t i = 0; i < strLen; i++)
				{
					if (strValue[i] == '\\')
					{
						//Output characters: strValue[strPos, i]
						outputRawChars(&strValue[strPos], i - strPos + 1);
						outputRawChars("\\", 1);
						strPos = i+1;
					}
					else if (strValue[i] == '\r' || strValue[i] == '\n')
					{
						//Output characters: strValue[strPos, i)
						outputRawChars(&strValue[strPos], i - strPos);
						outputRawChars((strValue[i] == '\r') ? "\\r" : "\\n", 2);
						strPos = i+1;
					}
				}
				//Output characters: strValue[strPos, strLen)
				outputRawChars(&strValue[strPos], strLen - strPos);
				outputRawChars("\"\r\n", 3);
			}
			break;
		case ValueType_ByteArray:
			{
				if (pField->GetTemplateField()->children.size() != 2)
					throw AssetUtilError("Unexpected byte array serialization.");
				AssetTypeByteArray *pByteArray = pField->GetValue()->AsByteArray();
				lineBuf.clear();
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {} ({} items)\r\n", alignment, pField->GetType(), pField->GetName(), pByteArray->size);
				outputLineBuf();
				lineBuf.clear();
				for (size_t i = 0; i <= depth; ++i)
					lineBuf += " ";
				AssetTypeTemplateField *countVal = &pField->GetTemplateField()->children[0];
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {} = {}\r\n", 0, countVal->type, countVal->name, pByteArray->size);
				outputLineBuf();
				AssetTypeTemplateField *byteVal = &pField->GetTemplateField()->children[1];
				for (uint32_t i = 0; i < pByteArray->size; i++)
				{
					lineBuf.clear();
					for (size_t i = 0; i <= depth; ++i)
						lineBuf += " ";
					std::format_to(std::back_inserter(lineBuf),
						"[{}]\r\n", i);
					outputLineBuf();
					lineBuf.clear();
					for (size_t i = 0; i <= depth+1; ++i)
						lineBuf += " ";
					std::format_to(std::back_inserter(lineBuf),
						"{} {} {} = {}\r\n", 0, byteVal->type, byteVal->name, pByteArray->data[i]);
					outputLineBuf();
				}
			}
			break;
		case ValueType_Array:
			{
				if (pField->GetTemplateField()->children.size() != 2)
					throw AssetUtilError("Unexpected array serialization.");
				lineBuf.clear();
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {} ({} items)\r\n", alignment, pField->GetType(), pField->GetName(), pField->GetChildrenCount());
				outputLineBuf();
				lineBuf.clear();
				for (size_t i = 0; i <= depth; ++i)
					lineBuf += " ";
				AssetTypeTemplateField *countVal = &pField->GetTemplateField()->children[0];
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {} = {}\r\n", 0, countVal->type, countVal->name, pField->GetChildrenCount());
				outputLineBuf();
				for (uint32_t i = 0; i < pField->GetChildrenCount(); i++)
				{
					lineBuf.clear();
					for (size_t i = 0; i <= depth; ++i)
						lineBuf += " ";
					std::format_to(std::back_inserter(lineBuf),
						"[{}]\r\n", i);
					outputLineBuf();
					recursiveDumpAsset(pReader, pField->operator[](i), depth + 2, pDumpWriter, lineBuf);
				}
			}
			break;
		case ValueType_None:
			{
				lineBuf.clear();
				std::format_to(std::back_inserter(lineBuf),
					"{} {} {}\r\n", alignment, pField->GetType(), pField->GetName());
				outputLineBuf();
				for (uint32_t i = 0; i < pField->GetChildrenCount(); i++)
				{
					recursiveDumpAsset(pReader, pField->operator[](i), depth + 1, pDumpWriter, lineBuf);
				}
			}
			break;
	}
}

AssetExportJSONDumpTask::AssetExportJSONDumpTask(class AppContext& appContext,
	std::vector<AssetUtilDesc> _assets, std::string _taskName,
	std::string _extension, std::string _baseDir, bool stopOnError)
	: AssetExportDumpTask(appContext, std::move(_assets), std::move(_taskName), std::move(_extension), std::move(_baseDir), stopOnError)
{}
void AssetExportJSONDumpTask::dumpAsset(IAssetsReader* pReader, AssetTypeValueField* pBaseField, IAssetsWriter* pDumpWriter)
{
	std::string lineBuf;
	_Dump_OutputLineBufHandler outputLineBuf(lineBuf, pDumpWriter);
	lineBuf.assign("{\r\n    ");
	outputLineBuf();
	
	recursiveDumpAsset(pReader, pBaseField, 1, pDumpWriter, lineBuf);

	lineBuf.assign("\r\n}");
	outputLineBuf();
}


void AssetExportJSONDumpTask::recursiveDumpAsset(IAssetsReader* pReader, AssetTypeValueField* pField, size_t depth,
	IAssetsWriter* pDumpWriter, std::string& lineBuf, bool dumpValueOnly)
{
	constexpr bool stripExtendedInfo = false;
	if (pField == nullptr)
		throw AssetUtilError("Null field encountered.");

	_Dump_OutputLineBufHandler outputLineBuf(lineBuf, pDumpWriter);
	_Dump_OutputRawCharsHandler outputRawChars(pDumpWriter);

	EnumValueTypes valueType = ValueType_None;
	if (pField->GetValue() != NULL)
		valueType = pField->GetValue()->GetType();

	if (!dumpValueOnly)
	{
		bool alignment = pField->GetTemplateField()->align ||
			(valueType == ValueType_String && (pField->GetTemplateField()->children.size() > 0 && pField->GetTemplateField()->children[0].align));
		std::string fieldName;
		if (stripExtendedInfo)
			fieldName.assign(pField->GetName());
		else
			fieldName = std::format("{} {} {}", alignment ? "1" : "0", pField->GetType(), pField->GetName());
		lineBuf.clear();
		std::format_to(std::back_inserter(lineBuf),
			"\"{}\": ", fieldName);
		outputLineBuf();
	}

	switch (valueType)
	{
		case ValueType_Bool:
			if (pField->GetValue()->AsBool())
				lineBuf.assign("true");
			else
				lineBuf.assign("false");
			outputLineBuf();
			break;
		case ValueType_Int8:
		case ValueType_Int16:
		case ValueType_Int32:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf), "{}", pField->GetValue()->AsInt());
			outputLineBuf();
			break;
		case ValueType_UInt8:
		case ValueType_UInt16:
		case ValueType_UInt32:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf), "{}", pField->GetValue()->AsUInt());
			outputLineBuf();
			break;
		case ValueType_Int64:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf), "\"{}\"", pField->GetValue()->AsInt64());
			outputLineBuf();
			break;
		case ValueType_UInt64:
			lineBuf.clear();
			std::format_to(std::back_inserter(lineBuf), "\"{}\"", pField->GetValue()->AsUInt64());
			outputLineBuf();
			break;
		case ValueType_Float:
			lineBuf.clear();
			formatFloatStr(pField->GetValue()->AsFloat(), lineBuf);
			outputLineBuf();
			break;
		case ValueType_Double:
			lineBuf.clear();
			formatFloatStr(pField->GetValue()->AsDouble(), lineBuf);
			outputLineBuf();
			break;
		case ValueType_String:
			{
				outputRawChars("\"", 1);
				char *strValue = pField->GetValue()->AsString();
				size_t strLen = strlen(strValue); size_t strPos = 0;
				for (size_t i = 0; i < strLen; i++)
				{
					if (strValue[i] & 0x80) //UTF-8 character has another byte ((char & 0xC0) == 0x80 for the 2nd/3rd/4th byte)
					{
						//Output strValue[strPos,i]
						outputRawChars(&strValue[strPos], i - strPos + 1);
						strPos = i+1;
					}
					else
					{
						lineBuf.clear();
						const char *replaceString = NULL;
						switch (strValue[i])
						{
						case '\\':
							lineBuf.assign("\\\\");
							break;
						case '"':
							lineBuf.assign("\\\"");
							break;
						case '\b':
							lineBuf.assign("\\b");
							break;
						case '\f':
							lineBuf.assign("\\f");
							break;
						case '\n':
							lineBuf.assign("\\n");
							break;
						case '\r':
							lineBuf.assign("\\r");
							break;
						case '\t':
							lineBuf.assign("\\t");
							break;
						default:
							if (strValue[i] < 0x20)
								std::format_to(std::back_inserter(lineBuf), "\\u{:04u}", strValue[i]);
							break;
						}
						if (!lineBuf.empty())
						{
							//Output strValue[strPos,i)
							outputRawChars(&strValue[strPos], i - strPos);
							strPos = i;
							outputLineBuf();
							strPos++;
						}
					}
				}
				//Output strValue[strPos,strLen)
				outputRawChars(&strValue[strPos], strLen - strPos);
				outputRawChars("\"", 1);
			}
			break;
		case ValueType_ByteArray:
			{
				AssetTypeByteArray *pByteArray = pField->GetValue()->AsByteArray();
				outputRawChars("[", 1);
				for (uint32_t i = 0; i < pByteArray->size; i++)
				{
					if (i)
						pDumpWriter->Write(2, ", ");
					lineBuf.clear();
					std::format_to(std::back_inserter(lineBuf), "{}", pByteArray->data[i]);
					outputLineBuf();
				}
				outputRawChars("]", 1);
			}
			break;
		case ValueType_Array:
			{
				if (pField->GetChildrenCount() > 0)
					outputRawChars("[\r\n", 3);
				else
					outputRawChars("[", 1);
				//bool writeChildAsObject = !field->GetTemplateField()->children[1].hasValue;
				for (uint32_t i = 0; i < pField->GetChildrenCount(); i++)
				{
					lineBuf.clear();
					if (i != 0)
						lineBuf += ",\r\n";
					for (size_t _i = 0; _i <= depth; ++_i)
						lineBuf += "    ";
					if (!stripExtendedInfo)
						lineBuf += "{";
					outputLineBuf();
					recursiveDumpAsset(pReader, pField->operator[](i), depth + 1, pDumpWriter, lineBuf, stripExtendedInfo);
					if (!stripExtendedInfo)
						outputRawChars("}", 1);
				}
				if (pField->GetChildrenCount() > 0)
				{
					lineBuf.clear();
					lineBuf += "\r\n";
					for (size_t _i = 0; _i < depth; _i++)
						lineBuf += "    ";
					outputLineBuf();
				}
				outputRawChars("]", 1);
			}
			break;
		case ValueType_None:
			{
				bool drawBrackets = false;
				if (dumpValueOnly)
				{
					if (pField->GetChildrenCount() > 1)
					{
						outputRawChars("{\r\n", 3);
						drawBrackets = true;
					}
				}
				else if (pField->GetChildrenCount() > 1)
				{
					lineBuf.clear();
					lineBuf += "\r\n";
					for (size_t _i = 0; _i < depth; _i++)
						lineBuf += "    ";
					lineBuf += "{\r\n";
					outputLineBuf();
					drawBrackets = true;
				}
				else
				{
					outputRawChars(" {\r\n", 4);
					drawBrackets = true;
				}
				for (uint32_t i = 0; i < pField->GetChildrenCount(); i++)
				{
					lineBuf.clear();
					if (i != 0)
						lineBuf += ",\r\n";
					for (size_t _i = 0; _i <= depth; _i++)
						lineBuf += "    ";
					outputLineBuf();
					recursiveDumpAsset(pReader, pField->operator[](i), depth + 1, pDumpWriter, lineBuf);
				}
				lineBuf.clear();
				if (pField->GetChildrenCount() > 0)
					lineBuf += "\r\n";
				if (drawBrackets)
				{
					for (size_t _i = 0; _i < depth; _i++)
						lineBuf += "    ";
					lineBuf += "}";
				}
				outputLineBuf();
			}
			break;
	}
}




CGenericBatchImportDialogDesc::CGenericBatchImportDialogDesc(std::vector<AssetUtilDesc> _elements, const std::string& extensionRegex)
	: elements(std::move(_elements))
{
	for (size_t i = 0; i < elements.size(); ++i)
	{
		if (elements[i].asset.pathID != 0)
			elementByPathID.insert({ elements[i].asset.pathID, i });
	}
	importFilePaths.resize(elements.size());
	importFilePathOverrides.resize(elements.size());
	regex = MakeImportFileNameRegex(extensionRegex);
}
bool CGenericBatchImportDialogDesc::GetImportableAssetDescs(OUT std::vector<AssetDesc>& nameList)
{
	nameList.reserve(elements.size());
	for (size_t i = 0; i < elements.size(); i++)
	{
		AssetDesc curDesc;
		std::string& desc = getElementDescription(i);
		curDesc.description = desc.c_str();
		curDesc.assetsFileName = elements[i].assetsFileName.c_str();
		curDesc.pathID = static_cast<pathid_t>(elements[i].asset.pathID);
		nameList.push_back(curDesc);
	}
	return true;
}
bool CGenericBatchImportDialogDesc::GetFilenameMatchStrings(OUT std::vector<const char*>& regexList, OUT bool& checkSubDirs)
{
	regexList.push_back(regex.c_str());
	checkSubDirs = false;
	return true;
}
bool CGenericBatchImportDialogDesc::GetFilenameMatchInfo(IN const char* filename, IN std::vector<const char*>& capturingGroups, OUT size_t& matchIndex)
{
	const char* assetsFileName = nullptr; pathid_t pathID = 0;
	matchIndex = (size_t)-1;
	if (RetrieveImportRegexInfo(capturingGroups, assetsFileName, pathID))
	{
		auto it_pair = elementByPathID.equal_range(pathID);
		for (auto it = it_pair.first; it != it_pair.second; ++it)
		{
			size_t i = it->second;
			assert(pathID == elements[i].asset.pathID);
			std::string elementFileNameFiltered = elements[i].assetsFileName;
			FilterNameForExportInplace(elementFileNameFiltered);
			if (elementFileNameFiltered == assetsFileName)
			{
				matchIndex = i;
				return true;
			}
		}
	}
	return false;
}
void CGenericBatchImportDialogDesc::SetInputFilepath(IN size_t matchIndex, IN const char* filepath)
{
	if (matchIndex < importFilePaths.size())
	{
		importFilePaths[matchIndex].assign(filepath ? filepath : "");
	}
}
bool CGenericBatchImportDialogDesc::HasFilenameOverride(IN size_t matchIndex, OUT std::string& filenameOverride, OUT bool& relativeToBasePath)
{
	relativeToBasePath = false;
	if (matchIndex >= importFilePathOverrides.size())
		return false;
	if (importFilePathOverrides[matchIndex].size() > 0)
	{
		filenameOverride.assign(importFilePathOverrides[matchIndex]);
		return true;
	}
	filenameOverride.clear();
	return false;
}

AssetImportTask::AssetImportTask(std::vector<AssetUtilDesc> _assets, std::vector<std::string> _importFilePaths,
		std::string _taskName, bool stopOnError)
	: assets(std::move(_assets)), importFilePaths(std::move(_importFilePaths)),
	taskName(std::move(_taskName)), stopOnError(stopOnError)
{
	if (assets.size() != importFilePaths.size())
		throw std::invalid_argument("AssetImportTask: assets and importFilePaths should have matching numbers of elements!");
}
const std::string& AssetImportTask::getName()
{
	return taskName;
}
TaskResult AssetImportTask::execute(TaskProgressManager& progressManager)
{
	unsigned int progressRange = static_cast<unsigned int>(std::min<size_t>(assets.size(), 10000));
	size_t assetsPerProgressStep = assets.size() / progressRange;
	constexpr size_t assetsPerDescUpdate = 8;
	progressManager.setCancelable();
	progressManager.setProgress(0, progressRange);
	auto lastDescTime = std::chrono::high_resolution_clock::time_point::min();
	bool encounteredErrors = false;
	for (size_t i = 0; i < assets.size(); ++i)
	{
		if (progressManager.isCanceled())
			return TaskResult_Canceled;
		if ((i % assetsPerProgressStep) == 0)
			progressManager.setProgress((unsigned int)(i / assetsPerProgressStep), progressRange);
		auto curTime = std::chrono::high_resolution_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastDescTime).count() >= 500)
		{
			progressManager.setProgressDesc(std::format("Importing {}/{}", (i + 1), assets.size()));
			lastDescTime = curTime;
		}
		try {
			bool result = (importFilePaths[i].empty() || importAsset(assets[i], importFilePaths[i], progressManager));
			if (!result && stopOnError)
				return (TaskResult)-1;
			if (!result)
				encounteredErrors = true;
		}
		catch (AssetUtilError err) {
			progressManager.logMessage(std::format(
				"Error importing an asset (File ID {0}, Path ID {1}): {2}",
				assets[i].asset.fileID, (int64_t)assets[i].asset.pathID, err.what()));
			if (err.getMayStop() && stopOnError)
				return (TaskResult)-1;
			encounteredErrors = true;
		}
	}
	return (TaskResult)(encounteredErrors ? -2 : 0);
}

static void Free_AssetsWriterToMemory_Delete(void* buffer)
{
	if (buffer)
		delete[] (uint8_t*)buffer;
}

AssetImportRawTask::AssetImportRawTask(AppContext& appContext,
	std::vector<AssetUtilDesc> _assets, std::vector<std::string> _importFilePaths,
	std::string _taskName, bool stopOnError)
	: AssetImportTask(std::move(_assets), std::move(_importFilePaths), std::move(_taskName), stopOnError),
	  appContext(appContext)
{}

bool AssetImportRawTask::importAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{
	if (asset.asset.pFile == nullptr)
		throw AssetUtilError("Unable to find the target .assets file.");
	std::unique_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(path.c_str(), true, RWOpenFlags_Immediately));// desc.asset.makeReader();
	if (pReader == nullptr)
		throw AssetUtilError("Unable to read the file.");
	QWORD size = 0;
	if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0) || size >= SIZE_MAX)
		throw AssetUtilError("Unable to read the file.");
	std::unique_ptr<uint8_t[]> memBuf(new uint8_t[(size_t)size]);
	if (pReader->Read(size, memBuf.get()) != size)
		throw AssetUtilError("Unable to read the file.");
	std::shared_ptr<AssetsEntryReplacer> pReplacer(MakeAssetModifierFromMemory(0, asset.asset.pathID,
		asset.asset.getClassID(), asset.asset.getMonoScriptID(),
		memBuf.release(), (size_t)size, Free_AssetsWriterToMemory_Delete));
	if (pReplacer == nullptr)
		throw AssetUtilError("Unexpected runtime error.");
	asset.asset.pFile->addReplacer(pReplacer, appContext);
	return true;
}

AssetImportDumpTask::AssetImportDumpTask(AppContext& appContext,
	std::vector<AssetUtilDesc> _assets, std::vector<std::string> _importFilePaths,
	std::string _taskName, bool stopOnError)
	: AssetImportTask(std::move(_assets), std::move(_importFilePaths), std::move(_taskName), stopOnError),
	appContext(appContext)
{}

bool AssetImportDumpTask::importAsset(AssetUtilDesc& asset, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
{
	if (asset.asset.pFile == nullptr)
		throw AssetUtilError("Unable to find the target .assets file.");
	std::unique_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(path.c_str(), true, RWOpenFlags_Immediately));// desc.asset.makeReader();
	if (pReader == nullptr)
		throw AssetUtilError("Unable to read the file.");
	QWORD size = 0;
	if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0))
		throw AssetUtilError("Unable to read the file.");
	std::unique_ptr<IAssetsWriterToMemory> pWriter(Create_AssetsWriterToMemory());
	parseDump(pReader.get(), pWriter.get());

	size_t bufferLen = 0; void* buffer = NULL;
	pWriter->GetBuffer(buffer, bufferLen);
	pWriter->SetFreeBuffer(false);

	std::shared_ptr<AssetsEntryReplacer> pReplacer(MakeAssetModifierFromMemory(0, asset.asset.pathID,
		asset.asset.getClassID(), asset.asset.getMonoScriptID(),
		buffer, bufferLen, Free_AssetsWriterToMemory_DynBuf));
	if (pReplacer == nullptr)
		throw AssetUtilError("Unexpected runtime error.");

	asset.asset.pFile->addReplacer(pReplacer, appContext);
	return true;
}
void AssetImportDumpTask::parseDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter)
{
	pDumpReader->Seek(AssetsSeek_Begin, 0);
	unsigned char curChar = 0;
	while (pDumpReader->Read(1, &curChar) && ((curChar <= 0x20) || (curChar & 0x80))) {}
	pDumpReader->Seek(AssetsSeek_Begin, 0);
	if (curChar == '{')
		parseJSONDump(pDumpReader, pWriter);
	else
		parseTextDump(pDumpReader, pWriter);
}

//Parses an integer type (valueType: ValueType_Int*, ValueType_UInt*) in base-10 string representation (valueStr)
// and writes it out.
//valueStr need not end after the number representation.
//
//Returns false iff the represented value is out of range for the type (otherwise true).
static bool parsePrimitiveInt(EnumValueTypes valueType, const char *valueStr, QWORD &filePos, IAssetsWriter* pWriter)
{
	switch (valueType)
	{
		case ValueType_Int8:
		{
			int32_t int8Tmp = strtol((char*)valueStr, NULL, 10);
			if (int8Tmp < INT8_MIN || int8Tmp > INT8_MAX)
				return false;
			pWriter->Write(filePos, 1, &int8Tmp); filePos += 1;
		}
		break;
		case ValueType_Int16:
		{
			int32_t int16Tmp = strtol((char*)valueStr, NULL, 10);
			if (int16Tmp < INT16_MIN || int16Tmp > INT16_MAX)
				return false;
			pWriter->Write(filePos, 2, &int16Tmp); filePos += 2;
		}
		break;
		case ValueType_Int32:
		{
			errno = 0;
			static_assert(sizeof(long) == sizeof(int32_t), "Unsupported long size.");
			int32_t int32Tmp = strtol((char*)valueStr, NULL, 10);
			if ((int32Tmp == LONG_MIN || int32Tmp == LONG_MAX) && errno == ERANGE)
				return false;
			pWriter->Write(filePos, 4, &int32Tmp); filePos += 4;
		}
		break;
		case ValueType_Int64:
		{
			static_assert(sizeof(long long) == sizeof(int64_t), "Unsupported long long size.");
			errno = 0;
			int64_t int64Tmp = strtoll((char*)valueStr, NULL, 10);
			if ((int64Tmp == LLONG_MIN || int64Tmp == LLONG_MAX) && errno == ERANGE)
				return false;
			pWriter->Write(filePos, 8, &int64Tmp); filePos += 8;
		}
		break;
		case ValueType_UInt8:
		{
			uint32_t uint8Tmp = strtoul((char*)valueStr, NULL, 10);
			if (uint8Tmp > UINT8_MAX)
				return false;
			pWriter->Write(filePos, 1, &uint8Tmp); filePos += 1;
		}
		break;
		case ValueType_UInt16:
		{
			uint32_t uint16Tmp = strtoul((char*)valueStr, NULL, 10);
			if (uint16Tmp > UINT16_MAX)
				return false;
			pWriter->Write(filePos, 2, &uint16Tmp); filePos += 2;
		}
		break;
		case ValueType_UInt32:
		{
			static_assert(sizeof(unsigned long) == sizeof(uint32_t), "Unsupported unsigned long size.");
			errno = 0;
			uint32_t uint32Tmp = strtoul((char*)valueStr, NULL, 10);
			if (uint32Tmp == ULONG_MAX && errno == ERANGE)
				return false;
			pWriter->Write(filePos, 4, &uint32Tmp); filePos += 4;
		}
		break;
		case ValueType_UInt64:
		{
			static_assert(sizeof(unsigned long long) == sizeof(uint64_t), "Unsupported unsigned long long size.");
			errno = 0;
			uint64_t uint64Tmp = strtoull((char*)valueStr, NULL, 10);
			if (uint64Tmp == ULLONG_MAX && errno == ERANGE)
				return false;
			pWriter->Write(filePos, 8, &uint64Tmp); filePos += 8;
		}
		break;
	}
	return true;
}

void AssetImportDumpTask::parseTextDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter)
{
	struct _TypeInfo
	{
		bool align;
		short depth;
	};
	std::vector<_TypeInfo> previousTypes;
	std::vector<char8_t> lineBuffer;
	auto readLine = [&lineBuffer, pDumpReader]()
	{
		size_t lineLen = -1; size_t lineBufferPos = 0; size_t readBytes = 0;
		do {
			lineBuffer.resize(lineBufferPos + 256);
			readBytes = pDumpReader->Read(256, &lineBuffer[lineBufferPos]);
			if (readBytes == 0)
				break;
			lineBuffer.resize(lineBufferPos + readBytes);
			for (size_t i = 0; i < readBytes; i++)
			{
				if (lineBuffer[lineBufferPos + i] == '\n')
				{
					lineLen = lineBufferPos + i;
					//fseek(pDumpFile, (long)(i+1+((nextCh=='\n')?1:0))-((long)readBytes), SEEK_CUR);
					pDumpReader->Seek(AssetsSeek_Cur, (int64_t)((i + 1) - readBytes));
					lineBuffer.resize(lineBufferPos + i);
					readBytes = i;
					break;
				}
			}
			lineBufferPos += readBytes;
		} while (lineLen == -1);
		if (!lineBuffer.empty() && lineBuffer.back() == '\r')
			lineBuffer.pop_back();
		lineBuffer.push_back(0);
		return (lineLen != -1);
	};
	QWORD filePos = 0;
	//Checks the depths of the previous lines.
	//If the new line exits at least one depth level,
	// the alignment of these depth levels is looked up and applied.
	auto checkPrevDepthsAndAlign = [&previousTypes, &filePos, pWriter](short newDepth) {
		short previousDepth;
		if (previousTypes.size() > 0 && ((previousDepth = previousTypes.back().depth) > newDepth))
		{
			//The new line exits at least one depth level compared to its predecessor.
			//As the predecessor must be a value type (since it apparently does not have any children),
			// we need to further check its predecessors to find out if alignment needs to be applied.
			// (Value types have their alignment applied immediately).
			for (size_t _i = previousTypes.size() - 2 + 1; _i > 0; _i--)
			{
				size_t i = _i - 1;
				if (previousTypes[i].depth < newDepth)
					break; //This line precedes opening the depth level for the new line.
				if (previousTypes[i].depth < previousDepth)
				{
					//The depth level of this line's children is closed.
					if (previousTypes[i].align)
					{
						uint32_t dwNull = 0; int paddingLen = 3 - ((filePos - 1) & 3);
						pWriter->Write(filePos, paddingLen, &dwNull); filePos += paddingLen;
						//No need to check further, as we are aligned to the 4 byte boundary.
						break;
					}
					else
						previousDepth = previousTypes[i].depth;
					//The new line either closes this line's depth level, too,
					// or is a direct neighbour to this line.
				}
				if (previousTypes[i].depth == newDepth)
					break; //Found a preceding neighbour, so the lookup can stop now.
			}
		}
	};
	size_t numLine = 0;
	auto getLineMessage = [&numLine]() {
		return std::string("Line ") + std::to_string(numLine) + ": ";
	};
	while (true)
	{
		lineBuffer.clear();
		if (!readLine())
			break;
		++numLine;
		assert(lineBuffer.size() > 0 && lineBuffer.back() == 0);
		size_t lineLen = lineBuffer.size() - 1;
		_TypeInfo ti; ti.depth = -1;
		for (size_t i = 0; i < lineBuffer.size() - 1; i++)
		{
			if (lineBuffer[i] != ' ')
			{
				ti.depth = (i > SHRT_MAX) ? -1 : (short)i;
				break;
			}
		}
		if (ti.depth == -1)
			continue;
		if (lineBuffer[ti.depth] != '0' && lineBuffer[ti.depth] != '1') //no alignment info (either invalid file or array index item)
			continue; //as it neither has a value nor specifies alignment, we can ignore this
		if (ti.depth + 2 >= lineBuffer.size())
			continue;
		ti.align = lineBuffer[ti.depth] == '1';
		char8_t* fieldType = &lineBuffer[ti.depth + 2];
		char8_t* fieldName = nullptr; size_t fieldNamePos = 0;
		size_t _tmpTypeBegin = ti.depth + 2;
		//"unsigned int" is the only built-in type with a space in its name.
		if (!_strnicmp((char*)fieldType, "unsigned ", 9))
		{
			_tmpTypeBegin = ti.depth + 2 + 9;
		}
		for (size_t lineBufferPos = _tmpTypeBegin; lineBufferPos < lineLen; lineBufferPos++)
		{
			if (lineBuffer[lineBufferPos] == ' ')
			{
				fieldName = &lineBuffer[lineBufferPos + 1];
				fieldNamePos = lineBufferPos + 1;
				break;
			}
		}

		checkPrevDepthsAndAlign(ti.depth);
		previousTypes.push_back(ti);

		if (fieldName == nullptr)
			continue;

		size_t valueBeginPos = 0;
		for (size_t lineBufferPos = fieldNamePos; lineBufferPos < lineLen; lineBufferPos++)
		{
			if (lineBuffer[lineBufferPos] == '=')
			{
				bool hasAdditionalSpace = false;
				if ((lineBufferPos + 1) < lineLen)
				{
					if (lineBuffer[lineBufferPos + 1] == ' ')
						hasAdditionalSpace = true;
				}
				else
					break;
				valueBeginPos = lineBufferPos + (hasAdditionalSpace ? 2 : 1);
				break;
			}
		}
		if (valueBeginPos != 0)
		{
			size_t valueEndPos = valueBeginPos;
			for (size_t _i = lineLen; _i > valueBeginPos; --_i)
			{
				size_t i = _i - 1;
				if (lineBuffer[i] > 0x20)
				{
					valueEndPos = i + 1;
					break;
				}
			}
			lineBuffer[fieldNamePos - 1] = 0; //so GetValueTypeByTypeName can find the type
			char8_t* value = &lineBuffer[valueBeginPos];
			EnumValueTypes valueType = GetValueTypeByTypeName((char*)fieldType);
			switch (valueType)
			{
			case ValueType_Bool:
			{
				uint8_t boolTmp = !_strnicmp((char*)value, "true", 4) ? 1 : 0;
				pWriter->Write(filePos, 1, &boolTmp); filePos += 1;
			}
			break;
			case ValueType_Int8:
			case ValueType_Int16:
			case ValueType_Int32:
			case ValueType_Int64:
			case ValueType_UInt8:
			case ValueType_UInt16:
			case ValueType_UInt32:
			case ValueType_UInt64:
			{
				if (!parsePrimitiveInt(valueType, (char*)value, filePos, pWriter))
					throw AssetUtilError(getLineMessage() + "Primitive value out of range.");
			}
			break;
			case ValueType_Float:
			{
				float floatTmp;
				if (!parseFloatStr<float>(floatTmp, std::string(&lineBuffer[valueBeginPos], &lineBuffer[valueEndPos])))
					throw AssetUtilError(std::string("Line ") + std::to_string(numLine) + ": Unable to parse value.");
				pWriter->Write(filePos, 4, &floatTmp); filePos += 4;
			}
			break;
			case ValueType_Double:
			{
				double doubleTmp;
				if (!parseFloatStr<double>(doubleTmp, std::string(&lineBuffer[valueBeginPos], &lineBuffer[valueEndPos])))
					throw AssetUtilError(std::string("Line ") + std::to_string(numLine) + ": Unable to parse value.");
				pWriter->Write(filePos, 8, &doubleTmp); filePos += 8;
			}
			break;
			case ValueType_String:
			{
				if (((valueBeginPos + 1) > (size_t)lineLen) || (lineBuffer[valueBeginPos] != '"'))
					break;
				value = &value[1];
				for (size_t lineBufferPos = (size_t)lineLen - 1; lineBufferPos > valueBeginPos; lineBufferPos--)
				{
					if (lineBuffer[lineBufferPos] == '"')
					{
						lineBuffer[lineBufferPos] = 0;
						break;
					}
				}
				size_t valueLen = strlen((char*)value);
				std::vector<char8_t> valueBuffer(valueLen + 1);
				size_t valueBufferIndex = 0;
				for (size_t i = 0; i < valueLen; i++)
				{
					if (value[i] == '\\' && ((i + 1) < (valueLen)))
					{
						switch (value[i + 1])
						{
						case '\\':
							valueBuffer[valueBufferIndex++] = '\\';
							i++;
							break;
						case 'r':
							valueBuffer[valueBufferIndex++] = '\r';
							i++;
							break;
						case 'n':
							valueBuffer[valueBufferIndex++] = '\n';
							i++;
							break;
						default:
							valueBuffer[valueBufferIndex++] = value[i];
							break;
						}
					}
					else
					{
						valueBuffer[valueBufferIndex++] = value[i];
					}
				}
				pWriter->Write(filePos, 4, &valueBufferIndex); filePos += 4;
				pWriter->Write(filePos, valueBufferIndex, valueBuffer.data()); filePos += valueBufferIndex;
			}
			break;
			default:
				valueType = ValueType_None;
				break;
			}
			if (valueType != ValueType_None)
			{
				if (ti.align)
				{
					uint32_t dwNull = 0; int paddingLen = 3 - ((filePos - 1) & 3);
					pWriter->Write(filePos, paddingLen, &dwNull); filePos += paddingLen;
				}
			}
		}
	}
	checkPrevDepthsAndAlign(0);
}

#define JSMN_PARENT_LINKS
#define JSMN_STATIC
#include <jsmn.h>
void AssetImportDumpTask::parseJSONDump(IAssetsReader* pDumpReader, IAssetsWriter* pWriter)
{
	QWORD filePos = 0;
	jsmn_parser parser;
	jsmn_init(&parser);
	pDumpReader->Seek(AssetsSeek_End, 0);
	QWORD jsonLen = 0;
	pDumpReader->Tell(jsonLen);
	pDumpReader->Seek(AssetsSeek_Begin, 0);
	//fseek(pDumpFile, 0, SEEK_END);
	//size_t jsonLen = (size_t)ftell(pDumpFile);
	//fseek(pDumpFile, 0, SEEK_SET);
	std::vector<char> jsonBuffer(jsonLen + 1);
	jsonLen = pDumpReader->Read(jsonLen, jsonBuffer.data());
	//jsonLen = fread(jsonBuffer, 1, jsonLen, pDumpFile);
	if (jsonLen == 0)
	{
		throw AssetUtilError("Unable to read the file.");
		return;
	}
	jsonBuffer[jsonLen] = 0;
	std::vector<jsmntok_t> tokens(16); size_t actualTokenCount = 0;
	while (true)
	{
		int err = jsmn_parse(&parser, jsonBuffer.data(), (size_t)jsonLen, &tokens[0], (unsigned int)tokens.size());
		size_t startIndex = actualTokenCount;
		if (err < 0)
		{
			switch (err)
			{
			default:
			case JSMN_ERROR_INVAL:
			case JSMN_ERROR_PART:
				throw AssetUtilError("The JSON file is invalid or cut off or bigger than 2 GiB.");
			case JSMN_ERROR_NOMEM:
				actualTokenCount = (size_t)parser.toknext;
				if (tokens.size() > INT_MAX)
					throw AssetUtilError("The JSON token count is out of range.");
				tokens.resize(std::min<size_t>(INT_MAX, tokens.size() * 2));
				break;
			}
		}
		else
		{
			actualTokenCount = (size_t)err;
			break;
		}
	}
	if (actualTokenCount > INT_MAX)
		throw AssetUtilError("The JSON token count is out of range.");
	bool doubleConvertErr = false;
	for (size_t i = 0; i < actualTokenCount; i++)
	{
		jsmntok_t& token = tokens[i];
		//Safety checks; Probably not required because jsmn shouldn't allow such cases.
		if ((token.start < 0) || (token.start > token.end) || ((unsigned long)token.end > jsonLen) || (token.parent != -1 && token.parent >= i))
			throw AssetUtilError("Invalid JSON file.");
		switch (token.type)
		{
		case JSMN_ARRAY:
		{
			if (token.parent == -1 || tokens[token.parent].type != JSMN_STRING) //Has no name
				throw AssetUtilError("UABE json dump not as expected; A JSON array is missing a name.");
			unsigned int count = (unsigned int)token.size;
			pWriter->Write(filePos, 4, &count); filePos += 4;
		}
		break;
		case JSMN_STRING:
		{
			if (jsonBuffer[token.end] != '"')
				throw AssetUtilError("Unable to parse a JSON string object.");
			if (/*(token.parent == -1 || tokens[token.parent].type != 5)
				&& */((int)actualTokenCount > (i + 1)) && tokens[i + 1].parent == i) //Is a token name.
			{
				if ((token.end - token.start) < 3
					|| (jsonBuffer[token.start] != '0' && jsonBuffer[token.start] != '1')
					|| jsonBuffer[token.start + 1] != ' ')
				{
					throw AssetUtilError("UABE json dump not as expected; Field name format is unexpected.");
				}
				jsonBuffer[token.end] = 0;
				//token.type = (jsmntype_t)5; //Custom type for names.
				break;
			}
			//Is a string value.
			bool parsed = false;
			//Handle non-JSON primitives (depending on the type determined by the JSON field name).
			if (token.parent != -1 && tokens[token.parent].type == JSMN_STRING)
			{
				jsmntok_t& nameToken = tokens[token.parent];
				int size = nameToken.end - nameToken.start;
				int start = nameToken.start + 2;
				if (size > 11 && !strncmp(&jsonBuffer[start], "unsigned ", 9))
					start += 9;
				for (int i = start; i < nameToken.end; i++)
				{
					if (jsonBuffer[i] == ' ')
					{
						jsonBuffer[i] = 0;
						break;
					}
				}
				EnumValueTypes valueType = GetValueTypeByTypeName(&jsonBuffer[nameToken.start + 2]);
				char* value = &jsonBuffer[token.start];
				switch (valueType)
				{
				case ValueType_Int64:
				case ValueType_UInt64:
					if (!parsePrimitiveInt(valueType, value, filePos, pWriter))
						throw AssetUtilError("Primitive value out of range.");
					parsed = true;
					break;
				case ValueType_Float:
					{
						float floatTmp;
						if (!parseFloatStr<float>(floatTmp, "\"" + std::string(&jsonBuffer[token.start], &jsonBuffer[token.end]) + "\""))
							throw AssetUtilError("Unable to parse float value.");
						pWriter->Write(filePos, 4, &floatTmp); filePos += 4;
					}
					parsed = true;
					break;
				case ValueType_Double:
					{
						double doubleTmp;
						if (!parseFloatStr<double>(doubleTmp, "\"" + std::string(&jsonBuffer[token.start], &jsonBuffer[token.end]) + "\""))
							throw AssetUtilError("Unable to parse float value.");
						pWriter->Write(filePos, 8, &doubleTmp); filePos += 8;
					}
					parsed = true;
					break;
				}
			}
			if (parsed)
				break;
			// Handle escaped string values.
			QWORD outStartPos = filePos;
			unsigned int inCount = 0;
			unsigned int outCount = 0;
			pWriter->Write(filePos, 4, &outCount); filePos += 4;
			for (int i = token.start; i < token.end; i++)
			{
				if (jsonBuffer[i] != '\\')
					continue;
				//Write the pending characters.
				pWriter->Write(filePos, i - (token.start + inCount), &jsonBuffer[token.start + inCount]);
				filePos += i - (token.start + inCount);
				inCount = i - token.start;
				//inCount now points to the '\\' character.
				if ((i + 1) >= token.end)
					break;
				i++;
				//Parse the escape sequence.
				switch (jsonBuffer[i])
				{
				case '"':
				case '\\':
				case '/':
					//The character will be directly copied (next Write call).
					//Increment inCount beyond the '\\' character.
					inCount++;
					break;
				case 'b':
					pWriter->Write(filePos, 1, "\b");
					filePos++; inCount += 2;
					break;
				case 'f':
					pWriter->Write(filePos, 1, "\f");
					filePos++; inCount += 2;
					break;
				case 'n':
					pWriter->Write(filePos, 1, "\n");
					filePos++; inCount += 2;
					break;
				case 'r':
					pWriter->Write(filePos, 1, "\r");
					filePos++; inCount += 2;
					break;
				case 't':
					pWriter->Write(filePos, 1, "\t");
					filePos++; inCount += 2;
					break;
				case 'u':
					//Should also suppate UTF-16 surrogate pairs.
					if ((i + 5) >= token.end || (i + 5) < 0)
						break;
					{
						wchar_t fullCharacter[2] = { 0,0 };
						size_t endIdx = SIZE_MAX;
						bool valid = false;
						try {
							//Parse primary "\\u" sequence.
							wchar_t firstChar = (wchar_t)
								std::stoul(std::string(&jsonBuffer[i + 1], &jsonBuffer[i + 1 + 4]), &endIdx, 16);
							valid = (endIdx == 4);
							i += 4; inCount += 6;
							if (valid)
							{
								fullCharacter[0] = firstChar;
								if (firstChar >= 0xD800 && firstChar <= 0xDFFF)
								{
									//Look for and parse secondary "\\u" sequence.
									if ((i + 6) < token.end && (i + 6) > 0 && jsonBuffer[i + 1] == '\\' && jsonBuffer[i + 2] == 'u')
									{
										wchar_t secondChar = (wchar_t)
											std::stoul(std::string(&jsonBuffer[i + 3], &jsonBuffer[i + 3 + 4]), &endIdx, 16);
										valid = (endIdx == 4);
										i += 6; inCount += 6;
										fullCharacter[1] = secondChar;
									}
									else
										valid = false;
								}
							}
						}
						catch (std::invalid_argument) { valid = false;  }
						catch (std::out_of_range) { valid = false; }
						if (!valid)
							throw AssetUtilError("Unable to parse a JSON \\u sequence (invalid characters?)");
						std::string utf8Str = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>("").
							to_bytes(&fullCharacter[0], &fullCharacter[fullCharacter[1] ? 2 : 1]);
						if (utf8Str.empty())
							throw AssetUtilError("Unable to convert a JSON \\u sequence (invalid UTF-16?)");
						pWriter->Write(filePos, utf8Str.size() * sizeof(char), utf8Str.data());
						filePos += utf8Str.size() * sizeof(char);
					}
					break;
				}
			}
			pWriter->Write(filePos, token.end - (token.start + inCount), &jsonBuffer[token.start + inCount]);
			filePos += token.end - (token.start + inCount);
			outCount = (unsigned int)(filePos - outStartPos - 4);
			pWriter->Write(outStartPos, 4, &outCount);
		}
		break;
		case JSMN_PRIMITIVE:
		{
			jsonBuffer[token.end] = 0;
			EnumValueTypes valueType = ValueType_None;
			if (token.parent != -1)
			{
				if (tokens[token.parent].type == JSMN_STRING) //parent is the name
				{
					int size = tokens[token.parent].end - tokens[token.parent].start;
					int start = tokens[token.parent].start + 2;
					if (size > 11 && !strncmp(&jsonBuffer[start], "unsigned ", 9))
						start += 9;
					for (int i = start; i < tokens[token.parent].end; i++)
					{
						if (jsonBuffer[i] == ' ')
						{
							jsonBuffer[i] = 0;
							break;
						}
					}
					valueType = GetValueTypeByTypeName(&jsonBuffer[tokens[token.parent].start + 2]);
				}
				else if (tokens[token.parent].type == JSMN_ARRAY) //byte array
				{
					valueType = ValueType_UInt8;
				}
			}
			if (valueType == ValueType_None)
				throw AssetUtilError("Encountered an unknown primitive value type.");
			char* value = &jsonBuffer[token.start];
			switch (valueType)
			{
			case ValueType_Bool:
				{
					uint8_t boolTmp = (!_strnicmp(value, "1", 1) || !_strnicmp(value, "true", 4)) ? 1 : 0;
					pWriter->Write(filePos, 1, &boolTmp); filePos += 1;
				}
				break;
			case ValueType_Int8:
			case ValueType_Int16:
			case ValueType_Int32:
			case ValueType_Int64:
			case ValueType_UInt8:
			case ValueType_UInt16:
			case ValueType_UInt32:
			case ValueType_UInt64:
			{
				if (!parsePrimitiveInt(valueType, value, filePos, pWriter))
					throw AssetUtilError("Primitive value out of range.");
			}
			break;
			case ValueType_Float:
				{
					float floatTmp;
					if (!parseFloatStr<float>(floatTmp, std::string(&jsonBuffer[token.start], &jsonBuffer[token.end])))
						throw AssetUtilError("Unable to parse float value.");
					pWriter->Write(filePos, 4, &floatTmp); filePos += 4;
				}
				break;
			case ValueType_Double:
				{
					double doubleTmp;
					if (!parseFloatStr<double>(doubleTmp, std::string(&jsonBuffer[token.start], &jsonBuffer[token.end])))
						throw AssetUtilError("Unable to parse float value.");
					pWriter->Write(filePos, 8, &doubleTmp); filePos += 8;
				}
				break;
			}
		}
		}
		//Alignment
		{
			//Because a value token's name is its parent token, this works when the next token is a direct neighbour or a parent's neighbour.
			int target = -1;
			if ((i + 1) < actualTokenCount)
			{
				if (tokens[i + 1].parent == i)
					//Don't need to check because : 
					//1) i is a JSMN_OBJECT, i+1 a child,
					//or 2) i is a JSMN_STRING and the name of i+1, so i isn't represented in the binary and doesn't need alignment.
					continue;
				target = tokens[i + 1].parent;
			}
			jsmntok_t& curParentToken = token;
			while ((curParentToken.parent != -1) && (curParentToken.parent > target))
			{
				curParentToken = tokens[curParentToken.parent];
				if (curParentToken.type == JSMN_STRING) //The name of i or one of i's parents.
				{
					if (jsonBuffer[curParentToken.start] == '1') //Alignment indicator is on.
					{
						QWORD alignCount = (4 - (filePos & 3)) & 3;
						if (alignCount > 0)
						{
							uint32_t nullDw = 0;
							pWriter->Write(filePos, alignCount, &nullDw); filePos += alignCount;
						}
						break; //Don't need to continue as another alignment is a redundant one.
					}
				}
			}
		}
	}
}
