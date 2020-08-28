#pragma once
#include <string>
#include "..\AssetsTools\AssetsFileFormat.h"
#include "..\AssetsTools\AssetBundleFileFormat.h"
#include "..\AssetsTools\AssetsFileReader.h"
#include "..\AssetsTools\AssetsReplacer.h"
#include "..\AssetsTools\AssetTypeClass.h"

class IAssetInterface
{
public:
	//Gets the class id of the asset
	virtual int GetClassID()=0;
	//Gets the index of the asset class for the current file (only if the class id is < 0); for format >= 0x0D
	virtual unsigned short GetMonoClassID()=0;

	//Returns the path id of the asset
	virtual __int64 GetPathID()=0;
	//Returns the absolute file id of the asset
	virtual unsigned int GetFileID()=0;
	//Returns the absolute file id of a referenced file id from an asset (usually in PPtrs)
	virtual unsigned int GetAbsoluteFileID(unsigned int referenceFileID)=0;
	
	//Returns a UTF-8 string
	virtual const char *GetAssetName()=0;

	//Returns a pointer to the current AssetsFile instance
	virtual AssetsFile *GetAssetsFile()=0;
	
	//Returns a UTF-8 file path string
	virtual const char *GetAssetsFileBasePath()=0;

	//Returns a UTF-8 string
	virtual const char *GetAssetsFileName()=0;

	//Gets the file reader and reader param and returns the file size.
	virtual unsigned __int64 GetFileReader(IAssetsReader *&pReader, unsigned __int64 &filePos)=0;
	
	//Frees a file reader returned by GetFileReader; Only use this BEFORE calling AddReplacer
	virtual void FreeFileReader(IAssetsReader *pReader)=0;
};
class IProgressIndicator
{
public:
	//Cancel status callback interface.
	class ICancelCallback
	{
		public:
		//Called when the progress indicator's cancel status changes. There is no guarantee which thread this is called from.
		virtual void OnCancelEvent(bool cancel) = 0;
	};
	
	//Instructs the progress indicator to stay open or not to stay open (default) with an OK button instead of Cancel if something was written to the log.
	virtual void SetDontCloseIfLog(bool dontclose = true) = 0;

	//Adds a new step and returns its index. The step with index 0 already exists before calling AddStep the first time.
	virtual size_t AddStep(unsigned int range = 0) = 0;
	//Sets the range of a step, which can represent the smallest unit by which the progress of this step can advance.
	virtual bool SetStepRange(size_t idx, unsigned int range) = 0;
	//Sets the progress of the current step, which should be a value from 0 to (including) range.
	virtual bool SetStepStatus(unsigned int progress) = 0;
	
	//Jumps to a step and sets its progress.
	virtual bool JumpToStep(size_t idx, unsigned int progress = 0) = 0;
	//Goes to the next step, setting its progress to 0. Returns (size_t)-1 on failure, the new idx otherwise.
	virtual size_t GoToNextStep() = 0;

	//Sets the progress indicator's window title.
	virtual bool SetTitle(std::string &title) = 0;
	virtual bool SetTitle(std::wstring &title) = 0;
	//Sets the description of the progress indicator, usually referring to the current step.
	virtual bool SetDescription(std::string &desc) = 0;
	virtual bool SetDescription(std::wstring &desc) = 0;

	//Adds text to the log.
	virtual bool AddLogText(std::string &text) = 0;
	virtual bool AddLogText(std::wstring &text) = 0;
	//Adds a line of text to the log.
	inline bool AddLogLine(std::string &line) {return AddLogText(line + "\r\n");}
	inline bool AddLogLine(std::wstring &line) {return AddLogText(line + L"\r\n");}
	
	//Enables or disables the cancel button.
	virtual bool SetCancellable(bool cancellable) = 0;

