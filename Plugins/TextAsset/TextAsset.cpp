#include "../UABE_Generic/PluginManager.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/AppContext.h"
#include <unordered_map>
#include <assert.h>

static bool SupportsElements(std::vector<AssetUtilDesc>& elements)
{
	std::unordered_map<AssetsFileContextInfo*, int32_t> textClassIDs;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i].asset.pFile == nullptr)
			return false;
		AssetsFileContextInfo* pFile = elements[i].asset.pFile.get();
		auto classIDsit = textClassIDs.find(pFile);
		int32_t textAssetClassID = -1;
		if (classIDsit == textClassIDs.end())
		{
			textAssetClassID = pFile->GetClassByName("TextAsset");
			textClassIDs[pFile] = textAssetClassID;
		}
		else
			textAssetClassID = classIDsit->second;
		if (textAssetClassID == -1)
			return false;
		int32_t classId = elements[i].asset.getClassID();
		if (classId != textAssetClassID)
			return false;
	}
	return true;
}

static void SubstituteTextAssetStringType(AssetTypeTemplateField& templateBase)
{
	for (uint32_t i = 0; i < templateBase.children.size(); i++)
	{
		if (templateBase.children[i].name == "m_Script")
		{
			templateBase.children[i].type = "_string";
			templateBase.children[i].valueType = ValueType_None;
			for (uint32_t k = 0; k < templateBase.children[i].children.size(); k++)
			{
				if (templateBase.children[i].children[k].name == "Array")
				{
					templateBase.children[i].children[k].type = "TypelessData";
					break;
				}
			}
			break;
		}
	}
}

static void FreeByteBufCallback(void* buffer)
{
	if (buffer)
		delete[](uint8_t*)buffer;
}

class TextAssetImportTask : public AssetImportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
public:
	TextAssetImportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::vector<std::string> _importFilePaths,
		bool stopOnError = false)

		: AssetImportTask(std::move(_assets), std::move(_importFilePaths), "Import TextAssets", stopOnError),
		  appContext(appContext)
	{}

	bool importAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
	{
		if (desc.asset.pFile == nullptr)
			throw AssetUtilError("Unable to find the target .assets file.");

		IAssetsReader_ptr pAssetReader = desc.asset.makeReader();
		if (pAssetReader == nullptr)
			throw AssetUtilError("Unable to read the asset.");
		QWORD assetSize = 0;
		if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
			throw AssetUtilError("Unable to read the asset.");

		AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset, &SubstituteTextAssetStringType);
		AssetTypeTemplateField* pTemplateBase = &templateBase;

		AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
		AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
		if (pBaseField == nullptr || pBaseField->IsDummy())
			throw AssetUtilError("Unable to deserialize the asset.");

		AssetTypeValueField* scriptField = pBaseField->Get("m_Script");
		AssetTypeValueField* dataArrayField = scriptField->Get("Array");
		if (scriptField->IsDummy() || dataArrayField->GetValue() == nullptr || dataArrayField->GetValue()->GetType() != ValueType_ByteArray)
			throw AssetUtilError("Unexpected TextAsset format.");

		std::unique_ptr<IAssetsReader> pTextReader(Create_AssetsReaderFromFile(path.c_str(), true, RWOpenFlags_Immediately));// desc.asset.makeReader();
		if (pTextReader == nullptr)
			throw AssetUtilError("Unable to read the text file.");
		QWORD textSize = 0;
		if (!pTextReader->Seek(AssetsSeek_End, 0) || !pTextReader->Tell(textSize) || !pTextReader->Seek(AssetsSeek_Begin, 0))
			throw AssetUtilError("Unable to read the text file.");
		if (textSize >= INT32_MAX)
			throw AssetUtilError("The text file is too large (should be below 2 GiB).");
		std::unique_ptr<uint8_t[]> textBuf(new uint8_t[(size_t)textSize]);
		if (pTextReader->Read(textSize, textBuf.get()) != textSize)
			throw AssetUtilError("Unable to read the text file.");

		AssetTypeByteArray byteArrayValue = {};
		byteArrayValue.data = textBuf.get();
		byteArrayValue.size = (uint32_t)textSize;
		dataArrayField->GetValue()->Set(&byteArrayValue);

		QWORD outSize = pBaseField->GetByteSize(0);
		if (outSize >= SIZE_MAX)
			throw AssetUtilError("Import size out of range.");
		std::unique_ptr<uint8_t[]> newDataBuf(new uint8_t[outSize]);
		std::unique_ptr<IAssetsWriter> pTempWriter(Create_AssetsWriterToMemory(newDataBuf.get(), outSize));
		if (pTempWriter == nullptr)
			throw AssetUtilError("Unexpected runtime error.");
		QWORD newByteSize = pBaseField->Write(pTempWriter.get(), 0, desc.asset.isBigEndian());

		std::shared_ptr<AssetsEntryReplacer> pReplacer(MakeAssetModifierFromMemory(0, desc.asset.pathID,
			desc.asset.getClassID(), desc.asset.getMonoScriptID(),
			newDataBuf.release(), (size_t)newByteSize, FreeByteBufCallback));
		if (pReplacer == nullptr)
			throw AssetUtilError("Unexpected runtime error.");
		desc.asset.pFile->addReplacer(pReplacer, appContext);
		return true;
	}
};
class TextAssetImportProvider : public IAssetOptionProviderGeneric
{
public:
	class Runner : public IOptionRunner
	{
		AppContext& appContext;
		std::vector<AssetUtilDesc> selection;
	public:
		Runner(AppContext& appContext, std::vector<AssetUtilDesc> _selection)
			: appContext(appContext), selection(std::move(_selection))
		{}
		void operator()()
		{
			std::vector<std::string> importLocations = appContext.QueryAssetImportLocation(selection, ".txt", "\\.txt", "*.txt|Text file:");
			if (!importLocations.empty())
			{
				auto pTask = std::make_shared<TextAssetImportTask>(appContext, std::move(selection), std::move(importLocations));
				appContext.taskManager.enqueue(pTask);
			}
		}
	};
	EAssetOptionType getType()
	{
		return EAssetOptionType::Import;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class AppContext& appContext,
		std::vector<AssetUtilDesc> selection,
		std::string& optionName)
	{
		if (!SupportsElements(selection))
			return nullptr;
		optionName = "Import from .txt";
		return std::make_unique<Runner>(appContext, std::move(selection));
	}
};


