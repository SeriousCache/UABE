#include "stdafx.h"
#include "MonoBehaviourManager.h"
#include "Win32AppContext.h"
#include "FileDialog.h"
#include "../AssetsTools/ClassDatabaseFile.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "../AssetsTools/EngineVersion.h"
#include "../libStringConverter/convert.h"
#include <vector>
#include "resource.h"
#include <WindowsX.h>

bool TryGetAssemblyFilePath(Win32AppContext &appContext, AssetsFileContextInfo &assetsFileInfo, const char *assemblyName, WCHAR *&path, bool allowUserDialog);
void ShowMonoBehaviourExportErrors(HINSTANCE hInstance, HWND hParent, std::vector<unsigned char> &errorBuffer);

static void AddAssemblyName(Win32AppContext &appContext, unsigned int assetsFileId, const char *scriptAssemblyName, std::vector<char*> &scannedAssemblies, std::vector<std::pair<EngineVersion,WCHAR*>> &assemblyNames)
{
	bool exists = false;
	for (size_t l = 0; l < scannedAssemblies.size(); l++)
	{
		if (!strcmp(scannedAssemblies[l], scriptAssemblyName))
		{
			exists = true;
			break;
		}
	}
	if (!exists)
	{
		size_t curNameLen = strlen(scriptAssemblyName);
		for (size_t i = 0; i < curNameLen; i++)
		{
			//Prevent format string injection.
			if (scriptAssemblyName[i] == ':' || scriptAssemblyName[i] == '|')
				return;
		}
		char *curScannedName = new char[curNameLen+1];
		memcpy(curScannedName, scriptAssemblyName, curNameLen+1);
		scannedAssemblies.push_back(curScannedName);
		WCHAR *pName;
		std::shared_ptr<AssetsFileContextInfo> pAssetsInfo = std::dynamic_pointer_cast<AssetsFileContextInfo>(appContext.getContextInfo(assetsFileId));
		if (pAssetsInfo && TryGetAssemblyFilePath(appContext, *pAssetsInfo, scriptAssemblyName, pName, true))
		{
			EngineVersion version;
			if (pAssetsInfo->getAssetsFileContext() && pAssetsInfo->getAssetsFileContext()->getAssetsFile())
				version = EngineVersion::parse(pAssetsInfo->getAssetsFileContext()->getAssetsFile()->typeTree.unityVersion);
			assemblyNames.push_back({ version, pName });
		}
	}
}