	//Adds a cancel callback. Called by the window handler from another thread, or by SetCancelled.
	//pCallback must not be freed before destroying the progress indicator.
	virtual bool AddCancelCallback(ICancelCallback *pCallback) = 0;
	//Retrieves the current cancel status.
	virtual bool IsCancelled() = 0;
	//Sets the current cancel status.
	virtual bool SetCancelled(bool cancelled) = 0;
};
//Class implemented by plugins that use the IPluginInterface::ShowBatchImportDialog function. 
//All const char* and std strings are UTF-8. const char* strings returned by the plugin must not be freed before the dialog has closed.
class IBatchImportDialogDesc
{
public:
	class AssetDesc
	{
	public:
		const char *description;
		const char *assetsFileName;
		long long int pathID;
	};
	//Returns a list of asset descriptions to show to the user. The indices into this list will be used for matchIndex.
	virtual bool GetImportableAssetDescs(OUT std::vector<AssetDesc> &descList)=0;
		
	//Returns one or multiple regex string(s) that match(es) any potentially importable file in a directory, including those not to be imported.
	//The batch import dialog implementation uses std::regex (ECMAScript) and matches the full name. See https://www.regular-expressions.info/stdregex.html
	virtual bool GetFilenameMatchStrings(OUT std::vector<const char*> &regexList, OUT bool &checkSubDirs)=0;
		
	//The return value specifies whether the file name matches any of the assets to import. If a match is found, its index is returned through matchIndex.
	//capturingGroups contains the contents of the regex capturing groups matched in filename.
	virtual bool GetFilenameMatchInfo(IN const char *filename, IN std::vector<const char*> &capturingGroups, OUT size_t &matchIndex)=0;

	//Sets the full file path for an asset. filepath is NULL for assets where no matching file was found.
	virtual void SetInputFilepath(IN size_t matchIndex, IN const char *filepath)=0; 

	//Shows a settings dialog to specify precise settings of a single asset. Returns whether such a dialog is supported if matchIndex==(size_t)-1.
	virtual bool ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow)=0;

	//Retrieves a potential file name override, returning true only if an override exists. Called by the dialog handler after ShowAssetSettings.
	virtual bool HasFilenameOverride(IN size_t matchIndex, OUT std::string &filenameOverride, OUT bool &relativeToBasePath)=0;
};
class IPluginInterface
{
public:
	//Returns a pointer to the AssetsFile instance by an absolute file id (or NULL if it doesn't exist)
	virtual AssetsFile *GetAssetsFileByFileID(unsigned int fileID)=0;

	//Returns a pointer to the AssetBundleFile by its name (usually CAB_<16byte hash>) (or NULL if it doesn't exist)
	virtual AssetBundleFile *GetBundleFileByName(const char *bundleName, size_t strLen = 0)=0;
	//Returns a pointer to the current bundle file
	virtual AssetBundleFile *GetBundleFile()=0;
	virtual IAssetsReader *GetBundleFileReader()=0;

	//Open an asset interface.
	virtual bool OpenAsset(unsigned int fileID, __int64 pathID, IAssetInterface *&pAsset)=0;
	//Closes an asset interface opened through OpenAsset.
	virtual void CloseAsset(IAssetInterface *pAsset)=0;
	
	//Open a streamed data file (a .resources or .resS file that contains audio and/or texture data or possibly more in later Unity versions).
	virtual bool OpenStreamedData(const char *name, IAssetsReader *&pReader)=0;
	virtual void CloseStreamedData(IAssetsReader *pReader)=0;

	//Adds an AssetsReplacer. It needs to be created with one of the functions in AssetsReplacer.h. The application frees it.
	//If it replaces an asset referred to by the asset interface/s passed to the plugin, 
	//the asset interface's reader can't longer be relied on as it might be freed.
	virtual void AddReplacer(AssetsReplacer *pReplacer)=0;
	
	//Tries to find a ClassDatabaseType from extracted MonoBehaviour type information for the specified asset.
	virtual bool FindScriptClassDatabaseEntry(ClassDatabaseFile *&pClassFile, ClassDatabaseType *&pClassType, AssetsFile *pAssetsFile, IAssetInterface *pAsset, Hash128 *pScriptID = NULL)=0;

