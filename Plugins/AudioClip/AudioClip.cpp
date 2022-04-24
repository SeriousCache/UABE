#include "../UABE_Generic/PluginManager.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/AppContext.h"
//#include "../AssetsTools/TextureFileFormat.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "../AssetsTools/ResourceManagerFile.h"

#include <fmod.hpp>

#include <tchar.h>
#include <filesystem>

#include "wavfile.h"

enum AudioType_U4
{
	AudioType_UNKNOWN=0,
	AudioType_ACC=1,
	AudioType_AIFF=2,
	AudioType_IT=10,
	AudioType_MOD=12,
	AudioType_MPEG=13, //.mp3
	AudioType_OGGVORBIS=14, //.ogg
	AudioType_S3M=0x11,
	AudioType_WAV=0x14,
	AudioType_XM=0x15,
	AudioType_XMA=0x16,
	AudioType_VAG=0x17,
	AudioType_AUDIOQUEUE=0x18,
	AudioType_MAX
};
uint8_t AudioTypeIndexTable[AudioType_MAX] = {
	0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 4, 5, 6, 0, 0, 7, 0, 0, 8, 9, 10, 11, 12
};
const char *AudioTypeExtensions[13] = {
	"", //0
	".acc",
	".aiff",
	"",
	".mod",
	".mp3",
	".ogg", //6
	".s3m",
	".wav",
	".xm",
	".xma",
	".vag",
	""
};
const wchar_t *AudioTypeExtensionsW[13] = {
	L"", //0
	L".acc",
	L".aiff",
	L"",
	L".mod",
	L".mp3",
	L".ogg", //6
	L".s3m",
	L".wav",
	L".xm",
	L".xma",
	L".vag",
	L""
};
const wchar_t *AudioTypeFileFilters[13] = {
	L"*.*|Unknown file format", //0
	L"*.acc|Advanced Audio Coding file",
	L"*.aiff|Audio Interchange File",
	L"*.*|Impulse tracker audio file",
	L"*.mod|Fasttracker .MOD file",
	L"*.mp3:*.mp2|MPEG audio file",
	L"*.ogg|Ogg vorbis container file", //6
	L"*.s3m|ScreamTracker 3 file",
	L"*.wav|Wave file",
	L"*.xm|FastTracker 2 XM file",
	L"*.xma|Xbox360 XMA file",
	L"*.vag|PlayStation ADPCM file",
	L"*.*|AudioQueue file"
};
enum AudioCompressionFormat_U4
{
	AudioCompressionFormat_PCM, //header-less
	AudioCompressionFormat_Vorbis, //.ogg
	AudioCompressionFormat_ADPCM, //headerless
	AudioCompressionFormat_MP3, //.mp3
	AudioCompressionFormat_VAG, //.vag
	AudioCompressionFormat_HEVAG, //no idea
	AudioCompressionFormat_XMA, //no idea
	AudioCompressionFormat_AAC  //.3gp or .aac
};
FMOD_RESULT _stdcall SoundEndCallback(FMOD_CHANNELCONTROL *channelcontrol, FMOD_CHANNELCONTROL_TYPE controltype, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbacktype, void *commanddata1, void *commanddata2)
{
	if (callbacktype == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		return FMOD_ERR_FILE_EOF;
	}
	return FMOD_OK;
}


class AudioClipExportTask : public AssetExportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;

	static void closeFMODSystem(FMOD::System* pSystem)
	{
		pSystem->release();
	}
	std::unique_ptr<FMOD::System, decltype(closeFMODSystem)*> pFMODSystem;
