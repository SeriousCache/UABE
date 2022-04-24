#include <unordered_map>
#include <memory>
#include "../libStringConverter/convert.h"
#include "../UABE_Generic/PluginManager.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/AppContext.h"
#include "../AssetsTools/TextureFileFormat.h"

#include "Texture.h"

#ifdef _WIN32
#define STBI_WINDOWS_UTF8
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define LODEPNG_NO_COMPILE_DECODER
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#include "lodepng.h"

#pragma region Import
static int stbi__AssetsTools_read(void* user, char* data, int size)
{
	return (int)((IAssetsReader*)user)->Read((QWORD)size, data);
}
static void stbi__AssetsTools_skip(void* user, int n)
{
	((IAssetsReader*)user)->Seek(AssetsSeek_Cur, n);
}
static int stbi__AssetsTools_eof(void* user)
{
	if (((IAssetsReader*)user)->Seek(AssetsSeek_Cur, 1))
	{
		((IAssetsReader*)user)->Seek(AssetsSeek_Cur, -1);
		return 0;
	}
	return 1;
}
static stbi_io_callbacks stbi__AssetsTools_callbacks =
{
   stbi__AssetsTools_read,
   stbi__AssetsTools_skip,
   stbi__AssetsTools_eof,
};

bool LoadTextureFromFile(const char *filePath, std::vector<uint8_t> &newTextureData, unsigned int &newWidth, unsigned int &newHeight)
{
	std::unique_ptr<IAssetsReader> pImageFileReader(Create_AssetsReaderFromFile(filePath, true, RWOpenFlags_Immediately));
	if (pImageFileReader == nullptr)
		return false;

	int width, height, n;
	stbi__vertically_flip_on_load = 1; //added because the image is flipped on save
	uint8_t *pImg = stbi_load_from_callbacks(&stbi__AssetsTools_callbacks, pImageFileReader.get(), &width, &height, &n, 4);
	if (pImg == NULL)
		return false;
	newWidth = width;
	newHeight = height;
	newTextureData.assign(&pImg[0], &pImg[(size_t)width * height * 4]);
	free(pImg);
	return true;
}
#pragma endregion

bool PluginSupportsElements(std::vector<AssetUtilDesc>& elements)
{
	std::unordered_map<AssetsFileContextInfo*, int32_t> texture2DClassIDs;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i].asset.pFile == nullptr)
			return false;
		AssetsFileContextInfo* pFile = elements[i].asset.pFile.get();
		auto classIDsit = texture2DClassIDs.find(pFile);
		int32_t texture2DClassID = -1;
		if (classIDsit == texture2DClassIDs.end())
		{
			texture2DClassID = pFile->GetClassByName("Texture2D");
			texture2DClassIDs[pFile] = texture2DClassID;
		}
		else
			texture2DClassID = classIDsit->second;
		if (texture2DClassID == -1)
			return false;
		int32_t classId = elements[i].asset.getClassID();
		if (classId != texture2DClassID)
			return false;
	}
	return true;
}

class TextureExportTask : public AssetExportTask
{
	AppContext& appContext;
	EExportFormat exportFormat;
	TypeTemplateCache templateCache;
public:
	TextureExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir, std::string _extension, EExportFormat exportFormat,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export Texture2D", std::move(_extension), std::move(_baseDir), stopOnError),
		appContext(appContext), exportFormat(exportFormat)
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

		AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset);
		AssetTypeTemplateField* pTemplateBase = &templateBase;

		AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
		AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
		if (pBaseField == nullptr || pBaseField->IsDummy())
			throw AssetUtilError("Unable to deserialize the asset.");

		TextureFile textureFile; memset(&textureFile, 0, sizeof(TextureFile));
		if (!ReadTextureFile(&textureFile, pBaseField))
			throw AssetUtilError("Unknown Texture2D asset format.");
		//Retrieve the texture format version.
		SupportsTextureFormat(desc.asset.pFile->getAssetsFileContext()->getAssetsFile(),
			(TextureFormat)0,
			textureFile.extra.textureFormatVersion);

		if (textureFile._pictureDataSize == 0)
			textureFile.pPictureData = NULL;
		std::vector<uint8_t> texDataResourceBuf;
		std::vector<uint8_t> texDataDecompressed((size_t)textureFile.m_Width * textureFile.m_Height * 4);
		if (textureFile._pictureDataSize == 0 && (textureFile.m_Width * textureFile.m_Height) > 0 && textureFile.m_StreamData.size)
		{
			std::shared_ptr<ResourcesFileContextInfo> streamResourcesContextInfo;
			streamResourcesContextInfo = FindResourcesFile(appContext, textureFile.m_StreamData.path, desc.asset, progressManager);
			//Non-null guaranteed by FindResourcesFile (AssetUtilError thrown otherwise).

			std::shared_ptr<IAssetsReader> pStreamReader = streamResourcesContextInfo->getResource(streamResourcesContextInfo,
				textureFile.m_StreamData.offset,
				textureFile.m_StreamData.size);
			if (pStreamReader == nullptr)
				throw AssetUtilError("Unable to locate the texture resource.");

			texDataResourceBuf.resize(textureFile.m_StreamData.size);
			textureFile.pPictureData = texDataResourceBuf.data();
			if (pStreamReader->Read(0, textureFile.m_StreamData.size, textureFile.pPictureData)
				< textureFile.m_StreamData.size)
				throw AssetUtilError("Unable to read data from the texture resource.");

			textureFile._pictureDataSize = textureFile.m_StreamData.size;
		}
		if (!GetTextureData(&textureFile, texDataDecompressed.data()))
			throw AssetUtilError("Unable to convert the texture data.");

		switch (exportFormat)
		{
		case EExportFormat::PNG:
			{
				if (textureFile.m_Height > 1)
				{
					std::vector<uint8_t> flipDataTmp(textureFile.m_Width * 4);
					unsigned int stride = textureFile.m_Width * 4;
					unsigned int halfHeight = textureFile.m_Height / 2;
					unsigned int curLinePos = 0;
					unsigned int curEndLinePos = stride * (textureFile.m_Height - 1);
					for (unsigned int y = 0; y < halfHeight; y++)
					{
						memcpy(flipDataTmp.data(), &texDataDecompressed.data()[curLinePos], stride);
						memcpy(&texDataDecompressed.data()[curLinePos], &texDataDecompressed.data()[curEndLinePos], stride);
						memcpy(&texDataDecompressed.data()[curEndLinePos], flipDataTmp.data(), stride);

						curLinePos += stride;
						curEndLinePos -= stride;
					}
				}
				//stbi_write_pngW(targetFile, g_TextureWidth, g_TextureHeight, 4, textureData, g_TextureWidth*4);
				auto pathW = unique_MultiByteToWide(path.c_str());
				lodepng_encode32_fileW(pathW.get(), texDataDecompressed.data(), textureFile.m_Width, textureFile.m_Height);
			}
			break;
		case EExportFormat::TGA:
			stbi_write_tga(path.c_str(), textureFile.m_Width, textureFile.m_Height, 4, true, texDataDecompressed.data());
			break;
		default:
			assert(false);
			throw AssetUtilError("Unexpected export format.");
			break;
		}

		return true;
	}
};
class TextureExportProvider : public IAssetOptionProviderGeneric
{
public:
	std::string optionName;
	std::string extension;
	std::string extensionFilter;
	EExportFormat exportFormat;
	class Runner : public IOptionRunner
	{
		AppContext& appContext;
		std::vector<AssetUtilDesc> selection;
		std::string extension;
		std::string extensionFilter;
		EExportFormat exportFormat;
	public:
		Runner(AppContext& appContext, std::vector<AssetUtilDesc> _selection,
			const std::string& extension, const std::string& extensionFilter,
			EExportFormat exportFormat)
			: appContext(appContext), selection(std::move(_selection)),
			  extension(extension), extensionFilter(extensionFilter), exportFormat(exportFormat)
		{}
		void operator()()
		{
			std::string exportLocation = appContext.QueryAssetExportLocation(selection, extension, extensionFilter);
			if (!exportLocation.empty())
			{
				auto pTask = std::make_shared<TextureExportTask>(appContext, std::move(selection), std::move(exportLocation), std::move(extension), exportFormat);
				appContext.taskManager.enqueue(pTask);
			}
		}
	};

	inline TextureExportProvider(const std::string &optionName,
		const std::string &extension, const std::string &extensionFilter,
		EExportFormat exportFormat)

		: optionName(optionName), extension(extension), extensionFilter(extensionFilter), exportFormat(exportFormat)
	{}
	EAssetOptionType getType()
	{
		return EAssetOptionType::Export;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class AppContext& appContext,
		std::vector<struct AssetUtilDesc> selection,
		std::string& optionName)
	{
		if (!PluginSupportsElements(selection))
			return nullptr;
		optionName = this->optionName;
		return std::make_unique<Runner>(appContext, std::move(selection), extension, extensionFilter, exportFormat);
	}
};

//class GenericTexturePluginDesc : public IPluginDesc
//{
	GenericTexturePluginDesc::GenericTexturePluginDesc()
	{
		pProviders = { 
			std::make_shared<TextureExportProvider>("Export to .png", ".png", "*.png|PNG file:", EExportFormat::PNG),
			std::make_shared<TextureExportProvider>("Export to .tga", ".tga", "*.tga|TGA file:", EExportFormat::TGA),
			//std::make_shared<TextureImportProvider>() 
		};
	}
	std::string GenericTexturePluginDesc::getName()
	{
		return "Texture";
	}
	std::string GenericTexturePluginDesc::getAuthor()
	{
		return "";
	}
	std::string GenericTexturePluginDesc::getDescriptionText()
	{
		return "Export and import the content of Texture2D assets.";
	}
	//The IPluginDesc object should keep a reference to the returned options, as the caller may keep only std::weak_ptrs.
	//Note: May be called early, e.g. before program UI initialization.
	std::vector<std::shared_ptr<IOptionProvider>> GenericTexturePluginDesc::getPluginOptions(class AppContext& appContext)
	{
		return pProviders;
	}
//};