	//Makes a template type of the classID using type information from the assets file or from one of the class databases, additionally using extracted MonoBehaviour formats.
	virtual bool MakeTemplateField(AssetsFile *pAssetsFile, int classID, AssetTypeTemplateField *pTemplateBase, WORD scriptIndex = 0xFFFF, IAssetInterface *pAsset = NULL)=0;

	//returns the id of a class by its name
	virtual int GetClassByName(AssetsFile *pAssetsFile, const char *name)=0;

	//Get the type name from a class id as UTF-8 or UTF-16 from type information or one of the class databases;
	//if none is found, returns the hexadecimal string of classID prepended with 0x.
	virtual void GetTypenameA(AssetsFile *pAssetsFile, int classID, char *nameBuffer, unsigned int bufferLen, IAssetInterface *pAsset = NULL)=0;
	virtual void GetTypenameW(AssetsFile *pAssetsFile, int classID, wchar_t *nameBuffer, unsigned int bufferLen, IAssetInterface *pAsset = NULL)=0;
	
	//Shows a file open dialog and optionally writes the index of the selected type filter to *pSelFilterIndex.
	virtual HRESULT ShowFileOpenDialog(HWND hOwner, WCHAR **filePathBuf, const wchar_t *fileTypeFilter, unsigned int *pSelFilterIndex = NULL, const wchar_t *defaultFile = NULL, const wchar_t *windowTitle = NULL)=0;
	//Shows a file save dialog and optionally writes the index of the selected type filter to *pSelFilterIndex.
	virtual HRESULT ShowFileSaveDialog(HWND hOwner, WCHAR **filePathBuf, const wchar_t *fileTypeFilter, unsigned int *pSelFilterIndex = NULL, LPCTSTR defaultFile = NULL, LPCTSTR windowTitle = NULL)=0;
	//Shows a file folder select dialog.
	virtual BOOL ShowFolderSelectDialog(HWND hOwner, WCHAR **folderPathBuf, LPCWSTR windowTitle = NULL)=0;
	//Frees a file dialog string (generated by the UTF-16 versions only).
	virtual void FreeCOMFilePathBuf(WCHAR **filePathBuf)=0;
	
	//Uses ShowFileOpenDialog and converts the input parameters and the output filePathBuf to UTF-8.
	virtual HRESULT ShowFileOpenDialogA(HWND hOwner, char **filePathBuf, const char *fileTypeFilter, unsigned int *pSelFilterIndex = NULL, const char *defaultFile = NULL, const char *windowTitle = NULL)=0;
	//Uses ShowFileSaveDialog and converts the input parameters and the output filePathBuf to UTF-8.
	virtual HRESULT ShowFileSaveDialogA(HWND hOwner, char **filePathBuf, const char *fileTypeFilter, unsigned int *pSelFilterIndex = NULL, const char *defaultFile = NULL, const char *windowTitle = NULL)=0;
	//Uses ShowFolderSelectDialog and converts the input windowTitle and the output folderPathBuf to UTF-8.
	virtual BOOL ShowFolderSelectDialogA(HWND hOwner, char **folderPathBuf, size_t *folderPathLen, const char *windowTitle = NULL)=0;
	//Frees a UTF-8 file dialog string (generated by the UTF-8 versions only).
	virtual void FreeUTF8DialogBuf(CHAR **filePathBuf)=0;

	//Shows a batch import dialog that lists the assets, the according files to import and that optionally allows the user to modify properties of each asset.
	//Returns false if the user pressed cancel, or if an error occured trying to open the dialog.
	virtual bool ShowBatchImportDialog(HWND hOwner, IBatchImportDialogDesc *pDesc, const char *basePath)=0;