bool GetAllScriptInformation(Win32AppContext &appContext, std::vector<std::shared_ptr<AssetsFileContextInfo>> &assetsInfo)
{
	std::vector<AssetsFileContextInfo*> assetsFilesWithMonoScript;
	std::vector<std::pair<EngineVersion, WCHAR*>> assemblyNames;
	std::vector<char*> scannedAssemblies;
	bool useLongPathID = false;
	for (size_t i = 0; i < assetsInfo.size(); i++)
	{
		AssetsFileContextInfo *pAssetsInfo = assetsInfo[i].get();
		bool isBigEndian = false;
		if (!pAssetsInfo || !pAssetsInfo->getAssetsFileContext() || !pAssetsInfo->getAssetsFileContext()->getAssetsFile()
			|| !pAssetsInfo->getEndianness(isBigEndian))
			continue;
		
		useLongPathID = useLongPathID || (pAssetsInfo->getAssetsFileContext()->getAssetsFile()->header.format >= 0x0E);
		int scriptClassId = pAssetsInfo->GetClassByName("MonoScript");
		int managerClassId = pAssetsInfo->GetClassByName("MonoManager");

		AssetTypeTemplateField scriptBase;
		if (scriptClassId >= 0)
			pAssetsInfo->MakeTemplateField(&scriptBase, appContext, scriptClassId);
		AssetTypeTemplateField managerBase;
		if (managerClassId >= 0)
			pAssetsInfo->MakeTemplateField(&managerBase, appContext, managerClassId);


		if (scriptBase.children.size() > 0)
		{
			bool fileHasMonoScript = false;
			AssetIdentifier identifier;
			for (AssetIterator iter(pAssetsInfo); !iter.isEnd(); ++iter)
			{
				iter.get(identifier);
				if (identifier.resolve(appContext) && identifier.getClassID() == scriptClassId)
				{
					IAssetsReader_ptr pReader = identifier.makeReader();
					if (!pReader) continue;
					fileHasMonoScript = true;
					AssetTypeTemplateField *pTemplate = &scriptBase;
					AssetTypeInstance scriptInstance = AssetTypeInstance(1, &pTemplate,
						identifier.getDataSize(), pReader.get(), isBigEndian);
					
					AssetTypeValueField *pScriptBase = scriptInstance.GetBaseField();
					AssetTypeValueField *pScriptAssemblyNameField; const char *scriptAssemblyName;
					if ((pScriptBase != NULL) && 
						(pScriptAssemblyNameField = pScriptBase->Get("m_AssemblyName"))->GetValue()
							&& (scriptAssemblyName = pScriptAssemblyNameField->GetValue()->AsString()))
					{
						AddAssemblyName(appContext, pAssetsInfo->getFileID(), scriptAssemblyName, scannedAssemblies, assemblyNames);
					}
				}
			}
			if (fileHasMonoScript)
				assetsFilesWithMonoScript.push_back(pAssetsInfo);
		}
		if (managerBase.children.size() > 0)
		{
			AssetIdentifier identifier;
			for (AssetIterator iter(pAssetsInfo); !iter.isEnd(); ++iter)
			{
				iter.get(identifier);
				if (identifier.resolve(appContext) && identifier.getClassID() == managerClassId)
				{
					IAssetsReader_ptr pReader = identifier.makeReader();
					if (!pReader) continue;
					AssetTypeTemplateField *pTemplate = &managerBase;
					AssetTypeInstance managerInstance = AssetTypeInstance(1, &pTemplate,
						identifier.getDataSize(), pReader.get(), isBigEndian);
					
					AssetTypeValueField *pScriptBase = managerInstance.GetBaseField();
					AssetTypeValueField *pAssemblyNamesField;
					if ((pScriptBase != NULL) && 
						(pAssemblyNamesField = pScriptBase->Get("m_AssemblyNames")->Get("Array"))->GetValue() &&
						pAssemblyNamesField->GetValue()->AsArray())
					{
						for (unsigned int l = 0; l < pAssemblyNamesField->GetChildrenCount(); l++)
						{
							AssetTypeValue *pNameValue = pAssemblyNamesField->Get(l)->GetValue();
							char *scriptAssemblyName;
							if (pNameValue && (scriptAssemblyName = pNameValue->AsString()))
							{
								AddAssemblyName(appContext, pAssetsInfo->getFileID(), scriptAssemblyName, scannedAssemblies, assemblyNames);
							}
						}
					}
				}
			}
		}
	}
	for (size_t i = 0; i < scannedAssemblies.size(); i++)
	{
		delete[] scannedAssemblies[i];
	}

	if (assemblyNames.size() > 0)
	{
		std::shared_ptr<ClassDatabaseFile> pClassDb = CreateMonoBehaviourClassDb(appContext, assemblyNames, useLongPathID, true);
		for (size_t i = 0; i < assemblyNames.size(); i++)
		{
			delete[] assemblyNames[i].second;
		}
		if (pClassDb != nullptr)
		{
			if (assetsFilesWithMonoScript.empty())
			{
				for (size_t i = 0; i < assetsInfo.size(); ++i)
					assetsInfo[i]->appendScriptDatabase(pClassDb);
			}
			else
			{
				for (size_t i = 0; i < assetsFilesWithMonoScript.size(); ++i)
					assetsFilesWithMonoScript[i]->appendScriptDatabase(pClassDb);
			}
			return true;
		}
	}
	else
		MessageBox(appContext.getMainWindow().getWindow(), TEXT("Unable to find any script assemblies!"), TEXT("Error"), 0);
	return false;
}

