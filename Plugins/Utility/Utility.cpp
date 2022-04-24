#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "../UABE_Win32/Win32PluginManager.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Win32/Win32AppContext.h"
#include "../UABE_Win32/FileDialog.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../libStringConverter/convert.h"
#include <format>

enum class ResourceRefType
{
	StreamingInfo,
	StreamedResource,
	Other
};
struct ResourceRef
{
	AssetTypeValueField* pOffsetField = nullptr;
	AssetTypeValueField* pSizeField = nullptr;
	AssetTypeValueField* pPathField = nullptr;
	ResourceRefType type = ResourceRefType::Other;
	bool Read(AssetTypeValueField* pParentField)
	{
		if (pParentField->GetType() == "StreamingInfo")
		{
			pOffsetField = pParentField->Get("offset");
			pSizeField = pParentField->Get("size");
			pPathField = pParentField->Get("path");
			type = ResourceRefType::StreamingInfo;
		}
		else if (pParentField->GetType() == "StreamedResource")
		{
			pOffsetField = pParentField->Get("m_Offset");
			pSizeField = pParentField->Get("m_Size");
			pPathField = pParentField->Get("m_Source");
			type = ResourceRefType::StreamedResource;
		}
		else
		{
			pOffsetField = nullptr;
			pSizeField = nullptr;
			pPathField = nullptr;
			type = ResourceRefType::Other;
		}
		if (!pOffsetField || pOffsetField->IsDummy() || !pOffsetField->GetValue()
			|| !pSizeField || pSizeField->IsDummy() || !pSizeField->GetValue()
			|| !pPathField || pPathField->IsDummy() || !pPathField->GetValue() || pPathField->GetValue()->GetType() != ValueType_String)
			return false;
		return true;
	}
};

static void reportError(Win32AppContext& appContext, const char *message)
{
	auto messageT = unique_MultiByteToTCHAR(message);
	MessageBox(appContext.getMainWindow().getWindow(),
		messageT.get(), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
}

class SelectedResourceExportProvider : public IAssetViewEntryOptionProvider
{
public:
	EAssetOptionType getType()
	{
		return EAssetOptionType::Export;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetViewModifyDialog& assetViewDialog,
		AssetViewModifyDialog::FieldInfo fieldInfo,
		std::string& optionName)
	{
		ResourceRef resourceRef;
		if (!resourceRef.Read(fieldInfo.pValueField))
			return nullptr;
		optionName = "Export resource to file";
		return std::make_unique<OptionRunnerByFn>([&appContext, &assetViewDialog, fieldInfo, resourceRef]()
			{
				uint64_t offset = resourceRef.pOffsetField->GetValue()->AsUInt64();
				uint64_t size = resourceRef.pSizeField->GetValue()->AsUInt64();
				const char* path = resourceRef.pPathField->GetValue()->AsString();
				AssetIdentifier asset(fieldInfo.assetIDs.fileID, fieldInfo.assetIDs.pathID);
				if (!asset.resolve(appContext))
				{
					reportError(appContext, "Unable to find the asset.");
					return;
				}
				std::shared_ptr<ResourcesFileContextInfo> pResourcesFile = nullptr;
				try
				{
					pResourcesFile = FindResourcesFile(appContext, path, asset, {});
				}
				catch (AssetUtilError e)
				{
					reportError(appContext, e.what());
					return;
				}
				assert(pResourcesFile != nullptr); //Should be guaranteed by FindResourcesFile.
				std::shared_ptr<IAssetsReader> pResourceReader = pResourcesFile->getResource(pResourcesFile, offset, size);
				if (pResourceReader == nullptr)
				{
					reportError(appContext, "Unable to open the resource.");
					return;
				}
				wchar_t* saveFilePath = nullptr;
				if (SUCCEEDED(ShowFileSaveDialog(appContext.getMainWindow().getWindow(),
					&saveFilePath, L"*.*|Any file:",
					nullptr, nullptr, TEXT("Save resource data"),
					UABE_FILEDIALOG_EXPIMPASSET_GUID)))
				{
					std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(saveFilePath, true, true, RWOpenFlags_Immediately));
					FreeCOMFilePathBuf(&saveFilePath);
					if (pWriter == nullptr)
					{
						reportError(appContext, "Unable to open the output file.");
						return;
					}
					std::unique_ptr<AssetsEntryReplacer> pCopier(MakeAssetModifierFromReader(0, 0, 0, 0, pResourceReader.get(), size));
					if (pCopier->Write(0, pWriter.get()) != size)
					{
						reportError(appContext, "Unable to copy the data to the output file.");
						return;
					}
				}
			});
	}
};