	//Creates a filename for batch export (i.e. assetName "test" and extension ".jpg" -> "test.jpg");
	//also checks for duplicates and adds numbers if necessary; if assetName is null or empty, uses <fileId>_<pathId> instead
	//May return NULL when out of memory after showing a MessageBox
	virtual char *MakeExportFileName(const char *outFolder, const char *assetName, const char *assetsFileName, __int64 pathId, char **previousFileNames, size_t index, size_t count, const char *extension, bool usePathId = true)=0;
	//Like MakeExportFileName but converts the returned string to UTF-16
	virtual WCHAR *MakeExportFileNameW(const char *outFolder, const char *assetName, const char *assetsFileName, __int64 pathId, char **previousFileNames, size_t index, size_t count, const char *extension, bool usePathId = true)=0;
	//Retrieves a regular expression compatible with std::regex (ECMAScript) that matches file names generated with MakeExportFileName,
	//called with a non-empty assetsFileName and usePathId == true.
	//extension is appended to the returned regex so it may be a regex as well. Extension should fully match the extension passed to MakeExportFileName, including the ".".
	virtual char *MakeImportFileNameRegex(const char *extension)=0;
	//Retrieves the contents of capture groups returned by a regex match with MakeImportFileNameRegex. Passes an existing const char* from capturingGroups to assetsFileName.
	virtual bool RetrieveImportRegexInfo(IN std::vector<const char*> &capturingGroups, OUT const char *&assetsFileName, OUT __int64 &pathId)=0;
	//Frees strings returned by MakeExportFileName, MakeExportFileNameW and MakeImportFileNameRegex.
	virtual void MemFree(void *buf)=0;

	//Creates a progress indicator with an optional parent window that will be blocked and a delay in milliseconds after which the window is actually shown.
	virtual IProgressIndicator *ShowProgressIndicator(HWND hParentWindow = NULL, unsigned int delay = 100)=0;
	//Frees a progress indicator created by ShowProgressIndicator.
	virtual void CloseProgressIndicator(IProgressIndicator *pProgressIndicator)=0;
};

enum ActionElementType
{
	ActionElement_ImportDescriptor, //The IElementDescriptor is a IImportDescriptor
	ActionElement_ExportDescriptor, //The IElementDescriptor is a IExportDescriptor
};
class IElementDescriptor
{
public:
	//Returns the type of the descriptor.
	virtual ActionElementType GetType()=0;
};
class IImportDescriptor : public IElementDescriptor
{
public:
	//Get-functions for both the support and the action callbacks.
	
	//Returns the IAssetInterface describing the asset that will be modified or created. Can return a null pointer.
	virtual IAssetInterface *GetTargetAssetInterface()=0;
	//Returns a reader/param pair for the source file to import (e.g. a .tga file for the texture plugin).
	virtual bool GetSourceFileReader(IAssetsReader *&pReader)=0;
	//Returns a UTF-8 source file name, excluding the base directory path. 
	virtual bool GetSourceFileName(const char *&name)=0;


	//Set-functions for the support callback.
	
	//Sets the attributes to create an IAssetInterface. GetTargetAssetInterface() will return NULL after this during the support callback.
	virtual bool SetTargetAsset(int fileID, __int64 pathID, int classID, unsigned short monoClassID)=0;
	//Sets the UTF-8 source file name, excluding the base directory path. The buffer used for fileName can be freed after this function returns.
	virtual void SetSourceFileName(const char *fileName)=0;
};
class IExportDescriptor : public IElementDescriptor
{
public:
	//Get-functions for both the support and the action callbacks.
	
	//Returns the IAssetInterface describing the asset that will be exported.
	virtual IAssetInterface *GetSourceAssetInterface()=0;
	//Returns a writer/param pair for the output file (e.g. a .tga file for the texture plugin).
	virtual bool GetTargetFileWriter(IAssetsWriter *&pWriter)=0;
	//Returns a UTF-8 target file name. If SetTargetFileName wasn't called before, returns the default name.
	virtual bool GetTargetFileName(const char *&name)=0;


	//Set-functions for the support callback. Don't use them in the action callback.