bool TryGetAssemblyFilePath(Win32AppContext &appContext, AssetsFileContextInfo &assetsFileInfo, const char *assemblyName, WCHAR *&path, bool allowUserDialog)
{
	if (!assetsFileInfo.getFileContext()
		|| !assetsFileInfo.getAssetsFileContext()
		|| !assetsFileInfo.getAssetsFileContext()->getAssetsFile())
		return false;

	if (!assemblyName)
		return false;
	size_t wcAssemblyNameLen;
	auto wcAssemblyName = unique_MultiByteToWide(assemblyName, wcAssemblyNameLen);
	if (!wcAssemblyName)
		return false;

	std::wstring assemblyPath = std::wstring();
	std::string assetsBaseFolder = assetsFileInfo.getAssetsFileContext()->getFileDirectoryPath();
	if (!assetsBaseFolder.empty())
	{
		size_t wcBaseFolderLen;
		auto wcBaseFolder = unique_MultiByteToWide(assetsBaseFolder.c_str(), wcBaseFolderLen);
		if (wcBaseFolder)
		{
			assemblyPath = std::wstring(wcBaseFolder.get());
			if (assemblyPath.size() > 10 && !assemblyPath.compare(assemblyPath.size() - 10, std::string::npos, L"\\Resources"))
				assemblyPath += L"\\..";
			assemblyPath += L"\\Managed\\";
			assemblyPath += wcAssemblyName.get();
		}
	}
	bool foundFile = false;
	if (assemblyPath.size() > 0)
	{
		IAssetsReader *pTempReader = Create_AssetsReaderFromFile(assemblyPath.c_str(), true, RWOpenFlags_Immediately);
		if (pTempReader)
		{
			Free_AssetsReader(pTempReader);
			foundFile = true;
		}
	}
	if (!foundFile)
	{
		if (allowUserDialog)
		{
			std::vector<wchar_t> wcAssemblyNameEscaped; wcAssemblyNameEscaped.reserve(wcAssemblyNameLen);
			for (size_t i = 0; i < wcAssemblyNameLen; ++i)
			{
				if (wcAssemblyName[i] == L'|' || wcAssemblyName[i] == L':' || wcAssemblyName[i] == L'*' || wcAssemblyName[i] == 0)
				{
					wcAssemblyNameEscaped.clear();
					wcAssemblyNameEscaped.push_back(L'*');
				}
				else
					wcAssemblyNameEscaped.push_back(wcAssemblyName[i]);
			}
			WCHAR *filePathBuf = NULL;
			HRESULT result = ShowFileOpenDialog(
				appContext.getMainWindow().getWindow(),
				&filePathBuf, 
				( std::wstring(wcAssemblyNameEscaped.begin(), wcAssemblyNameEscaped.end()) + L"|Assembly file:" ).c_str(),
				NULL,
				wcAssemblyName.get(),
				L"Open the Assembly file",
				UABE_FILEDIALOG_FILE_GUID);
			if (SUCCEEDED(result))
			{
				assemblyPath = std::wstring(filePathBuf);
				FreeCOMFilePathBuf(&filePathBuf);
				foundFile = true;
			}
		}
	}
	
	path = new WCHAR[assemblyPath.size() + 1];
	memcpy(path, assemblyPath.c_str(), (assemblyPath.size() + 1) * sizeof(WCHAR));

	return foundFile;
}