class SelectedResourceImportProvider : public IAssetViewEntryOptionProvider
{
public:
	EAssetOptionType getType()
	{
		return EAssetOptionType::Import;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetViewModifyDialog& assetViewDialog,
		AssetViewModifyDialog::FieldInfo fieldInfo,
		std::string& optionName)
	{
		ResourceRef resourceRef;
		if (!resourceRef.Read(fieldInfo.pValueField))
			return nullptr;
		optionName = "Import resource from file";
		return std::make_unique<OptionRunnerByFn>([&appContext, &assetViewDialog, fieldInfo, resourceRef]()
		{
			AssetIdentifier asset(fieldInfo.assetIDs.fileID, fieldInfo.assetIDs.pathID);
			if (!asset.resolve(appContext))
			{
				reportError(appContext, "Unable to find the asset.");
				return;
			}
			std::string resourcesFileRefPath;
			auto tryFindResourcesFile = [&appContext, &asset, &resourcesFileRefPath](const std::string& path)
			{
				std::shared_ptr<ResourcesFileContextInfo> pResourcesFile = nullptr;
				try
				{
					pResourcesFile = FindResourcesFile(appContext, path, asset, {});
					if (pResourcesFile) resourcesFileRefPath = path;
				}
				catch (AssetUtilError e) {}
				return pResourcesFile;
			};
			std::shared_ptr<ResourcesFileContextInfo> pResourcesFile = tryFindResourcesFile(resourceRef.pPathField->GetValue()->AsString());
			if (pResourcesFile == nullptr)
			{
				std::string assetsFileRefName;
				auto pParentBundle = std::dynamic_pointer_cast<BundleFileContextInfo>(appContext.getContextInfo(asset.pFile->getParentFileID()));
				if (pParentBundle != 0)
				{
					std::string bundlePathName = pParentBundle->getBundlePathName();
					if (!bundlePathName.empty())
					{
						assetsFileRefName = std::string("archive:/") + bundlePathName + "/" + asset.pFile->getFileName();
					}
					else
						assetsFileRefName = asset.pFile->getFileName();
				}
				else
					assetsFileRefName = asset.pFile->getFileName();
				if (assetsFileRefName.size() > 7
					&& !strnicmp(&assetsFileRefName.data()[assetsFileRefName.size() - 7], ".assets", 7))
					assetsFileRefName.erase(assetsFileRefName.begin() + (assetsFileRefName.size() - 7), assetsFileRefName.end());
				//If the string iterator bug strikes again: assetsFileRefName = assetsFileRefName.substr(0, assetsFileRefName.size() - 7);
				if (resourceRef.type == ResourceRefType::StreamedResource)
				{
					pResourcesFile = tryFindResourcesFile(assetsFileRefName + ".resources");
					if (!pResourcesFile)
						pResourcesFile = tryFindResourcesFile(assetsFileRefName + ".resource");
					//".resources"
				}
				else //if (resourceRef.type == ResourceRefType::StreamingInfo)
					pResourcesFile = tryFindResourcesFile(assetsFileRefName + ".resS");
				if (pResourcesFile == nullptr)
				{
					std::string message = std::format("Unable to find the associated resources file.\n"
						"Make sure the {}{} file exists (or create an empty one) and open it within UABE.",
						asset.pFile->getFileName(),
						(resourceRef.type == ResourceRefType::StreamedResource) ? ".resource" : ".resS");
					reportError(appContext, message.c_str());
					return;
				}
			}
			wchar_t* openFilePath = nullptr;
			if (SUCCEEDED(ShowFileOpenDialog(appContext.getMainWindow().getWindow(),
				&openFilePath, L"*.*|Any file:",
				nullptr, nullptr, TEXT("Open file to import"),
				UABE_FILEDIALOG_EXPIMPASSET_GUID)))
			{
				std::shared_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(openFilePath, true, RWOpenFlags_Immediately));
				FreeCOMFilePathBuf(&openFilePath);
				if (pReader == nullptr)
				{
					reportError(appContext, "Unable to open the input file.");
					return;
				}
				QWORD importSize = 0;
				if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(importSize) || !pReader->Seek(AssetsSeek_Begin, 0))
				{
					reportError(appContext, "Unable to read the input file.");
					return;
				}
				uint64_t newOffset = 0;
				pResourcesFile->addResource(std::move(pReader), 0, importSize, newOffset);

				if (!assetViewDialog.setStringValue(resourceRef.pPathField, fieldInfo.assetIDs, resourcesFileRefPath))
				{
					//Shouldn't happen.
					reportError(appContext, "Unable to assign the new resource reference.");
					return;
				}
				resourceRef.pOffsetField->GetValue()->Set(&newOffset, ValueType_UInt64);
				resourceRef.pSizeField->GetValue()->Set(&importSize, ValueType_UInt64);
				assetViewDialog.updateValueFieldText(fieldInfo);
			}
		});
	}
};