	//Sets the attributes to create an IAssetInterface. GetSourceAssetInterface() will return NULL after this during the support callback.
	virtual bool SetSourceAsset(int fileID, __int64 pathID, int classID, unsigned short monoClassID)=0;
	//Sets the UTF-8 target file name. The buffer used for fileName can be freed after this function returns.
	virtual void SetTargetFileName(const char *fileName)=0;
};

/*class IElementDescriptorList
{
public:
	virtual IElementDescriptor &operator[](size_t index)=0;
	virtual size_t size()=0;
};*/

enum PluginInfoVersion
{
	PluginInfoVersion1, //Old plugin callback type.
	PluginInfoVersion2, //New plugin callback type (API 2.2).
};

//The function type of the plugin action callback.
typedef void(_cdecl *AssetPluginActionCallback1)(HWND hParentWnd, IPluginInterface *pInterface, IAssetInterface **ppAssets, size_t count);
//The function type of the plugin's function that returns a PluginInfo1 or PluginInfo2 pointer. The function should be exported with a .def file as GetAssetBundlePluginInfo.
//If the plugin has a pre-2.2 callback without the version field and/or doesn't change the version value, it will default to PluginInfoVersion1.
typedef void*(_cdecl *AssetPlugin_GetPluginInfoCallback)(PluginInfoVersion &version);


//The function type of the plugin action callback (API 2.2).
typedef void(_cdecl *AssetPluginActionCallback2)(HWND hParentWnd, IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine);
//The function type of the plugin action support callback (addition to API 2.2). The plugin should set "desc" to the action's description.
typedef bool(_cdecl *AssetPluginSupportCallback2)(IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine, std::string &desc);

enum PluginAction
{
	PluginAction_IMPORT=1,
	PluginAction_EXPORT=2,
	PluginAction_Batch=4, //IMPORT|Batch Supported since API 2.2.
	PluginAction_CREATE=8, //Create empty assets using IImportDescriptors. Supported since API 2.2. 

	PluginAction_IMPORT_Batch=PluginAction_IMPORT | PluginAction_Batch,
	PluginAction_EXPORT_Batch=PluginAction_EXPORT | PluginAction_Batch,
	PluginAction_All=15,

	PluginAction_Console=0x10000, //This option is a console command and should not be displayed in UABE's GUI. Supported since API 2.2 but not used yet.
};


struct PluginAssetOption2
{
	PluginAction action; //Type of action.
	AssetPluginSupportCallback2 supportCallback; //Plugin function to determine whether this action supports a list of assets and to get a description text.
	AssetPluginActionCallback2 performCallback; //Plugin function to be called to perform the action.
};
//The plugin should return a PluginInfo2 pointer to UABE with the initial GetAssetBundlePluginInfo call.
//Should be written to a buffer with sizeof(PluginInfo2)+optionCount*sizeof(PluginAssetOption2) Bytes, the buffer should not be reused or freed after the GetAssetBundlePluginInfo call.
struct PluginInfo2
{
	char name[64]; //Name of the Plugin; not actually used at the moment.
	unsigned int optionCount; //Amount of plugin options.
	PluginAssetOption2 options[0]; //Variable-size array for the plugin options.
};

//DEPRECATED as of API 2.2. Not yet removed to simplify compilation of pre-2.2 plugins against the current API.
struct PluginAssetOption1
{
	char desc[256]; //Plugin option description as shown in the Plugins dialog.
	int unityClassID; //Class ID of the supported type.
	PluginAction action; //Type of action.
	AssetPluginActionCallback1 callback; //Plugin function to be called to perform the action.
};
//A PluginInfo1 pointer can be returned to UABE with the initial GetAssetBundlePluginInfo call.
//Should be written to a buffer with sizeof(PluginInfo1)+optionCount*sizeof(PluginAssetOption) Bytes, the buffer should not be reused or freed after the GetAssetBundlePluginInfo call.
struct PluginInfo1
{
	char name[64]; //Name of the Plugin; not actually used at the moment.
	unsigned int optionCount; //Amount of plugin options.
	PluginAssetOption1 options[0]; //Variable-size array for the plugin options.
};