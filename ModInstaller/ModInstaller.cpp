// ModInstaller.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"
#include <stdio.h>
#include <tchar.h>
#include "../libStringConverter/convert.h"
#include "../AssetsTools/AssetsFileReader.h"
#include "../AssetsTools/InternalBundleReplacer.h"
#include "InstallerDataFormat.h"
#include "ModInstaller.h"
#include "InstallDialog.h"
#include <string>
#include <assert.h>

HMODULE DelayResolveImport(HMODULE hModule, PIMAGE_IMPORT_DESCRIPTOR pDescriptor)
{
	if (!pDescriptor->Name || !pDescriptor->FirstThunk)
		return NULL;
	HMODULE hNewLibrary = LoadLibraryA((LPCSTR) &((uint8_t*)hModule)[pDescriptor->Name]);
	if (!hNewLibrary)
		return NULL;
	PIMAGE_THUNK_DATA pThunks = (PIMAGE_THUNK_DATA) &((uint8_t*)hModule)[pDescriptor->FirstThunk];
	for (size_t i = 0; pThunks[i].u1.Function != 0; i++)
	{
		PVOID proc = NULL;
		if (IMAGE_SNAP_BY_ORDINAL(pThunks[i].u1.Ordinal))
		{
			proc = GetProcAddress(hNewLibrary, (LPCSTR) IMAGE_ORDINAL(pThunks[i].u1.Ordinal));
			if (!proc)
			{
				FreeLibrary(hNewLibrary);
				return NULL;
			}
		}
		else
		{
			PIMAGE_IMPORT_BY_NAME pImport = (PIMAGE_IMPORT_BY_NAME) &((uint8_t*)hModule)[pThunks[i].u1.AddressOfData];
			proc = GetProcAddress(hNewLibrary, (LPCSTR)&pImport->Name[0]);
			if (!proc)
			{
				FreeLibrary(hNewLibrary);
				return NULL;
			}
		}
		DWORD dwOldProt;
		VirtualProtect(&pThunks[i].u1.Function, sizeof(ULONG_PTR), PAGE_EXECUTE_READWRITE, &dwOldProt);
		pThunks[i].u1.Function = (ULONG_PTR)proc;
		VirtualProtect(&pThunks[i].u1.Function, sizeof(ULONG_PTR), dwOldProt, &dwOldProt);
	}
	return hNewLibrary;
}