std::shared_ptr<ClassDatabaseFile> CreateMonoBehaviourClassDb(Win32AppContext &appContext, 
	std::vector<std::pair<EngineVersion, WCHAR*>> &assemblyFullNames,
	bool useLongPathID, bool allowUserDialog)
{
	HANDLE stdoutReadPipe = INVALID_HANDLE_VALUE; HANDLE stdoutWritePipe = INVALID_HANDLE_VALUE;
	HANDLE stderrReadPipe = INVALID_HANDLE_VALUE; HANDLE stderrWritePipe = INVALID_HANDLE_VALUE;
	HANDLE stdinReadPipe = INVALID_HANDLE_VALUE; HANDLE stdinWritePipe = INVALID_HANDLE_VALUE;
	SECURITY_ATTRIBUTES secAttributes = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
	if (!CreatePipe(&stdoutReadPipe, &stdoutWritePipe, &secAttributes, 0) || 
		!CreatePipe(&stderrReadPipe, &stderrWritePipe, &secAttributes, 0) || 
		!CreatePipe(&stdinReadPipe, &stdinWritePipe, &secAttributes, 0))
	{
		if (stdinReadPipe != INVALID_HANDLE_VALUE)
		{
			CloseHandle(stdinReadPipe);
			CloseHandle(stdinWritePipe);
		}
		if (stderrReadPipe != INVALID_HANDLE_VALUE)
		{
			CloseHandle(stderrReadPipe);
			CloseHandle(stderrWritePipe);
		}
		if (allowUserDialog)
			MessageBox(appContext.getMainWindow().getWindow(), TEXT("Unable to create stdout/stderr pipes for the child process!"), TEXT("Asset Bundle Extractor"), 16);
		return std::shared_ptr<ClassDatabaseFile>();
	}
	SetHandleInformation(stdoutReadPipe, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(stderrReadPipe, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(stdinWritePipe, HANDLE_FLAG_INHERIT, 0);
	
	std::wstring typeTreeGeneratorApp;
	{
		std::string baseDir = appContext.getBaseDir(); size_t baseDirWLen = 0;
		auto baseDirW = unique_MultiByteToWide(baseDir.c_str(), baseDirWLen);

		typeTreeGeneratorApp = std::wstring(baseDirW.get(), baseDirW.get() + baseDirWLen) + L"\\Tools\\TypeTreeGenerator.exe";
	}

	std::vector<wchar_t> commandLine;
	{
		std::wstring _commandLine = std::wstring(L"TypeTreeGenerator -stdout -stdin -longpathid ") + (useLongPathID ? L"1" : L"0");
		/*for (size_t i = 0; i < assemblyFullNames.size(); i++)
		{
			_commandLine = _commandLine + L" -f \"" + assemblyFullNames[i] + L"\"";
		}*/
		commandLine = std::vector<wchar_t>();
		commandLine.resize(_commandLine.size() + 1); //include null-terminator
		memcpy(commandLine.data(), _commandLine.c_str(), (_commandLine.size() + 1) * sizeof(wchar_t));
	}
	STARTUPINFO startInfo = {}; PROCESS_INFORMATION procInfo = {};
	startInfo.cb = sizeof(STARTUPINFO);
	startInfo.dwFlags = STARTF_USESTDHANDLES;
	startInfo.hStdError = stderrWritePipe;
	startInfo.hStdOutput = stdoutWritePipe;
	startInfo.hStdInput = stdinReadPipe;

	std::vector<unsigned char> dataBuffer;
	std::vector<unsigned char> errorBuffer;
	if (CreateProcess(typeTreeGeneratorApp.c_str(), commandLine.data(), NULL, NULL, TRUE, 0/*CREATE_NO_WINDOW*/, NULL, NULL, &startInfo, &procInfo))
	{
		CloseHandle(procInfo.hThread);
		std::wstring commands = std::wstring();
		for (size_t i = 0; i < assemblyFullNames.size(); i++)
		{
			commands += L"-ver";
			commands.push_back(0);
			commands += std::to_wstring(assemblyFullNames[i].first.year);
			commands.push_back(0);
			commands += std::to_wstring(assemblyFullNames[i].first.release);
			commands.push_back(0);
			commands += L"-f";
			commands.push_back(0);
			commands += assemblyFullNames[i].second;
			commands.push_back(0);
		}
		if (commands.size() == 0)
			commands.push_back(0);
		commands.push_back(0);
		DWORD written = 0;
		WriteFile(stdinWritePipe, commands.data(), (DWORD)(commands.size() * sizeof(wchar_t)), &written, NULL);
		bool repeatOnce = false;
		while (true)
		{
			bool repeatOnce = false;
			if (WaitForSingleObject(procInfo.hProcess, 100) != WAIT_TIMEOUT)
			{
				repeatOnce = true;
			}
			DWORD avail = 0;
			if (PeekNamedPipe(stdoutReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0)
			{
				size_t targetIndex = dataBuffer.size();
				dataBuffer.resize(dataBuffer.size() + avail);
				DWORD actuallyRead = 0;
				ReadFile(stdoutReadPipe, &dataBuffer[targetIndex], avail, &actuallyRead, NULL);
				dataBuffer.resize(targetIndex + actuallyRead);
			}
			avail = 0;
			if (PeekNamedPipe(stderrReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0)
			{
				size_t targetIndex = errorBuffer.size();
				errorBuffer.resize(errorBuffer.size() + avail);
				DWORD actuallyRead = 0;
				ReadFile(stderrReadPipe, &errorBuffer[targetIndex], avail, &actuallyRead, NULL);
				errorBuffer.resize(targetIndex + actuallyRead);
			}
			if (repeatOnce)
				break;
		}
		TerminateProcess(procInfo.hProcess, 0); //Make sure it's really closed.
		CloseHandle(procInfo.hProcess);
	}
	else
	{
		if (allowUserDialog)
		{
			char errorCodeBuf[128];
			sprintf_s(errorCodeBuf, "Unable to open Tools\\TypeTreeGenerator.exe (error %d)!", GetLastError());
			MessageBoxA(appContext.getMainWindow().getWindow(), errorCodeBuf, "Error", 16);
		}	
	}
	CloseHandle(stdoutReadPipe);
	CloseHandle(stdoutWritePipe);
	CloseHandle(stderrReadPipe);
	CloseHandle(stderrWritePipe);
	CloseHandle(stdinReadPipe);
	CloseHandle(stdinWritePipe);

	std::shared_ptr<ClassDatabaseFile> pClassDatabaseFile = std::make_shared<ClassDatabaseFile>();
	bool success = false;
	IAssetsReader *pReader = Create_AssetsReaderFromMemory(dataBuffer.data(), dataBuffer.size(), false);
	if (pReader)
	{
		success = pClassDatabaseFile->Read(pReader);
		Free_AssetsReader(pReader);
	}
	if (!success)
	{
		if (allowUserDialog)
		{
			std::wstring errorMessage = std::wstring(L"Unable to retrieve the script type database!");
			if (errorBuffer.size() > 0)
			{
				//Add a null terminator
				errorBuffer.push_back(0);
				size_t strLen = 0;
				WCHAR *wideLog = _MultiByteToWide((char*)errorBuffer.data(), strLen);
				if (wideLog)
				{
					errorMessage = errorMessage + L"\n" + wideLog;
					_FreeWCHAR(wideLog);
				}
			}
			MessageBoxW(appContext.getMainWindow().getWindow(), errorMessage.c_str(), L"Error", 16);
		}
		pClassDatabaseFile.reset();
	}
	else
	{
		if (errorBuffer.size() > 0 && allowUserDialog)
		{
			ShowMonoBehaviourExportErrors(appContext.getMainWindow().getHInstance(), appContext.getMainWindow().getWindow(), errorBuffer);
		}
	}
	return pClassDatabaseFile;
}

class MonoBehavErrorDialogParam
{
public:
	struct ErrorDesc
	{
		//points to errorBuffer with byte lengths, UTF-16
		size_t headerText_bufferOffset;
		size_t excText_bufferOffset;
		unsigned int headerText_bufferLen;
		unsigned int excText_bufferLen;
	};
public:
	HWND hParentWnd;
	std::vector<unsigned char> &errorBuffer;
	std::vector<ErrorDesc> errorDescriptors;
public:
	MonoBehavErrorDialogParam(HWND hParentWnd, std::vector<unsigned char> &errorBuffer)
		: hParentWnd(hParentWnd), errorBuffer(errorBuffer)
	{
	}
};

static void GetErrorString(MonoBehavErrorDialogParam *pParam, size_t offset, unsigned int byteLen,
	std::vector<wchar_t> &outString, bool pushback, bool nullchar = true)
{
	wchar_t *outBuf;
	if (pushback)
	{
		size_t oldLen = outString.size();
		outString.resize(oldLen + (byteLen >> 1));
		outBuf = &outString.data()[oldLen];
	}
	else
	{
		outString.resize((byteLen >> 1));
		outBuf = outString.data();
	}
	memcpy(outBuf, &pParam->errorBuffer.data()[offset], byteLen);
	if (nullchar)
		outString.push_back(0);
}

static void GetFullErrorString(MonoBehavErrorDialogParam *pParam, size_t errorIdx, 
	std::vector<wchar_t> &outString, bool pushback, bool nullchar=true)
{
	MonoBehavErrorDialogParam::ErrorDesc &desc = pParam->errorDescriptors[errorIdx];
	GetErrorString(pParam, desc.headerText_bufferOffset, desc.headerText_bufferLen, outString, pushback, false);
	outString.push_back(L' ');
	outString.push_back(L':');
	outString.push_back(L'\r');
	outString.push_back(L'\n');
	GetErrorString(pParam, desc.excText_bufferOffset, desc.excText_bufferLen, outString, true, nullchar);
}

static INT_PTR CALLBACK MonoBehavErrorDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent; bool all;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			MonoBehavErrorDialogParam *pParam = (MonoBehavErrorDialogParam*)lParam;
			HWND hErrorList = GetDlgItem(hDlg, IDC_ERRORLIST);
			if (hErrorList == NULL)
				return (INT_PTR)FALSE;
			std::vector<wchar_t> strBuffer;
			for (size_t i = 0; i < pParam->errorDescriptors.size(); i++)
			{
				MonoBehavErrorDialogParam::ErrorDesc &desc = pParam->errorDescriptors[i];
				GetErrorString(pParam, desc.headerText_bufferOffset, desc.headerText_bufferLen, strBuffer, false);
				ListBox_AddString(hErrorList, strBuffer.data());
			}
		}
		return (INT_PTR)TRUE;
		
	case WM_CLOSE:
	case WM_DESTROY:
		EndDialog(hDlg, LOWORD(wParam));
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		all = true;
		switch (wmId)
		{
			case IDC_BTNVIEW:
				{
					MonoBehavErrorDialogParam *pParam = (MonoBehavErrorDialogParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					HWND hErrorList = GetDlgItem(hDlg, IDC_ERRORLIST);
					int selection = ListBox_GetCurSel(hErrorList);
					if ((selection >= 0) && ((unsigned int)selection < pParam->errorDescriptors.size()))
					{
						std::vector<wchar_t> messageBuffer;
						GetFullErrorString(pParam, (size_t)selection, messageBuffer, false);
						MessageBoxW(hDlg, messageBuffer.data(), L"MonoBehaviour error", 0);
					}
				}
				break;
			case IDC_BTNCOPY:
				all = false;
			case IDC_BTNCOPYALL:
				{
					MonoBehavErrorDialogParam *pParam = (MonoBehavErrorDialogParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					HWND hErrorList = GetDlgItem(hDlg, IDC_ERRORLIST);
					int selection = ListBox_GetCurSel(hErrorList);
					std::vector<wchar_t> messageBuffer;
					if (all)
					{
						for (size_t i = 0; i < pParam->errorDescriptors.size(); i++)
						{
							if (i > 0)
							{
								messageBuffer.push_back('\r');
								messageBuffer.push_back('\n');
								messageBuffer.push_back('\r');
								messageBuffer.push_back('\n');
							}
							GetFullErrorString(pParam, i, messageBuffer, true, false);
						}
						messageBuffer.push_back(0);
					}
					else if ((selection >= 0) && ((unsigned int)selection < pParam->errorDescriptors.size()))
						GetFullErrorString(pParam, (size_t)selection, messageBuffer, false);
					HGLOBAL hClipboardMem = GlobalAlloc(GMEM_MOVEABLE, messageBuffer.size() * sizeof(wchar_t));
					void *pClipboardMem = nullptr;
					bool success = false;
					if ((hClipboardMem != nullptr) && ((pClipboardMem = GlobalLock(hClipboardMem)) != nullptr))
					{
						memcpy(pClipboardMem, messageBuffer.data(), messageBuffer.size() * sizeof(wchar_t));
						GlobalUnlock(pClipboardMem);
						if (OpenClipboard(pParam->hParentWnd))
						{
							if (SetClipboardData(CF_UNICODETEXT, hClipboardMem))
							{
								CloseClipboard();
								success = true;
							}
							else
							{
								CloseClipboard();
								MessageBox(hDlg, L"Unable to change the clipboard data!", L"ERROR", 16);
							}
						}
						else
							MessageBox(hDlg, L"Unable to open the clipboard!", L"ERROR", 16);
					}
					else
						MessageBox(hDlg, L"Unable to allocate the global clipboard buffer!", L"ERROR", 16);
					if (!success && hClipboardMem) 
						GlobalFree(hClipboardMem);
				}
				break;
			case IDOK:
			case IDCANCEL:
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void ShowMonoBehaviourExportErrors(HINSTANCE hInstance, HWND hParent, std::vector<unsigned char> &errorBuffer)
{
	/*
	errorBuffer format : any number of error entries, each of which is

	struct ErrorBufferEntry
	{
		unsigned int headerLen, excTextLen;
		unsigned char header[headerLen]; //UTF-16 buffer
		unsigned char excText[excTextLen]; //UTF-16 buffer
	};
	*/
	MonoBehavErrorDialogParam dlgParam(hParent, errorBuffer);
	size_t bufferIndex = 0;
	unsigned char *errorBufferRaw = errorBuffer.data();
	while ((bufferIndex + 8) < errorBuffer.size())
	{
		MonoBehavErrorDialogParam::ErrorDesc desc;
		desc.headerText_bufferLen = *(unsigned int*)(&errorBufferRaw[bufferIndex]);
		desc.excText_bufferLen = *(unsigned int*)(&errorBufferRaw[bufferIndex + 4]);
		desc.headerText_bufferOffset = bufferIndex + 8;
		desc.excText_bufferOffset = desc.headerText_bufferOffset + desc.headerText_bufferLen;
		if ((desc.excText_bufferOffset + desc.excText_bufferLen) <= errorBuffer.size() 
			&& (desc.excText_bufferLen & 1) == 0
			&& (desc.headerText_bufferLen & 1) == 0)
		{
			dlgParam.errorDescriptors.push_back(desc);
		}
		bufferIndex = desc.excText_bufferOffset + desc.excText_bufferLen;
	}
	if (dlgParam.errorDescriptors.size() > 0)
		DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_MONOBEHAVERROR), hParent, MonoBehavErrorDialogProc, (LPARAM)&dlgParam);
}