class SelectedByteArrayExportProvider : public IAssetViewEntryOptionProvider
{
public:
	EAssetOptionType getType()
	{
		return EAssetOptionType::Export;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetViewModifyDialog& assetViewDialog,
		AssetViewModifyDialog::FieldInfo fieldInfo,
		std::string& optionName)
	{
		if (fieldInfo.pValueField->GetValue() == nullptr)
			return nullptr;
		if (fieldInfo.pValueField->GetValue()->GetType() == ValueType_Array)
		{
			if (fieldInfo.pValueField->GetTemplateField() == nullptr
				|| fieldInfo.pValueField->GetTemplateField()->children.size() != 2
				|| fieldInfo.pValueField->GetTemplateField()->children[0].valueType != ValueType_Int32
				|| (fieldInfo.pValueField->GetTemplateField()->children[1].valueType != ValueType_UInt8
					&& fieldInfo.pValueField->GetTemplateField()->children[1].valueType != ValueType_Int8))
				return nullptr;
		}
		else if (fieldInfo.pValueField->GetValue()->GetType() != ValueType_ByteArray
			&& fieldInfo.pValueField->GetValue()->GetType() != ValueType_String)
			return nullptr;
		optionName = "Export data to file";
		return std::make_unique<OptionRunnerByFn>([&appContext, &assetViewDialog, fieldInfo]()
			{
				std::unique_ptr<uint8_t[]> data_raii;
				const uint8_t *data = nullptr;
				size_t size = 0;

				switch (fieldInfo.pValueField->GetValue()->GetType())
				{
					case ValueType_Array:
					{
						size = fieldInfo.pValueField->GetChildrenCount();
						data_raii.reset(new uint8_t[size]);
						data = data_raii.get();
						AssetTypeValueField** ppChildren = fieldInfo.pValueField->GetChildrenList();
						for (size_t i = 0; i < size; ++i)
						{
							if (ppChildren[i]->GetValue() == nullptr)
							{
								reportError(appContext, "Unable to interpret the data.");
								return;
							}
							data_raii[i] = (uint8_t)ppChildren[i]->GetValue()->AsUInt();
						}
					}
					break;
					case ValueType_String:
					{
						data = reinterpret_cast<const uint8_t*>(fieldInfo.pValueField->GetValue()->AsString());
						if (data == nullptr)
						{
							reportError(appContext, "Unable to interpret the data.");
							return;
						}
						size = strlen(fieldInfo.pValueField->GetValue()->AsString());
					}
					break;
					case ValueType_ByteArray:
					{
						AssetTypeByteArray* pByteArray = fieldInfo.pValueField->GetValue()->AsByteArray();
						if (pByteArray == nullptr)
						{
							reportError(appContext, "Unable to interpret the data.");
							return;
						}
						data = pByteArray->data;
						size = pByteArray->size;
					}
					break;
					default:
					{
						//Should be excluded by prepareForSelection.
						reportError(appContext, "Unable to interpret the data.");
						return;
					}
				}

				wchar_t* saveFilePath = nullptr;
				if (FAILED(ShowFileSaveDialog(appContext.getMainWindow().getWindow(),
					&saveFilePath, L"*.*|Any file:",
					nullptr, nullptr, TEXT("Save resource data"),
					UABE_FILEDIALOG_EXPIMPASSET_GUID)))
					return;
				std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(saveFilePath, true, true, RWOpenFlags_Immediately));
				FreeCOMFilePathBuf(&saveFilePath);
				if (pWriter == nullptr)
				{
					reportError(appContext, "Unable to open the output file.");
					return;
				}
				if (pWriter->Write(size, data) != size)
				{
					reportError(appContext, "Unable to write the data to the output file.");
					return;
				}
			});
	}
};