static std::string getModulePath(HINSTANCE hInstance)
{
	std::vector<TCHAR> modulePathT;
	size_t ownPathLen = MAX_PATH;
	while (true)
	{
		modulePathT.resize(ownPathLen + 1, 0);
		SetLastError(0);
		DWORD result = GetModuleFileName(hInstance, modulePathT.data(), (DWORD)ownPathLen);
		if (result == 0)
		{
			modulePathT.clear();
			break;
		}
		else if (result == ownPathLen && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			ownPathLen += MAX_PATH;
		else
			break;
	}

	size_t outLen = 0;
	char *modulePath8 = _TCHARToMultiByte(modulePathT.data(), outLen);
	std::string modulePath(modulePath8);
	_FreeCHAR(modulePath8);
	return modulePath;
}

//Also checks that all section headers are in bounds.
size_t GetPEOverlayOffset(IAssetsReader *pReader)
{
	IMAGE_DOS_HEADER dosHeader = {};
	if (pReader->Read(0, sizeof(IMAGE_DOS_HEADER), &dosHeader) != sizeof(IMAGE_DOS_HEADER))
		return 0;
	QWORD fileHeaderPos = dosHeader.e_lfanew + sizeof(IMAGE_NT_SIGNATURE);
	IMAGE_FILE_HEADER fileHeader = {};
	if (pReader->Read(fileHeaderPos, sizeof(IMAGE_FILE_HEADER), &fileHeader) != sizeof(IMAGE_FILE_HEADER))
		return 0;
	std::vector<uint8_t> optHeaderBuf(fileHeader.SizeOfOptionalHeader);
	if (pReader->Read(fileHeaderPos + sizeof(IMAGE_FILE_HEADER), fileHeader.SizeOfOptionalHeader, optHeaderBuf.data()) != fileHeader.SizeOfOptionalHeader)
		return 0;
	DWORD fileAlignment = 1;
	if (fileHeader.Machine == 0x014C) //is i386 / 32bit
		fileAlignment = ((IMAGE_OPTIONAL_HEADER32*)optHeaderBuf.data())->FileAlignment;
	else if (fileHeader.Machine == 0x8664) //is AMD64 / 64bit
		fileAlignment = ((IMAGE_OPTIONAL_HEADER64*)optHeaderBuf.data())->FileAlignment;
	else
		return 0;
	if (fileAlignment == 0 || fileAlignment >= 0x7FFFFFFF
		|| ((fileAlignment + (fileAlignment - 1)) & ~(fileAlignment - 1)) != fileAlignment
		|| ((2*fileAlignment) & ~(fileAlignment - 1)) != 2*fileAlignment)
		return 0; //Rudimentary check that fileAlignment is a power of two (may not be fully precise), just in case. 
	QWORD sectionHeadersPos = fileHeaderPos + sizeof(IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
	std::vector<IMAGE_SECTION_HEADER> sectionHeaders(fileHeader.NumberOfSections);
	if (pReader->Read(sectionHeadersPos, sizeof(IMAGE_SECTION_HEADER) * sectionHeaders.size(), sectionHeaders.data())
		!= sizeof(IMAGE_SECTION_HEADER) * sectionHeaders.size())
		return 0;

	DWORD rawEndAddressMax = 0;
	for (size_t _i = sectionHeaders.size(); _i > 0; --_i)
	{
		size_t i = _i - 1;
		if (sectionHeaders[i].SizeOfRawData > 0)
		{
			DWORD downAlignedRawPtr = sectionHeaders[i].PointerToRawData & ~511;
			DWORD endOfRaw = sectionHeaders[i].PointerToRawData + sectionHeaders[i].SizeOfRawData;
			endOfRaw = (endOfRaw + (fileAlignment - 1)) & ~(fileAlignment - 1);

			DWORD actualSize = (sectionHeaders[i].Misc.VirtualSize != 0)
				? std::min(sectionHeaders[i].Misc.VirtualSize, endOfRaw - downAlignedRawPtr)
				: (endOfRaw - downAlignedRawPtr);
			actualSize = (actualSize + 511) & ~511;

			DWORD selfEndAddress = downAlignedRawPtr + actualSize;
			if (selfEndAddress < sectionHeaders[i].PointerToRawData)
				assert(false);
			rawEndAddressMax = std::max(rawEndAddressMax, selfEndAddress);
		}
	}
	return rawEndAddressMax;
}

void RunModInstaller(HINSTANCE hInstance);
extern "C" BOOL WINAPI _CRT_INIT(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved);
extern "C"
{
	//Initialize to 1 instead of 0 to make sure this field is not stripped from the .dll file as uninitialized data
	__declspec(dllexport) DWORD delayResolveImportRVAs[32] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}; //if one is null or 1, it's not imported
	__declspec(dllexport) int APIENTRY _WinMain();
}
int APIENTRY _WinMain()
{
#ifdef _DEBUG
	MessageBox(NULL, 
		TEXT("Installer says hello!"),
		TEXT("UABE Mod installer"), 0);
#endif
	HINSTANCE hInstance = GetModuleHandle(NULL);
	for (DWORD i = 0; i < sizeof(delayResolveImportRVAs) / sizeof(DWORD); i++)
	{
		if (delayResolveImportRVAs[i] > 1)
		{
			if (!DelayResolveImport(hInstance, (PIMAGE_IMPORT_DESCRIPTOR)( &((uint8_t*)hInstance)[delayResolveImportRVAs[i]] )))
			{
				MessageBox(NULL, 
					TEXT("Unable to find the MSVC++ 2022 redistributable!\nMake sure the 32bit and 64bit versions are installed."),
					TEXT("UABE Mod installer"), 16);
				return 0;
			}
		}
	}
	_CRT_INIT(hInstance, DLL_PROCESS_ATTACH, NULL);
	HeapSetInformation(GetProcessHeap(), HeapEnableTerminationOnCorruption, NULL, 0);

	RunModInstaller(hInstance);

	exit(0);
	_CRT_INIT(hInstance, DLL_PROCESS_DETACH, NULL);
	return 0;
}

static void RunModInstaller(HINSTANCE hInstance)
{
	std::string selfPath = getModulePath(hInstance);
	std::unique_ptr<IAssetsReader> pSelfReader(Create_AssetsReaderFromFile(selfPath.c_str(), true, RWOpenFlags_Immediately));
	QWORD selfFileEnd = 0;
	size_t overlayOffset = 0;
	if (pSelfReader != nullptr)
	{
		pSelfReader->Seek(AssetsSeek_End, 0);
		pSelfReader->Tell(selfFileEnd);
	}
	if (pSelfReader == nullptr || (overlayOffset = GetPEOverlayOffset(pSelfReader.get())) == 0 || selfFileEnd <= overlayOffset)
	{
		MessageBox(NULL, 
			TEXT("Unable to locate the installer data. Make sure the installer executable is readable, or try moving the installer to another directory."),
			TEXT("UABE Mod installer"), 0);
		return;
	}
	std::unique_ptr<InstallDialogsData> pInstallDialogs(InitInstallDialogs(hInstance));
	if (pInstallDialogs == nullptr)
	{
		MessageBox(NULL, 
			TEXT("Unable to create the installer window. I have no idea why this happened."),
			TEXT("UABE Mod installer"), 0);
		return;
	}
	DialogController_Prepare *pPrepDlgController = dynamic_cast<DialogController_Prepare*>(pInstallDialogs->pControllers[InstallDialog_Prepare]);
	
	pPrepDlgController->SetStatus(InstallDialogPrepareStatus_LoadInstData, InstallPrepareStatus_Active);

	std::unique_ptr<IAssetsReader> pPackageReader(
		Create_AssetsReaderFromReaderRange(pSelfReader.get(), overlayOffset, selfFileEnd - overlayOffset));
	//AssetsReaderFromSplitResourceWrapper readerWrapper(readerPar);

	InstallerPackageFile packageFile;
	QWORD filePos = 0;
	bool readPackage = packageFile.Read(filePos, pPackageReader.get());
	if (!readPackage)
	{
		pPrepDlgController->SetStatus(InstallDialogPrepareStatus_LoadInstData, InstallPrepareStatus_Error);
		MessageBox(NULL, 
			TEXT("Unable to load the installer data. Either the installer is invalid, or the system is out of memory."),
			TEXT("UABE Mod installer"), 0);
		CloseDialogThread(pInstallDialogs.get());
		return;
	}
	SanitizePackageFile(packageFile);

	pPrepDlgController->SetStatus(InstallDialogPrepareStatus_LoadInstData, InstallPrepareStatus_Completed);
	if (!ShowInstallDialog(pInstallDialogs.get(), InstallDialog_Introduction))
	{
		CloseDialogThread(pInstallDialogs.get());
		return;
	}
	for (int i = 0; i < InstallDialog_COUNT; i++)
	{
		DialogControllerTitled *titledController =
			dynamic_cast<DialogControllerTitled*>(pInstallDialogs->pControllers[i]);
		if (titledController)
			titledController->SetModName(packageFile.modName.c_str());
	}
	DialogController_Description *pDescController = 
		dynamic_cast<DialogController_Description*>(pInstallDialogs->pControllers[InstallDialog_Description]);
	pDescController->SetAuthors(packageFile.modCreators.c_str());
	pDescController->SetDescription(packageFile.modDescription.c_str());
	DialogController_PathSelect *pPathController = 
		dynamic_cast<DialogController_PathSelect*>(pInstallDialogs->pControllers[InstallDialog_PathSelect]);
	DialogController_Progress *pProgressController = 
		dynamic_cast<DialogController_Progress*>(pInstallDialogs->pControllers[InstallDialog_Progress]);
	DialogController_Complete *pCompleteController = 
		dynamic_cast<DialogController_Complete*>(pInstallDialogs->pControllers[InstallDialog_Complete]);
	{
		DWORD curDirLen = GetCurrentDirectory(0, NULL);
		//curDirLen includes the null-char but it could also be 0 if there's an error
		TCHAR *curDir = new TCHAR[curDirLen + 1];
		GetCurrentDirectory(curDirLen, curDir);
		curDir[curDirLen] = 0;
		pPathController->SetPath(curDir);
		delete[] curDir;
	}
	pPathController->FillModsTree(&packageFile);
	size_t selPathLen = 0; TCHAR *selPath = NULL;
	while (true)
	{
		WaitInstall_Loop:
		EInstallDialogs newDialogType = WaitForDialogChanged(pInstallDialogs.get());
		if (newDialogType == (EInstallDialogs)-1)
		{
			CloseDialogThread(pInstallDialogs.get());
			return;
		}
		if (newDialogType == InstallDialog_Progress)
		{
			selPath = pPathController->GetPath(selPathLen);
			if (!SetCurrentDirectory(selPath))
			{
				DWORD msgResult = MessageBox(pInstallDialogs->hWindow, 
					TEXT("Unable to switch to the path entered, most likely it's invalid.\n")\
					TEXT("Press OK to ignore and continue, press Cancel to retry."),
					TEXT("UABE Mod Installer"),
					MB_OKCANCEL | MB_ICONWARNING);
				if (msgResult == IDCANCEL)
				{
					ShowInstallDialog(pInstallDialogs.get(), InstallDialog_PathSelect);
					goto WaitInstall_Loop;
				}
			}
			for (size_t i = 0; i < packageFile.affectedAssets.size(); i++)
			{
				TCHAR *_MultiByteToTCHAR(const char *mb, size_t &len);
				void _FreeTCHAR(TCHAR *tc);
				size_t pathLenT;
				TCHAR *pathT = _MultiByteToTCHAR(packageFile.affectedAssets[i].path.c_str(), pathLenT);
				FILE *testFile = _tfopen(pathT, TEXT("rb"));
				if (!testFile)
				{
					TCHAR *sprntTmp = new TCHAR[pathLenT + 164];
					_stprintf_s(sprntTmp, pathLenT + 164, 
						TEXT("Unable to open %s! Likely it's not in the path entered or it's locked.")\
						TEXT("Press abort to select another path, retry to retry or ignore to continue."), pathT);
					DWORD msgResult = MessageBox(pInstallDialogs->hWindow,
						sprntTmp,
						TEXT("UABE Mod Installer"),
						MB_ABORTRETRYIGNORE | MB_ICONWARNING);
					delete[] sprntTmp;
					if (msgResult == IDABORT)
					{
						_FreeTCHAR(pathT);
						ShowInstallDialog(pInstallDialogs.get(), InstallDialog_PathSelect);
						goto WaitInstall_Loop;
					}
					if (msgResult == IDRETRY)
						i--;
				}
				else
					fclose(testFile);
				_FreeTCHAR(pathT);
			}
			break;
		}
	}
			
	Install(packageFile, NULL, pProgressController, pCompleteController);

	//FreeAssetsReaderFromSplitResource(readerPar);
	while (true)
	{
		EInstallDialogs newDialogType = WaitForDialogChanged(pInstallDialogs.get());
		if (newDialogType == (EInstallDialogs)-1)
		{
			CloseDialogThread(pInstallDialogs.get());
			return;
		}
	}
}



void SanitizePackageFile(InstallerPackageFile &packageFile)
{
	for (size_t i = 0; i < packageFile.affectedAssets.size(); i++)
	{
		InstallerPackageAssetsDesc &assetsDesc = packageFile.affectedAssets[i];
		for (size_t k = 0; k < assetsDesc.replacers.size(); k++)
		{
			if (assetsDesc.replacers[k] == NULL)
			{
				assetsDesc.replacers.erase(assetsDesc.replacers.begin() + k);
				k--;
				continue;
			}
		}
	}
}

typedef void(_cdecl *LogCallback)(const char *message);
static void LogToBoth(const char *message, DialogController_Progress *pProgressController, LogCallback log)
{
	if (pProgressController)
		pProgressController->AddToLog(message);
	if (log)
		log(message);
}
static void LogToBoth(const wchar_t *message, DialogController_Progress *pProgressController, LogCallback log)
{
	if (pProgressController)
		pProgressController->AddToLog(message);
	if (log)
	{
		size_t mbLen = 0;
		char *mbMessage = _WideToMultiByte(message, mbLen);
		log(mbMessage);
		_FreeCHAR(mbMessage);
	}
}
int Install(InstallerPackageFile &packageFile, LogCallback log, 
	DialogController_Progress *pProgressController, DialogController_Complete *pCompleteController)
{
	bool errorsOccured = false;
	double curFileLenProgress = (double)0.99F / (double)packageFile.affectedAssets.size();
	struct InstallFileDesc
	{
		std::basic_string<TCHAR> tOrigFilePath;
		bool originalFileExists = false;
	};
	std::vector<InstallFileDesc> fileDescs(packageFile.affectedAssets.size());
	for (size_t i = 0; i < packageFile.affectedAssets.size(); i++)
	{
		if (pProgressController && pProgressController->GetCancelled())
			break;
		InstallerPackageAssetsDesc &affectedDesc = packageFile.affectedAssets[i];

		double curFileBeginProgress = (double)i * (double)0.99F / (double)packageFile.affectedAssets.size();
		if (pProgressController)
			pProgressController->SetProgress((float)(curFileBeginProgress * 100.0), affectedDesc.path.c_str());

		bool needsOriginalFile = true;
		if (affectedDesc.type == InstallerPackageAssetsType::Resources)
		{
			if (affectedDesc.replacers.size() == 1 &&
				dynamic_cast<BundleEntryModifierByResources*>(
					reinterpret_cast<BundleReplacer*>(affectedDesc.replacers[0].get())
					) != nullptr)
			{
				needsOriginalFile = 
					reinterpret_cast<BundleEntryModifierByResources*>(affectedDesc.replacers[0].get())
					->RequiresEntryReader();
			}
			else
			{
				errorsOccured = true;
				LogToBoth("ERROR: Unexpected resource file replacer!\r\n", pProgressController, log);
				continue;
			}
		}

		size_t pathLenT;
		auto pathT = unique_MultiByteToTCHAR(affectedDesc.path.c_str(), pathLenT);
		std::unique_ptr<IAssetsReader> pCurReader;
		if (needsOriginalFile)
		{
			LogToBoth("Opening file ", pProgressController, log);
			LogToBoth(pathT.get(), pProgressController, log);
			pCurReader.reset(Create_AssetsReaderFromFile(pathT.get(), true, RWOpenFlags_Immediately));
			//FILE *curFile = _tfopen(pathT, TEXT("rb"));
			if (!pCurReader)
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			fileDescs[i].originalFileExists = true;
		}
		else
		{
			std::unique_ptr<IAssetsReader> tmpReader(Create_AssetsReaderFromFile(pathT.get(), true, RWOpenFlags_Immediately));
			fileDescs[i].originalFileExists = (tmpReader != nullptr);
		}
		bool curFileModded = false;
		std::basic_string<TCHAR> decompFileName;
		switch (affectedDesc.type)
		{
		case InstallerPackageAssetsType::Assets:
		{
			LogToBoth("Opening assets", pProgressController, log);
			AssetsFile assetsFile = AssetsFile(pCurReader.get());
			if (!assetsFile.VerifyAssetsFile())
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			if (pProgressController)
				pProgressController->SetProgress((float)((curFileBeginProgress + (curFileLenProgress * 0.05F)) * 100.0), affectedDesc.path.c_str());
			std::basic_string<TCHAR> modFileName = pathT.get();
			modFileName += TEXT(".mod");
			std::unique_ptr<IAssetsWriter> pModWriter(Create_AssetsWriterToFile(modFileName.c_str(), true, true, RWOpenFlags_Immediately));
			//FILE *pModFile = _tfopen(modFileName, TEXT("wb"));
			LogToBoth("Modifying and writing assets to ", pProgressController, log);
			LogToBoth(modFileName.c_str(), pProgressController, log);
			if (!pModWriter)
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			if (pProgressController)
				pProgressController->SetProgress((float)((curFileBeginProgress + (curFileLenProgress * 0.1F)) * 100.0), affectedDesc.path.c_str());
			std::vector<AssetsReplacer*> pReplacers(affectedDesc.replacers.size());
			for (size_t i = 0; i < affectedDesc.replacers.size(); ++i)
				pReplacers[i] = reinterpret_cast<AssetsReplacer*>(affectedDesc.replacers[i].get());
			if (!assetsFile.Write(pModWriter.get(), 0, pReplacers.data(), pReplacers.size(), 0, &packageFile.addedTypes))
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			pModWriter.reset();
			LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			curFileModded = true;
		}
		break;
		case InstallerPackageAssetsType::Bundle:
		{
			LogToBoth("Opening bundle", pProgressController, log);
			AssetBundleFile bundleFile;
			if (!bundleFile.Read(pCurReader.get(), NULL, true))
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			if (pProgressController)
				pProgressController->SetProgress((float)((curFileBeginProgress + (curFileLenProgress * 0.1F)) * 100.0), affectedDesc.path.c_str());
			bool iscompressed = (bundleFile.IsCompressed() || bundleFile.bundleHeader6.fileVersion >= 6 && (bundleFile.bundleHeader6.flags & 0x3F) != 0);
			if (iscompressed)
			{
				decompFileName.assign(pathT.get());
				decompFileName += TEXT(".decomp");
				std::unique_ptr<IAssetsWriter> pDecompWriter(Create_AssetsWriterToFile(decompFileName.c_str(), true, true, RWOpenFlags_Immediately));
				//FILE *pDecompFile = _tfopen(decompFileName, TEXT("wb"));
				LogToBoth("Decompressing bundle to ", pProgressController, log);
				LogToBoth(decompFileName.c_str(), pProgressController, log);
				if (!pDecompWriter)
				{
					errorsOccured = true;
					LogToBoth(" [FAILURE]\r\n", pProgressController, log);
					bundleFile.Close();
					continue;
				}
				if (!bundleFile.Unpack(pCurReader.get(), pDecompWriter.get()))
				{
					errorsOccured = true;
					LogToBoth(" [FAILURE]\r\n", pProgressController, log);
					bundleFile.Close();
					continue;
				}
				bundleFile.Close();
				pDecompWriter.reset();
				pCurReader.reset(Create_AssetsReaderFromFile(decompFileName.c_str(), true, RWOpenFlags_Immediately));
				//curFile = _tfopen(decompFileName, TEXT("rb"));
				if (!pCurReader || !bundleFile.Read(pCurReader.get(), NULL, false))
				{
					errorsOccured = true;
					LogToBoth(" [FAILURE]\r\n", pProgressController, log);
					continue;
				}
				LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
				if (pProgressController)
					pProgressController->SetProgress((float)((curFileBeginProgress + (curFileLenProgress * 0.5F)) * 100.0), affectedDesc.path.c_str());
			}
			std::basic_string<TCHAR> modFileName = pathT.get();
			modFileName += TEXT(".mod");
			std::unique_ptr<IAssetsWriter> pModWriter(Create_AssetsWriterToFile(modFileName.c_str(), true, true, RWOpenFlags_Immediately));
			//FILE *pModFile = _tfopen(modFileName, TEXT("wb"));
			LogToBoth("Modifying and writing bundle to ", pProgressController, log);
			LogToBoth(modFileName.c_str(), pProgressController, log);
			if (!pModWriter)
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				bundleFile.Close();
				continue;
			}
			std::vector<BundleReplacer*> pReplacers(affectedDesc.replacers.size());
			for (size_t i = 0; i < affectedDesc.replacers.size(); ++i)
				pReplacers[i] = reinterpret_cast<BundleReplacer*>(affectedDesc.replacers[i].get());
			if (!bundleFile.Write(pCurReader.get(), pModWriter.get(), pReplacers.data(), pReplacers.size(),
				log, &packageFile.addedTypes))
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				bundleFile.Close();
				continue;
			}
			bundleFile.Close();
			pModWriter.reset();
			LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			curFileModded = true;
		}
		break;
		case InstallerPackageAssetsType::Resources:
		{
			std::basic_string<TCHAR> modFileName = pathT.get();
			modFileName += TEXT(".mod");
			std::unique_ptr<IAssetsWriter> pModWriter(Create_AssetsWriterToFile(modFileName.c_str(), true, true, RWOpenFlags_Immediately));
			LogToBoth(needsOriginalFile ? "Writing resources to " : "Modifying and writing resources to ", pProgressController, log);
			LogToBoth(modFileName.c_str(), pProgressController, log);
			if (!pModWriter)
			{
				errorsOccured = true;
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
				continue;
			}
			auto* pEntryModifier = reinterpret_cast<BundleEntryModifierByResources*>(affectedDesc.replacers[0].get());
			if (needsOriginalFile)
				pEntryModifier->Init(nullptr, pCurReader.get(), 0, std::numeric_limits<QWORD>::max());
			QWORD newSize = pEntryModifier->Write(0, pModWriter.get());
			if (pEntryModifier->getSize() != newSize)
			{
				errorsOccured = true;
				LogToBoth(" [INCOMPLETE]\r\n", pProgressController, log);
			}
			else
			{
				LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			}
			pEntryModifier->Uninit();
		}
		break;
		}
		
		if (curFileModded)
			fileDescs[i].tOrigFilePath = pathT.get();
		pCurReader.reset();
		if (!decompFileName.empty())
		{
			LogToBoth("Deleting decompressed unmodded file", pProgressController, log);
			if (DeleteFile(decompFileName.c_str()))
				LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			else
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
		}
	}
	int ret = 0;
	if (pProgressController)
		pProgressController->DisableCancel();
	if (pProgressController && pProgressController->GetCancelled())
	{
		for (size_t _i = packageFile.affectedAssets.size(); _i > 0; _i--)
		{
			size_t i = _i - 1;
			double curFileBeginProgress = (double)(i + 1) * (double)0.99F / (double)packageFile.affectedAssets.size();
			pProgressController->SetProgress((float)(curFileBeginProgress * 100.0), "");
			if (fileDescs[i].tOrigFilePath.empty())
				continue;
			LogToBoth("Removing modded file of ", pProgressController, log);
			LogToBoth(fileDescs[i].tOrigFilePath.c_str(), pProgressController, log);
			std::basic_string<TCHAR> modFileName = fileDescs[i].tOrigFilePath + TEXT(".mod");
			if (DeleteFile(modFileName.c_str()))
				LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
			else
				LogToBoth(" [FAILURE]\r\n", pProgressController, log);
		}
		pProgressController->SetProgress(0.0F, "");
		if (pCompleteController)
			pCompleteController->SetCompleteText(
				TEXT("The mod installation is cancelled. ")\
				TEXT("No changes have been applied to the files. All temporary files have been removed.\r\n")\
				TEXT(""));
		ret = MI_CANCEL;
	}
	else
	{
		if (pProgressController)
			pProgressController->SetProgress(99.0F, "");
		if (errorsOccured)
		{
			if (pCompleteController)
				pCompleteController->SetCompleteText(
					TEXT("At least one error occured while installing the mod.\r\n")\
					TEXT("Successfully modified files (if there are any) are saved as .mod files.\r\n")\
					TEXT("Press back and see the log for more details.\r\n"));
			ret = MI_INCOMPLETE;
		}
		else
		{
			bool allFilesMoved = true;
			for (size_t i = 0; i < packageFile.affectedAssets.size(); i++)
			{
				double curFileProgress = (double)0.99F + 
					((double)i * (double)0.01F) / (double)packageFile.affectedAssets.size();
				if (pProgressController)
					pProgressController->SetProgress((float)(curFileProgress * 100.0), packageFile.affectedAssets[i].path.c_str());
				if (fileDescs[i].tOrigFilePath.empty())
					continue;
				bool doRenameModdedFile = true;
				const std::basic_string<TCHAR>& filePath = fileDescs[i].tOrigFilePath;
				std::vector<TCHAR> backupFilePath;
				if (fileDescs[i].originalFileExists)
				{
					LogToBoth("Swapping original and mod of ", pProgressController, log);
					LogToBoth(fileDescs[i].tOrigFilePath.c_str(), pProgressController, log);
					backupFilePath.assign(filePath.begin(), filePath.end());
					//http://stackoverflow.com/a/6218957
					int backupIndex = -1;
					for (int k = 0; k < 10000; k++)
					{
						backupFilePath.resize(filePath.size());
						TCHAR sprntTmp[32];
						_stprintf_s(sprntTmp, TEXT(".bak%04d"), k);
						backupFilePath.insert(backupFilePath.end(), sprntTmp, sprntTmp + _tcslen(sprntTmp) + 1);
						if (GetFileAttributes(backupFilePath.data()) == INVALID_FILE_ATTRIBUTES)
						{
							backupIndex = k;
							break;
						}
					}
					backupFilePath.push_back(0); //Should not be necessary.
					if (backupIndex == -1)
					{
						allFilesMoved = false;
						doRenameModdedFile = false;
						LogToBoth(" [FAILURE]\r\nIt seems like you already have 10000 backups?!?\r\n", pProgressController, log);
					}
					else
					{
						if (!MoveFile(filePath.c_str(), backupFilePath.data()))
						{
							allFilesMoved = false;
							doRenameModdedFile = false;
							LogToBoth(" [FAILURE]\r\nUnable to rename the original file.\r\n", pProgressController, log);
						}
					}
				}
				if (doRenameModdedFile)
				{
					std::basic_string<TCHAR> modFilePath = filePath + TEXT(".mod");
					if (!MoveFile(modFilePath.c_str(), filePath.c_str()))
					{
						allFilesMoved = false;
						LogToBoth(" [FAILURE]\r\nUnable to rename the modded file.\r\n", pProgressController, log);
						if (!backupFilePath.empty())
							MoveFile(backupFilePath.data(), filePath.c_str());
					}
					else
						LogToBoth(" [SUCCESS]\r\n", pProgressController, log);
				}
			}
			if (allFilesMoved)
			{
				if (pCompleteController)
					pCompleteController->SetCompleteText(
						TEXT("The mod has been installed successfully and should now be usable. ")\
						TEXT("The old files are preserved with .bak + number file names.\r\n")\
						TEXT(""));
				ret = 0;
			}
			else
			{
				if (pCompleteController)
					pCompleteController->SetCompleteText(
						TEXT("The mod is not completely installed. ")\
						TEXT("Not all original files were moved to backup files (.bak + number) and swapped with modded ones. ")\
						TEXT("Some modified files still have a .mod file name.\r\n")\
						TEXT("See the log for more details.\r\n")\
						TEXT(""));
				ret = MI_MOVEFILEFAIL;
			}
		}
		if (pProgressController)
			pProgressController->SetProgress(100.0F, "");
	}
	if (pCompleteController)
		pCompleteController->SetAuthors(packageFile.modCreators.c_str());
	if (pProgressController)
		pProgressController->EnableContinue();
	return ret;
}
int Install(InstallerPackageFile &packageFile, LogCallback log)
{
	return Install(packageFile, log, NULL, NULL);
}