public:
	AudioClipExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export AudioClip data", "", std::move(_baseDir), stopOnError),
		appContext(appContext),
		pFMODSystem(nullptr, &closeFMODSystem)
	{
		FMOD::System* pFMODSystem = NULL;
		//createFunc((FMOD_SYSTEM**)&pFMODSystem)
		if (FMOD::System_Create(&pFMODSystem) == FMOD_OK)
		{
			this->pFMODSystem.reset(pFMODSystem);
			pFMODSystem->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT);
			pFMODSystem->init(16, FMOD_INIT_NORMAL, NULL);
		}
	}

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
				for (size_t i = 0; i < templateBase.children.size(); i++)
				{
					if (templateBase.children[i].children.size() > 0 && templateBase.children[i].name == "m_AudioData")
					{
						templateBase.children[i].children[0].type = "TypelessData"; //Improve deserialization performance for U4 audio clips
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

		
		AssetTypeValueField* formatField = pBaseField->Get("m_Format");
		AssetTypeValueField* typeField = pBaseField->Get("m_Type");
		AssetTypeValueField* nameField = pBaseField->Get("m_Name");
		AssetTypeValueField* streamField = pBaseField->Get("m_Stream");
		AssetTypeValueField* dataField = pBaseField->Get("m_AudioData")->Get(0U);

		if (!nameField->IsDummy() && !formatField->IsDummy() && !nameField->IsDummy() && !dataField->IsDummy()) //Unity 3/4
		{
			int streamType = 1;
			if (!streamField->IsDummy())
				streamType = streamField->GetValue()->AsInt();
			std::shared_ptr<IAssetsReader> pStreamReader = nullptr;
			QWORD streamSize = 0;
			if (streamType == 2 && !desc.assetsFileName.empty())
			{
				//TODO: Make sure this actually works (is based on legacy plugin code, API has been revamped in the meantime).
				//  It appears like the data Array size is set to the resource size, but the in-asset Array just contains the 4 bytes size?
				//  If so, this would most certainly cause trouble with deserialization.
				std::string streamDataFileName = desc.assetsFileName + ".resS";

				if (dataField->GetValue()->AsByteArray()->size < 4)
					throw AssetUtilError("The streamed data file is invalid.");

				QWORD streamFileOffset = *(uint32_t*)dataField->GetValue()->AsByteArray()->data;
				QWORD streamSize = dataField->GetValue()->AsByteArray()->size;

				std::shared_ptr<ResourcesFileContextInfo> streamResourcesContextInfo;
				streamResourcesContextInfo = FindResourcesFile(appContext, streamDataFileName, desc.asset, progressManager);
				//Non-null guaranteed by FindResourcesFile (AssetUtilError thrown otherwise).

				pStreamReader = streamResourcesContextInfo->getResource(streamResourcesContextInfo,
					streamFileOffset,
					streamSize);
				if (pStreamReader == nullptr)
					throw AssetUtilError("Unable to locate the audio resource.");
			}

			char* audioClipName = nameField->GetValue()->AsString();
			unsigned int audioClipFormat = (unsigned int)formatField->GetValue()->AsInt();
			unsigned int audioClipType = (unsigned int)typeField->GetValue()->AsInt();
			if (audioClipType >= AudioType_MAX)
				audioClipType = 0;
			const char* audioClipExtension = AudioTypeExtensions[AudioTypeIndexTable[audioClipType]];
			std::string fullOutputPath = path + audioClipExtension;

			std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(fullOutputPath.c_str(), true, true, RWOpenFlags_Immediately));
			if (pWriter == nullptr)
				throw AssetUtilError("Unable to create the output file.");
			if (pStreamReader)
			{
				std::unique_ptr<AssetsEntryReplacer> copier(MakeAssetModifierFromReader(0, 0, -1, 0xFFFF, pStreamReader.get(), streamSize));
				if (copier->Write(0, pWriter.get()) != streamSize)
					throw AssetUtilError("Unable to copy data from the audio resource to the output file.");
			}
			else
			{
				QWORD size = dataField->GetValue()->AsByteArray()->size;
				if (pWriter->Write(size, dataField->GetValue()->AsByteArray()->data) != size)
					throw AssetUtilError("Unable to write data to the output file.");
			}
			return true;
		}
		else //Unity 5 and newer
		{
			AssetTypeValueField* bitsPerSampleField = pBaseField->Get("m_BitsPerSample");
			AssetTypeValueField* frequencyField = pBaseField->Get("m_Frequency");
			AssetTypeValueField* channelsField = pBaseField->Get("m_Channels");
			AssetTypeValueField* resource = pBaseField->Get("m_Resource");
			if (bitsPerSampleField->IsDummy() || nameField->IsDummy() || resource->IsDummy()
				|| frequencyField->IsDummy() || channelsField->IsDummy())
			{
				throw AssetUtilError("Unexpected AudioClip asset format.");
			}
			AssetTypeValueField* sourceField = resource->Get("m_Source");
			AssetTypeValueField* offsetField = resource->Get("m_Offset");
			AssetTypeValueField* sizeField = resource->Get("m_Size");
			if ((sourceField->IsDummy() || offsetField->IsDummy() || sizeField->IsDummy())
				|| stricmp(sourceField->GetType().c_str(), "string")
				|| (stricmp(offsetField->GetType().c_str(), "UInt64") && stricmp(offsetField->GetType().c_str(), "FileSize"))
				|| stricmp(sizeField->GetType().c_str(), "UInt64")
				|| stricmp(frequencyField->GetType().c_str(), "int") || stricmp(channelsField->GetType().c_str(), "int"))
			{
				throw AssetUtilError("Unexpected AudioClip asset format.");
			}

			char* sourceFileName = sourceField->GetValue()->AsString();
			QWORD soundOffset = offsetField->GetValue()->AsUInt64();
			QWORD soundSize = sizeField->GetValue()->AsUInt64();
			if (soundSize > std::numeric_limits<size_t>::max())
				throw AssetUtilError("Resource size out of range.");

			std::shared_ptr<ResourcesFileContextInfo> streamResourcesContextInfo;
			streamResourcesContextInfo = FindResourcesFile(appContext, sourceFileName, desc.asset, progressManager);
			//Non-null guaranteed by FindResourcesFile (AssetUtilError thrown otherwise).

			std::shared_ptr<IAssetsReader> pStreamReader = streamResourcesContextInfo->getResource(streamResourcesContextInfo,
				soundOffset,
				soundSize);
			if (pStreamReader == nullptr)
				throw AssetUtilError("Unable to locate the audio resource.");

			//May consider alternatives to the proprietary FMOD API, e.g. vgmstream's parser
			//-> https://github.com/vgmstream/vgmstream/blob/master/src/meta/fsb5.c

			std::unique_ptr<FMOD::System, decltype(closeFMODSystem)*> _pFMODSystem_raii(nullptr, closeFMODSystem);

			FMOD::System* pFMODSystem = this->pFMODSystem.get();
			if (!pFMODSystem)
			{
				if (FMOD::System_Create(&pFMODSystem) != FMOD_OK)
					throw AssetUtilError("Unable to initialize FMOD.");
				_pFMODSystem_raii.reset(pFMODSystem);
				//pFMODSystem->setOutput(FMOD_OUTPUTTYPE_WAVWRITER_NRT);
				//pFMODSystem->init(16, FMOD_INIT_NORMAL, cOutFilePath);
				pFMODSystem->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT);
				pFMODSystem->init(16, FMOD_INIT_NORMAL, NULL);
			}
			void* rawBuffer = NULL; unsigned int rawBufferLen = 0;
			std::vector<uint8_t> soundBuffer(soundSize);
			if (pStreamReader->Read(0, (QWORD)soundSize, soundBuffer.data()) != soundSize)
				throw AssetUtilError("Unable to read data from the audio resource.");

			FMOD::Sound* pSound = NULL;
			FMOD_CREATESOUNDEXINFO soundLenInfo;
			memset(&soundLenInfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
			soundLenInfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
			soundLenInfo.length = (unsigned int)soundSize;
			soundLenInfo.format = (FMOD_SOUND_FORMAT)0;
			soundLenInfo.suggestedsoundtype = (FMOD_SOUND_TYPE)8;
			if (pFMODSystem->createSound((char*)soundBuffer.data(), FMOD_OPENMEMORY, &soundLenInfo, &pSound) == FMOD_OK)
			{
				auto closeFMODSound = [](FMOD::Sound* pSound)
				{
					pSound->release();
				};
				std::unique_ptr<FMOD::Sound, decltype(closeFMODSound)> _pSound_raii(pSound, closeFMODSound);

				pSound->setMode(FMOD_LOOP_OFF);
				pSound->setLoopCount(-1);
				FMOD::Sound* pSubSound = NULL;
				if (pSound->getSubSound(0, &pSubSound) != FMOD_OK)
					throw AssetUtilError("Unable to get the sub sound.");

				pSubSound->setMode(FMOD_LOOP_OFF);
				pSubSound->setLoopCount(-1);
				//uint32_t bitsPerSample = (uint32_t)bitsPerSampleField->GetValue()->AsInt();
				//FMOD_SOUND_FORMAT rawFormat;
				//switch (bitsPerSample)
				//{
				//... rawFormat = ...
				//}
				unsigned int sampleByteCount = 0;
				pSubSound->getLength(&sampleByteCount, FMOD_TIMEUNIT_PCMBYTES);

				std::string fullOutputPath = path + ".wav";
				std::unique_ptr<IAssetsWriter> pFileWriter(Create_AssetsWriterToFile(fullOutputPath.c_str(), true, true, RWOpenFlags_Immediately));
				if (pFileWriter == nullptr)
					throw AssetUtilError("Unable to open the output file.");
				if (!wavfile_open(pFileWriter.get(), WAVFILE_SOUND_FORMAT::PCM_16bit,
					(uint32_t)frequencyField->GetValue()->AsInt(),
					(uint32_t)channelsField->GetValue()->AsInt()))
					throw AssetUtilError("Unable to open the output file.");

				void* pData = NULL; unsigned int dataLen = 0;
				void* pData2; unsigned int dataLen2;
				pSubSound->lock(0, sampleByteCount, &pData, &pData2, &dataLen, &dataLen2);
				if (pData && dataLen)
				{
					wavfile_write(pFileWriter.get(), pData, dataLen);
				}
				pSubSound->unlock(pData, pData2, dataLen, dataLen2);
				wavfile_close(pFileWriter.get());
			}
			else if ((soundSize > 12) && !memcmp(&soundBuffer.data()[4], "ftypmp42", 8))
			{
				std::string fullOutputPath = path + ".m4a";
				std::unique_ptr<IAssetsWriter> pFileWriter(Create_AssetsWriterToFile(fullOutputPath.c_str(), true, true, RWOpenFlags_Immediately));
				if (pFileWriter == nullptr)
					throw AssetUtilError("Unable to open the output file.");
				if (pFileWriter->Write(soundSize, soundBuffer.data()) != soundSize)
					throw AssetUtilError("Unable to write data to the output file.");
			}
			else
			{
				throw AssetUtilError("Unable to recognize the audio data format.");
			}
			return true;
		}
		throw AssetUtilError("Unrecognized AudioClip asset format.");
	}
};