class SelectedByteArrayImportProvider : public IAssetViewEntryOptionProvider
{
public:
	EAssetOptionType getType()
	{
		return EAssetOptionType::Import;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class Win32AppContext& appContext, class AssetViewModifyDialog& assetViewDialog,
		AssetViewModifyDialog::FieldInfo fieldInfo,
		std::string& optionName)
	{
		if (fieldInfo.pValueField->GetValue() == nullptr)
			return nullptr;
		if (fieldInfo.pValueField->GetValue()->GetType() == ValueType_Array)
		{
			if (fieldInfo.pValueField->GetTemplateField() == nullptr
				|| fieldInfo.pValueField->GetTemplateField()->children.size() != 2
				|| fieldInfo.pValueField->GetTemplateField()->children[0].valueType != ValueType_Int32
				|| (fieldInfo.pValueField->GetTemplateField()->children[1].valueType != ValueType_UInt8
					&& fieldInfo.pValueField->GetTemplateField()->children[1].valueType != ValueType_Int8)
				|| fieldInfo.pValueField->GetTemplateField()->children[1].align)
				return nullptr;
		}
		else if (fieldInfo.pValueField->GetValue()->GetType() != ValueType_ByteArray
			&& fieldInfo.pValueField->GetValue()->GetType() != ValueType_String)
			return nullptr;
		optionName = "Import data from file";
		return std::make_unique<OptionRunnerByFn>([&appContext, &assetViewDialog, fieldInfo]()
			{
				wchar_t* openFilePath = nullptr;
				if (FAILED(ShowFileOpenDialog(appContext.getMainWindow().getWindow(),
					&openFilePath, L"*.*|Any file:",
					nullptr, nullptr, TEXT("Open file to import"),
					UABE_FILEDIALOG_EXPIMPASSET_GUID)))
					return;
				std::shared_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(openFilePath, true, RWOpenFlags_Immediately));
				FreeCOMFilePathBuf(&openFilePath);
				if (pReader == nullptr)
				{
					reportError(appContext, "Unable to open the input file.");
					return;
				}
				QWORD importSize = 0;
				if (!pReader->Seek(AssetsSeek_End, 0) || !pReader->Tell(importSize) || !pReader->Seek(AssetsSeek_Begin, 0)
					|| importSize > std::numeric_limits<size_t>::max())
				{
					reportError(appContext, "Unable to read the input file.");
					return;
				}
				std::unique_ptr<uint8_t[]> importData(new uint8_t[(size_t)importSize]);
				if (pReader->Read(importSize, importData.get()) != importSize)
				{
					reportError(appContext, "Unable to read the input file.");
					return;
				}

				if (!assetViewDialog.setByteArrayValue(fieldInfo, std::move(importData), (size_t)importSize))
				{
					//Shouldn't happen.
					reportError(appContext, "Unable to assign the new data.");
					return;
				}
				assetViewDialog.updateValueFieldText(fieldInfo);
			});
	}
};

class UtilityPluginDesc : public IPluginDesc
{
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	UtilityPluginDesc()
	{
		pProviders = { 
			std::make_shared<SelectedResourceExportProvider>(), std::make_shared<SelectedResourceImportProvider>(),
			std::make_shared<SelectedByteArrayExportProvider>(), std::make_shared<SelectedByteArrayImportProvider>()
		};
	}
	std::string getName()
	{
		return "Utility";
	}
	std::string getAuthor()
	{
		return "";
	}
	std::string getDescriptionText()
	{
		return "Collection of small utility features.";
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
	return new UtilityPluginDesc();
}
