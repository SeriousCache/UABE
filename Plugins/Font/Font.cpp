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
		int32_t FontClassID = -1;
		if (classIDsit == textClassIDs.end())
		{
			FontClassID = pFile->GetClassByName("Font");
			textClassIDs[pFile] = FontClassID;
		}
		else
			FontClassID = classIDsit->second;
		if (FontClassID == -1)
			return false;
		int32_t classId = elements[i].asset.getClassID();
		if (classId != FontClassID)
			return false;
	}
	return true;
}

static bool isOtf(AssetTypeByteArray* data1){
    auto data = data1->data;
    return data[0] == 0x4f && data[1] == 0x54 && data[2] == 0x54 && data[3] == 0x4f;
}

static void FreeByteBufCallback(void* buffer)
{
    if (buffer)
        delete[](uint8_t*)buffer;
}

class FontImportTask : public AssetImportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
public:
	FontImportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::vector<std::string> _importFilePaths,
		bool stopOnError = false)

		: AssetImportTask(std::move(_assets), std::move(_importFilePaths), "Import Font", stopOnError),
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

        AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset,
          [](AssetTypeTemplateField& templateBase) {
              for (uint32_t i = 0; i < templateBase.children.size(); i++)
              {
                  if (templateBase.children[i].name == "m_FontData")
                  {
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
        );

        AssetTypeTemplateField* pTemplateBase = &templateBase;

        AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
        AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
        if (pBaseField == nullptr || pBaseField->IsDummy())
            throw AssetUtilError("Unable to deserialize the asset.");

        AssetTypeValueField* dataArrayField = pBaseField->Get("m_FontData")->Get(0U);
        if (dataArrayField->GetValue() == nullptr)
            throw AssetUtilError("Empty data");

        if(dataArrayField->GetValue()->GetType() != ValueType_ByteArray) {
            throw AssetUtilError("Unexpected Font format.");
        }

        AssetTypeByteArray* bytes = dataArrayField->GetValue()->AsByteArray();

        std::unique_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(path.c_str(), true, RWOpenFlags_Immediately));
        if (pReader == nullptr)
            throw AssetUtilError("Unable to read the file.");

        QWORD size = 0;
        if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(size) || !pReader->Seek(AssetsSeek_Begin, 0))
            throw AssetUtilError("Unable to read the file.");

        if (size >= INT32_MAX)
            throw AssetUtilError("The file is too large (should be below 2 GiB).");

        std::unique_ptr<uint8_t[]> buf(new uint8_t[(size_t)size]);

        if (pReader->Read(size, buf.get()) != size)
            throw AssetUtilError("Unable to read the file.");

        AssetTypeByteArray byteArrayValue = {};
        byteArrayValue.data = buf.get();
        byteArrayValue.size = (uint32_t)size;
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
class FontImportProvider : public IAssetOptionProviderGeneric
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
			std::vector<std::string> importLocations = appContext.QueryAssetImportLocation(selection, "", "\\.*", "*|Font file:");
			if (!importLocations.empty())
			{
				auto pTask = std::make_shared<FontImportTask>(appContext, std::move(selection), std::move(importLocations));
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
		optionName = "Import font";
		return std::make_unique<Runner>(appContext, std::move(selection));
	}
};


class FontExportTask : public AssetExportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
public:
	FontExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export Font", ".ttf", std::move(_baseDir), stopOnError),
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

        AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset,
          [](AssetTypeTemplateField& templateBase) {
              for (uint32_t i = 0; i < templateBase.children.size(); i++)
              {
                  if (templateBase.children[i].name == "m_FontData")
                  {
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
        );
		AssetTypeTemplateField* pTemplateBase = &templateBase;

        AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
        AssetTypeValueField* pBaseField = assetInstance.GetBaseField();

        if (pBaseField == nullptr || pBaseField->IsDummy())
            throw AssetUtilError("Unable to deserialize the asset.");

        AssetTypeValueField* dataArrayField = pBaseField->Get("m_FontData")->Get(0U);
        if (dataArrayField->GetValue() == nullptr)
            throw AssetUtilError("Empty data");

        if(dataArrayField->GetValue()->GetType() != ValueType_ByteArray) {
            throw AssetUtilError("Unexpected Font format.");
        }

        AssetTypeByteArray* bytes = dataArrayField->GetValue()->AsByteArray();

        std::string ext = isOtf(bytes) ? ".otf" : ".ttf";
        std::string fullOutputPath = path + ext;
        std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(fullOutputPath.c_str(), true, true, RWOpenFlags_Immediately));
        if (pWriter == nullptr)
            throw AssetUtilError("Unable to create the output file.");

        QWORD size = bytes->size;
        if (pWriter->Write(size, bytes->data) != size)
            throw AssetUtilError("Unable to write data to the output file.");

		return true;
    }
};
class FontExportProvider : public IAssetOptionProviderGeneric
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
			std::string exportLocation = appContext.QueryAssetExportLocation(selection, "", "*|Font file:");
			if (!exportLocation.empty())
			{
				auto pTask = std::make_shared<FontExportTask>(appContext, std::move(selection), std::move(exportLocation));
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
		optionName = "Export font";
		return std::make_unique<Runner>(appContext, std::move(selection));
	}
};

class FontPluginDesc : public IPluginDesc
{
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	FontPluginDesc()
	{
		pProviders = { std::make_shared<FontExportProvider>(), std::make_shared<FontImportProvider>() };
	}
	std::string getName()
	{
		return "Font";
	}
	std::string getAuthor()
	{
		return "deadYokai";
	}
	std::string getDescriptionText()
	{
		return "Export and import the content of Font assets.";
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
	return new FontPluginDesc();
}