class TextAssetExportTask : public AssetExportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
public:
	TextAssetExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export TextAssets", ".txt", std::move(_baseDir), stopOnError),
		appContext(appContext)
	{}

	bool exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
	{
		if (desc.asset.pFile == nullptr)
			throw AssetUtilError("Unable to find the target .assets file.");

		IAssetsReader_ptr pAssetReader = desc.asset.makeReader();
		if (pAssetReader == nullptr)
			throw AssetUtilError("Unable to read the asset.");
		QWORD assetSize = 0;
		if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
			throw AssetUtilError("Unable to read the asset.");

		AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset, &SubstituteTextAssetStringType);
		AssetTypeTemplateField* pTemplateBase = &templateBase;

		AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
		AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
		if (pBaseField == nullptr || pBaseField->IsDummy())
			throw AssetUtilError("Unable to deserialize the asset.");

		AssetTypeValueField* scriptField = pBaseField->Get("m_Script");
		AssetTypeValueField* dataArrayField = scriptField->Get("Array");
		if (scriptField->IsDummy() || dataArrayField->GetValue() == nullptr || dataArrayField->GetValue()->GetType() != ValueType_ByteArray)
			throw AssetUtilError("Unexpected TextAsset format.");

		AssetTypeByteArray* pByteArray = dataArrayField->GetValue()->AsByteArray();

		std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(path.c_str(), true, true, RWOpenFlags_Immediately));
		if (pWriter == nullptr)
			throw AssetUtilError("Unable to create the output file.");

		if (pWriter->Write(pByteArray->size, pByteArray->data) != pByteArray->size)
			throw AssetUtilError("Unable to write the data.");

		return true;
	}
};
class TextAssetExportProvider : public IAssetOptionProviderGeneric
{
public:
	class Runner : public IOptionRunner
	{
		AppContext& appContext;
		std::vector<AssetUtilDesc> selection;
	public:
		Runner(AppContext& appContext, std::vector<AssetUtilDesc> _selection)
			: appContext(appContext), selection(std::move(_selection))
		{}
		void operator()()
		{
			std::string exportLocation = appContext.QueryAssetExportLocation(selection, ".txt", "*.txt|Text file:");
			if (!exportLocation.empty())
			{
				auto pTask = std::make_shared<TextAssetExportTask>(appContext, std::move(selection), std::move(exportLocation));
				appContext.taskManager.enqueue(pTask);
			}
		}
	};
	EAssetOptionType getType()
	{
		return EAssetOptionType::Export;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class AppContext& appContext,
		std::vector<struct AssetUtilDesc> selection,
		std::string& optionName)
	{
		if (!SupportsElements(selection))
			return nullptr;
		optionName = "Export to .txt";
		return std::make_unique<Runner>(appContext, std::move(selection));
	}
};

class TextAssetPluginDesc : public IPluginDesc
{
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	TextAssetPluginDesc()
	{
		pProviders = { std::make_shared<TextAssetExportProvider>(), std::make_shared<TextAssetImportProvider>() };
	}
	std::string getName()
	{
		return "TextAsset";
	}
	std::string getAuthor()
	{
		return "";
	}
	std::string getDescriptionText()
	{
		return "Export and import the content of TextAsset assets.";
	}
	//The IPluginDesc object should keep a reference to the returned options, as the caller may keep only std::weak_ptrs.
	//Note: May be called early, e.g. before program UI initialization.
	std::vector<std::shared_ptr<IOptionProvider>> getPluginOptions(class AppContext& appContext)
	{
		return pProviders;
	}
};

IPluginDesc* GetUABEPluginDesc1(size_t sizeof_AppContext, size_t sizeof_BundleFileContextInfo)
{
	if (sizeof_AppContext != sizeof(AppContext) || sizeof_BundleFileContextInfo != sizeof(BundleFileContextInfo))
	{
		assert(false);
		return nullptr;
	}
	return new TextAssetPluginDesc();
}