static bool SupportsElements(AppContext& appContext, std::vector<AssetUtilDesc>& elements)
{
	auto checkAudioClass = [&appContext](AssetsFileContextInfo* pFile, int32_t classID)
	{
		AssetTypeTemplateField templateBase;
		if (!pFile->MakeTemplateField(&templateBase, appContext, classID))
			return false;
		if (templateBase.SearchChild("m_Name")
			&& templateBase.SearchChild("m_Format")
			&& templateBase.SearchChild("m_Type")
			&& templateBase.SearchChild("m_Stream")
			&& templateBase.SearchChild("m_Type")
			&& templateBase.SearchChild("m_AudioData")
			&& templateBase.SearchChild("m_AudioData")->SearchChild("Array"))
			return true; //Unity 3/4
		if (templateBase.SearchChild("m_Name")
			&& templateBase.SearchChild("m_BitsPerSample")
			&& templateBase.SearchChild("m_Frequency")
			&& templateBase.SearchChild("m_Channels")
			&& templateBase.SearchChild("m_Resource"))
		{
			AssetTypeTemplateField& frequencyField = *templateBase.SearchChild("m_Frequency");
			AssetTypeTemplateField& channelsField = *templateBase.SearchChild("m_Channels");
			AssetTypeTemplateField& resourceField = *templateBase.SearchChild("m_Resource");
			AssetTypeTemplateField* pSourceField = resourceField.SearchChild("m_Source");
			AssetTypeTemplateField* pOffsetField = resourceField.SearchChild("m_Offset");
			AssetTypeTemplateField* pSizeField = resourceField.SearchChild("m_Size");
			if (pSourceField
				&& !stricmp(pSourceField->type.c_str(), "string")
				&& pOffsetField
				&& (!stricmp(pOffsetField->type.c_str(), "UInt64") || !stricmp(pOffsetField->type.c_str(), "FileSize"))
				&& pSizeField
				&& !stricmp(pSizeField->type.c_str(), "UInt64")
				&& !stricmp(frequencyField.type.c_str(), "int")
				&& !stricmp(channelsField.type.c_str(), "int"))
				return true; //Unity 5..2021+
		}
		return false;
	};

	std::unordered_map<AssetsFileContextInfo*, int32_t> audioClassIDs;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i].asset.pFile == nullptr)
			return false;
		AssetsFileContextInfo* pFile = elements[i].asset.pFile.get();
		auto classIDsit = audioClassIDs.find(pFile);
		int32_t audioClipClassID = -1;
		if (classIDsit == audioClassIDs.end())
		{
			audioClipClassID = pFile->GetClassByName("AudioClip");
			audioClassIDs[pFile] = audioClipClassID;
			if (!checkAudioClass(pFile, audioClipClassID))
				return false;
		}
		else
			audioClipClassID = classIDsit->second;
		if (audioClipClassID == -1)
			return false;
		int32_t classId = elements[i].asset.getClassID();
		if (classId != audioClipClassID)
			return false;
	}
	return true;
}
class AudioClipExportProvider : public IAssetOptionProviderGeneric
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
			std::string exportLocation = appContext.QueryAssetExportLocation(selection, "", "*|Varying audio format:");
			if (!exportLocation.empty())
			{
				auto pTask = std::make_shared<AudioClipExportTask>(appContext, std::move(selection), std::move(exportLocation));
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
		if (!SupportsElements(appContext, selection))
			return nullptr;
		optionName = "Export audio";
		return std::make_unique<Runner>(appContext, std::move(selection));
	}
};

class AudioClipPluginDesc : public IPluginDesc
{
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	AudioClipPluginDesc()
	{
		pProviders = { std::make_shared<AudioClipExportProvider>() };
	}
	std::string getName()
	{
		return "AudioClip";
	}
	std::string getAuthor()
	{
		return "";
	}
	std::string getDescriptionText()
	{
		return "Export AudioClip assets.";
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
	return new AudioClipPluginDesc();
}
