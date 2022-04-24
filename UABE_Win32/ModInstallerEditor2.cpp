#include "stdafx.h"
#include "ModInstallerEditor2.h"
#include "resource.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include "../ModInstaller/ModInstaller.h"
#include "../AssetsTools/InternalAssetsReplacer.h"
#include "../AssetsTools/InternalBundleReplacer.h"
#include "../libStringConverter/convert.h"
#include "FileDialog.h"
#include <Shlwapi.h>
#include <WindowsX.h>
#include <algorithm>

void Win32ModInstallerEditor::UpdateDisplayedRelPaths()
{
	HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
	HWND hTreeChanges = GetDlgItem(hDlg, IDC_TREECHANGES);
	if (!hEditBaseFolder || !hTreeChanges)
		return;
	size_t baseDirLen = (size_t)Edit_GetTextLength(hEditBaseFolder);
	std::vector<TCHAR> tBaseDir(baseDirLen + 1);
	Edit_GetText(hEditBaseFolder, tBaseDir.data(), (int)(baseDirLen + 1));
	tBaseDir[baseDirLen] = 0;

	//PathRelativePathTo https://msdn.microsoft.com/en-us/library/bb773740(VS.85).aspx
	for (size_t i = 0; i < visibleFiles.size(); i++)
	{
		assert(visibleFiles[i].treeViewEntry != NULL);
		size_t filePathLen = 0;
		auto tcFilePath = unique_MultiByteToTCHAR(visibleFiles[i].pathOrName.c_str(), filePathLen);
		size_t newPathLen = baseDirLen + filePathLen + 1; if (newPathLen <= MAX_PATH) newPathLen = MAX_PATH + 1;
		std::vector<TCHAR> newPath(newPathLen);
		if (!PathRelativePathTo(newPath.data(), tBaseDir.data(), FILE_ATTRIBUTE_DIRECTORY, tcFilePath.get(), FILE_ATTRIBUTE_NORMAL))
			memcpy(newPath.data(), tcFilePath.get(), (filePathLen + 1) * sizeof(TCHAR));
							
		TVITEMEX itemex;
		itemex.hItem = (HTREEITEM)visibleFiles[i].treeViewEntry;
		itemex.mask = TVIF_HANDLE | TVIF_TEXT;
		itemex.pszText = newPath.data();
		itemex.cchTextMax = (int)_tcslen(newPath.data());
		TreeView_SetItem(hTreeChanges, &itemex);
	}
}
void Win32ModInstallerEditor::SelectAndLoadIcon()
{
	static const GUID UABE_FILEDIALOG_ICON_GUID = { 0x6ac81505, 0xed13, 0xdca2, 0x14, 0xf5, 0x70, 0xf2, 0x14, 0xab, 0xf6, 0x73 };
	HWND hIIcon = GetDlgItem(hDlg, IDC_IICON);
	if (!hIIcon)
		return;
	WCHAR *pakPath = NULL;
	if (SUCCEEDED(ShowFileOpenDialog(hDlg, &pakPath, L"*.ico|Icon file:",
		nullptr, nullptr, nullptr, UABE_FILEDIALOG_ICON_GUID)))
	{
		IAssetsReader *pFileReader = Create_AssetsReaderFromFile(pakPath, true, RWOpenFlags_Immediately);
		if (pFileReader)
		{
			pFileReader->Seek(AssetsSeek_End, 0);
			QWORD _size = 0;
			pFileReader->Tell(_size);
			size_t size = (size_t)_size;
			pFileReader->Seek(AssetsSeek_Begin, 0);
			std::vector<uint8_t> fileData(size, 0);
			pFileReader->Read(size, fileData.data());
			Free_AssetsReader(pFileReader);

			HICON hIcon = (HICON)LoadImage(NULL, pakPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
			FreeCOMFilePathBuf(&pakPath);
			if (hIcon)
			{
				HICON hOldIcon = (HICON)SendMessage(hIIcon, STM_GETIMAGE, IMAGE_ICON, NULL);
				SendMessage(hIIcon, STM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
				if (hOldIcon)
					DestroyIcon(hOldIcon);
				this->iconData.swap(fileData);
			}
		}
		FreeCOMFilePathBuf(&pakPath);
	}
}

void Win32ModInstallerEditor::SelectAndImportPackage()
{
	HWND hTreeChanges = GetDlgItem(hDlg, IDC_TREECHANGES);
	HWND hEModName = GetDlgItem(hDlg, IDC_EMODNAME);
	HWND hEAuthors = GetDlgItem(hDlg, IDC_EAUTHORS);
	HWND hEDescription = GetDlgItem(hDlg, IDC_EDESCRIPTION);
	if (!hTreeChanges || !hEModName || !hEAuthors || !hEDescription)
		return;
	WCHAR *pakPath = NULL;
	if (FAILED(ShowFileOpenDialog(hDlg, &pakPath, L"*.emip|UABE Mod Installer Package:*.exe|UABE Installer:",
		nullptr, nullptr, nullptr, UABE_FILEDIALOG_FILE_GUID)))
		return;
	std::shared_ptr<IAssetsReader> pReader(Create_AssetsReaderFromFile(pakPath, true, RWOpenFlags_Immediately), Free_AssetsReader);
	FreeCOMFilePathBuf(&pakPath);
	if (!pReader)
		return;
	InstallerPackageFile loadedPackage = InstallerPackageFile();
	QWORD readPos = 0;
	bool success = loadedPackage.Read(readPos, pReader);
	if (!success)
	{
		size_t overlayOffset = GetPEOverlayOffset(pReader.get());
		if (overlayOffset != 0)
		{
			readPos = overlayOffset;
			success = loadedPackage.Read(readPos, pReader);
		}
	}
	if (!success)
	{
		MessageBox(appContext.getMainWindow().getWindow(), TEXT("Unable to understand the package file!\n")\
			TEXT("Make sure the selected file actually is a valid package file."), TEXT("ERROR"), 16);
		return;
	}
	WCHAR *newBasePath = NULL;
	if (ShowFolderSelectDialog(hDlg, &newBasePath, L"Select the base path", UABE_FILEDIALOG_FILE_GUID))
	{
		size_t wBasePathLen = wcslen(newBasePath);
		InstallerPackageAssetsDesc tempDesc;
		for (size_t i = 0; i < loadedPackage.affectedAssets.size(); i++)
		{
			size_t wPathLen = 0;
			WCHAR *wPath = _MultiByteToWide(loadedPackage.affectedAssets[i].path.c_str(), wPathLen);
			std::vector<WCHAR> combinedPathBuf(std::max<size_t>(wBasePathLen + wPathLen + 16, MAX_PATH));
			bool changePath = PathCombine(combinedPathBuf.data(), newBasePath, wPath) != NULL;
			_FreeWCHAR(wPath);
			if (changePath)
			{
				tempDesc.type = loadedPackage.affectedAssets[i].type;
				tempDesc.replacers.assign(loadedPackage.affectedAssets[i].replacers.begin(), 
					loadedPackage.affectedAssets[i].replacers.end());
				size_t mbCombinedPathLen = 0;
				tempDesc.path = _WideToMultiByte(combinedPathBuf.data(), mbCombinedPathLen);
				loadedPackage.affectedAssets[i] = tempDesc;
			}
		}
		FreeCOMFilePathBuf(&newBasePath);
	}
	if (Edit_GetTextLength(hEModName) == 0)
	{
		auto tcTemp = unique_MultiByteToTCHAR(loadedPackage.modName.c_str());
		Edit_SetText(hEModName, tcTemp.get());
	}
	if (Edit_GetTextLength(hEAuthors) == 0)
	{
		auto tcTemp = unique_MultiByteToTCHAR(loadedPackage.modCreators.c_str());
		Edit_SetText(hEAuthors, tcTemp.get());
	}
	if (Edit_GetTextLength(hEDescription) == 0)
	{
		auto tcTemp = unique_MultiByteToTCHAR(loadedPackage.modDescription.c_str());
		Edit_SetText(hEDescription, tcTemp.get());
	}
	
	MergeInstallerData(loadedPackage);
	UpdateModsTree();
	//UpdateDisplayedRelPaths();
}


void Win32ModInstallerEditor::MergeInstallerData(InstallerPackageFile &newFile)
{
	//Note: Is O(n²), but should be fine since the amount of types n is relatively small.
	for (size_t i = 0; i < newFile.addedTypes.classes.size(); i++)
	{
		bool alreadyExists = false;
		for (size_t k = 0; k < typesToExport.classes.size(); k++)
		{
			if (newFile.addedTypes.classes[i].classId == typesToExport.classes[k].classId)
			{
				alreadyExists = true;
				break;
			}
		}
		if (!alreadyExists)
		{
			typesToExport.InsertFrom(&newFile.addedTypes, &newFile.addedTypes.classes[i]);
		}
	}
	if (!newFile.affectedAssets.empty())
		this->changedFlag = true;
	for (size_t i = newFile.affectedAssets.size(); i > 0; i--)
	{
		InstallerPackageAssetsDesc &newDesc = newFile.affectedAssets[i-1];
		VisibleFileEntry newEntry(this->appContext, newDesc);
		bool merged = false;
		for (size_t k = visibleFiles.size(); k > 0; k--)
		{
			VisibleFileEntry &existingEntry = visibleFiles[k-1];
			//TODO: Use C++17 std::filesystem::equivalent instead of string comparison.
			if (newEntry.fileType == existingEntry.fileType && !newEntry.pathNull && newEntry.pathOrName == existingEntry.pathOrName)
			{
				auto resolveConflict = [&newEntry, this](VisibleReplacerEntry& existing, const VisibleReplacerEntry& other)
				{
					std::string message;
					if (dynamic_cast<BundleEntryModifierByResources*>(existing.pReplacer.get()) != nullptr)
					{
						auto* pExistingReplacer = reinterpret_cast<BundleEntryModifierByResources*>(existing.pReplacer.get());
						assert(dynamic_cast<const BundleEntryModifierByResources*>(other.pReplacer.get()) != nullptr);
						message = "There's a conflict between the resource replacers for " + newEntry.pathOrName +
							".\nShould the new replacer be used and the old one be removed?";
					}
					else if (dynamic_cast<BundleReplacer*>(existing.pReplacer.get()) != nullptr)
					{
						BundleReplacer* pExistingReplacer = reinterpret_cast<BundleReplacer*>(existing.pReplacer.get());
						const char* name = pExistingReplacer->GetOriginalEntryName();
						if (name == nullptr) name = pExistingReplacer->GetEntryName();
						if (name == nullptr) name = "";
						assert(dynamic_cast<const BundleReplacer*>(other.pReplacer.get()) != nullptr);
						message = "There's a conflict between the bundle entry replacers for " + newEntry.pathOrName +
							"/<...>/" + name + ".\nShould the new replacer be used and the old one be removed?";
					}
					else if (dynamic_cast<AssetsEntryReplacer*>(existing.pReplacer.get()) != nullptr)
					{
						assert(dynamic_cast<const AssetsEntryReplacer*>(other.pReplacer.get()) != nullptr);
						assert(reinterpret_cast<AssetsEntryReplacer*>(existing.pReplacer.get())->GetPathID()
							== reinterpret_cast<const AssetsEntryReplacer*>(other.pReplacer.get())->GetPathID());
						message = "There's a conflict between the asset replacers for " + newEntry.pathOrName
							+ "/<...>/Path ID "
							+ std::to_string((int64_t)reinterpret_cast<AssetsEntryReplacer*>(existing.pReplacer.get())->GetPathID())
							+ ".\nShould the new replacer be used and the old one be removed?";
					}
					else if (dynamic_cast<AssetsDependenciesReplacer*>(existing.pReplacer.get()) != nullptr)
					{
						assert(dynamic_cast<const AssetsDependenciesReplacer*>(other.pReplacer.get()) != nullptr);
						message = "There's a conflict between the dependency replacers for " + newEntry.pathOrName
							+ ".\nShould the new replacer be used and the old one be removed?";
					}
					else if (dynamic_cast<AssetsReplacer*>(existing.pReplacer.get()) != nullptr)
					{
						assert(dynamic_cast<const AssetsReplacer*>(other.pReplacer.get()) != nullptr);
						message = "There's a conflict between the replacers for " + newEntry.pathOrName
							+ ".\nShould the new replacer be used and the old one be removed?";
					}
					assert(!message.empty());
					if (message.empty())
						return false;
					auto tMessage = unique_MultiByteToTCHAR(message.c_str());
					if (IDYES == MessageBox(hDlg, tMessage.get(), TEXT("Mod Installer Editor"), MB_YESNO))
					{
						HWND hTreeModifications = GetDlgItem(hDlg, IDC_TREECHANGES);
						if (existing.treeItem != NULL && hTreeModifications != NULL)
						{
							TreeView_DeleteItem(hTreeModifications, existing.treeItem);
						}
						existing.treeItem = NULL;
						return true;
					}
					return false;
				};
				existingEntry.mergeWith(newEntry, resolveConflict);
				merged = true;
				break;
			}
		}
		if (!merged)
		{
			this->visibleFiles.push_back(std::move(newEntry));
		}
	}
}

bool Win32ModInstallerEditor::removeChangesBy(VisibleFileEntry &file, HTREEITEM treeItem)
{
	for (size_t i = 0; i < file.replacers.size(); i++)
	{
		if ((HTREEITEM)file.replacers[i].treeItem == treeItem)
		{
			//Delete a single replacer.
			TreeView_DeleteItem(hTreeModifications, treeItem);
			file.replacers.erase(file.replacers.begin() + i);
			return true;
		}
	}
	for (size_t i = 0; i < file.subFiles.size(); ++i)
	{
		if ((HTREEITEM)file.subFiles[i].treeViewEntry == treeItem)
		{
			//Delete all changes in a sub file.
			TreeView_DeleteItem(hTreeModifications, file.subFiles[i].treeViewEntry);
			file.subFiles.erase(file.subFiles.begin() + i);
			return true;
		}
	}
	//Try in deeper levels.
	for (size_t i = 0; i < file.subFiles.size(); ++i)
	{
		if (file.subFiles[i].treeViewEntry != NULL && removeChangesBy(file.subFiles[i], treeItem))
			return true;
	}
	return false;
}
void Win32ModInstallerEditor::RemoveChange(HTREEITEM treeItem)
{
	if (treeItem == NULL)
		return;
	if (treeItem == bundleBaseEntry || treeItem == assetsBaseEntry || treeItem == resourcesBaseEntry)
	{
		//Delete all changes in bundles / assets.
		auto targetType = 
			   (treeItem == bundleBaseEntry) ? FileContext_Bundle
			: ((treeItem == assetsBaseEntry) ? FileContext_Assets : FileContext_Resources);
		for (size_t _i = this->visibleFiles.size(); _i > 0; --_i)
		{
			size_t i = _i - 1;
			if (this->visibleFiles[i].fileType == targetType
				&& this->visibleFiles[i].treeViewEntry != NULL)
			{
				this->changedFlag = true;
				TreeView_DeleteItem(hTreeModifications, this->visibleFiles[i].treeViewEntry);
				this->visibleFiles.erase(this->visibleFiles.begin() + i);
			}
		}
		return;
	}
	for (size_t i = 0; i < this->visibleFiles.size(); ++i)
	{
		//Delete all changes in a base file.
		if ((HTREEITEM)this->visibleFiles[i].treeViewEntry == treeItem)
		{
			this->changedFlag = true;
			TreeView_DeleteItem(hTreeModifications, this->visibleFiles[i].treeViewEntry);
			this->visibleFiles.erase(this->visibleFiles.begin() + i);
			return;
		}
	}
	//Look in all non-top levels (recursively).
	for (size_t i = 0; i < this->visibleFiles.size(); ++i)
	{
		if (this->visibleFiles[i].treeViewEntry != NULL && removeChangesBy(this->visibleFiles[i], treeItem))
		{
			this->changedFlag = true;
			return;
		}
	}
	//Tree item was not found, even though we were passed a non-NULL handle.
	assert(false);
}

//ModInstaller.dll
__declspec(dllimport) bool MakeInstaller(const TCHAR *installerDllPath, InstallerPackageFile *installerData, const TCHAR *outPath, const std::vector<uint8_t> &iconData);

static bool GenerateInstaller(HWND hDlg, InstallerPackageFile &packageFile, const std::wstring &filePath,
	const std::vector<uint8_t> &iconData)
{
	HMODULE hModInstaller = GetModuleHandle(TEXT("ModInstaller.dll"));
	if (!hModInstaller)
		MessageBox(hDlg, TEXT("Unable to locate ModInstaller.dll!"), TEXT("ERROR"), 16);
	else
	{
		std::vector<TCHAR> moduleFileNameBuf(257);
		SetLastError(ERROR_SUCCESS);
		while (true)
		{
			moduleFileNameBuf[moduleFileNameBuf.size() - 1] = 0;
			DWORD written = GetModuleFileName(hModInstaller, moduleFileNameBuf.data(), (DWORD)(moduleFileNameBuf.size() - 1));
			//How too small buffer sizes are indicated : 
			//Win XP : wrote partial string without null-terminator
			//Win Vista+ : ERROR_INSUFFICIENT_BUFFER set
			if ((written > moduleFileNameBuf.size()) || 
				(GetLastError() == ERROR_INSUFFICIENT_BUFFER) || moduleFileNameBuf[written] != 0)
			{
				moduleFileNameBuf.resize(moduleFileNameBuf.size() + 1024);
			}
			else
			{
				moduleFileNameBuf.resize(written + 1);
				moduleFileNameBuf[written] = 0;
				break;
			}
		}
		bool result = MakeInstaller(&moduleFileNameBuf[0], &packageFile, filePath.c_str(), iconData);
		return result;
	}
	return false;
}

bool Win32ModInstallerEditor::SaveChanges()
{
	InstallerPackageFile packageFile;
	packageFile.addedTypes = this->typesToExport;

	HWND hEditModName = GetDlgItem(hDlg, IDC_EMODNAME);
	HWND hEditAuthors = GetDlgItem(hDlg, IDC_EAUTHORS);
	HWND hEditDescription = GetDlgItem(hDlg, IDC_EDESCRIPTION);

	{
		size_t modNameLen = (size_t)Edit_GetTextLength(hEditModName);
		std::vector<TCHAR> tModName(modNameLen + 1);
		Edit_GetText(hEditModName, tModName.data(), (int)(tModName.size()));
		tModName[modNameLen] = 0;
		auto mb = unique_TCHARToMultiByte(tModName.data());
		packageFile.modName = mb.get();
	}
	{
		size_t authorsLen = (size_t)Edit_GetTextLength(hEditAuthors);
		std::vector<TCHAR> tAuthors(authorsLen + 1);
		Edit_GetText(hEditAuthors, tAuthors.data(), (int)(tAuthors.size()));
		tAuthors[authorsLen] = 0;
		auto mb = unique_TCHARToMultiByte(tAuthors.data());
		packageFile.modCreators = mb.get();
	}
	{
		size_t descriptionLen = (size_t)Edit_GetTextLength(hEditDescription);
		std::vector<TCHAR> tDescriptions(descriptionLen + 1);
		Edit_GetText(hEditDescription, tDescriptions.data(), (int)(tDescriptions.size()));
		tDescriptions[descriptionLen] = 0;
		auto mb = unique_TCHARToMultiByte(tDescriptions.data());
		packageFile.modDescription = mb.get();
	}
	//#ifdef __X64
	//if (this->saveType == ModDataSaveType_Installer)
	//{
	//	DWORD result = MessageBox(
	//		hDlg, 
	//		TEXT("The installer will only be usable on 64bit systems. The 32bit release of UABE creates installers that work on both.\n")\
	//		TEXT("Do you want to proceed? Press \"No\" to create an installer package that can be opened to create a 32bit installer."), 
	//		TEXT("Asset Bundle Extractor"), 
	//		MB_YESNOCANCEL);
	//	switch (result)
	//	{
	//	case IDYES:
	//		break;
	//	case IDNO:
	//		this->saveType = ModDataSaveType_PackageFile;
	//		break;
	//	case IDCANCEL:
	//	default:
	//		return false;
	//	}
	//}
	//#endif
	WCHAR *filePathW = NULL;
	if (FAILED(ShowFileSaveDialog(hDlg, &filePathW, 
		(this->saveType == ModDataSaveType_Installer) ? L"*.exe|Standalone Mod Installer:"
		: L"*.emip|UABE Mod Installer Package:",
		nullptr, nullptr, nullptr, UABE_FILEDIALOG_FILE_GUID)))
		return false;

	std::wstring filePath = std::wstring(filePathW);
	std::wstring fileExtension = (this->saveType == ModDataSaveType_Installer) ? L".exe" : L".emip";
	if (filePath.size() >= fileExtension.size()
		&& 0==filePath.compare(filePath.size() - fileExtension.size(), fileExtension.size(), fileExtension))
	{}
	else
		filePath += fileExtension;
	FreeCOMFilePathBuf(&filePathW);

	HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
	HWND hTreeChanges = GetDlgItem(hDlg, IDC_TREECHANGES);
	if (!hEditBaseFolder || !hTreeChanges)
		return false;
	size_t baseDirLen = (size_t)Edit_GetTextLength(hEditBaseFolder);
	packageFile.affectedAssets.resize(this->visibleFiles.size());
	std::vector<TCHAR> pathBuffer;

	//Fill in the relative paths.
	for (size_t i = 0; i < this->visibleFiles.size(); i++)
	{
		assert(!this->visibleFiles[i].pathOrName.empty() && !this->visibleFiles[i].pathNull);
		assert(this->visibleFiles[i].treeViewEntry != NULL);
		if (this->visibleFiles[i].pathOrName.empty() || this->visibleFiles[i].pathNull || this->visibleFiles[i].treeViewEntry == NULL)
			continue;
		size_t newPathLen = baseDirLen + this->visibleFiles[i].pathOrName.size() + 1;
		if (newPathLen <= MAX_PATH) newPathLen = MAX_PATH + 1;
			
		//Retrieve the relative paths from the TreeView.
		TVITEMEX itemex;
		do {
			if (newPathLen >= INT_MAX-1) newPathLen = INT_MAX-2;
			if (pathBuffer.size() < newPathLen) pathBuffer.resize(newPathLen);
			itemex.hItem = (HTREEITEM)this->visibleFiles[i].treeViewEntry;
			itemex.mask = TVIF_HANDLE | TVIF_TEXT;
			itemex.pszText = pathBuffer.data();
			itemex.cchTextMax = (int)pathBuffer.size();
			TreeView_GetItem(hTreeChanges, &itemex);
			pathBuffer[pathBuffer.size() - 1] = 0;
			newPathLen += MAX_PATH;
		} while (newPathLen < INT_MAX-2 && _tcslen(itemex.pszText) >= pathBuffer.size() - 1);

		auto newPath8 = unique_TCHARToMultiByte(pathBuffer.data());
		packageFile.affectedAssets[i].path = newPath8.get();
	}
	//Set the replacers.
	for (size_t i = 0; i < this->visibleFiles.size(); i++)
	{
		InstallerPackageAssetsDesc &packageFileDesc = packageFile.affectedAssets[i];
		VisibleFileEntry &visibleFileDesc = this->visibleFiles[i];

		packageFileDesc.replacers.reserve(visibleFileDesc.replacers.size() + visibleFileDesc.subFiles.size());
		for (size_t i = 0; i < visibleFileDesc.replacers.size(); ++i)
			packageFileDesc.replacers.push_back(visibleFileDesc.replacers[i].pReplacer);

		switch (visibleFileDesc.fileType)
		{
		case FileContext_Assets:
			packageFileDesc.type = InstallerPackageAssetsType::Assets;
			assert(visibleFileDesc.subFiles.empty());
			break;
		case FileContext_Bundle:
			packageFileDesc.type = InstallerPackageAssetsType::Bundle;
			for (size_t i = 0; i < visibleFileDesc.subFiles.size(); ++i)
				packageFileDesc.replacers.push_back(visibleFileDesc.subFiles[i].produceBundleReplacer());
			break;
		case FileContext_Resources:
			packageFileDesc.type = InstallerPackageAssetsType::Resources;
			assert(visibleFileDesc.subFiles.empty());
			assert(packageFileDesc.replacers.size() == 1);
			break;
		default:
			assert(false);
		}
	}
	//Free invalid file entries.
	for (size_t _i = packageFile.affectedAssets.size(); _i > 0; --_i)
	{
		size_t i = _i - 1;
		if (packageFile.affectedAssets[i].path.empty())
			packageFile.affectedAssets.erase(packageFile.affectedAssets.begin() + i);
	}

	if (this->saveType == ModDataSaveType_Installer)
	{
		return GenerateInstaller(hDlg, packageFile, filePath, this->iconData);
	}
	else
	{
		bool success = false;
		IAssetsWriter *pOutputWriter = Create_AssetsWriterToFile(filePath.c_str(), true, true, RWOpenFlags_Immediately);

		if (pOutputWriter)
		{
			QWORD filePos = 0;
			if (!packageFile.Write(filePos, pOutputWriter))
				MessageBox(hDlg, TEXT("An error occured while writing the package file!"), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
			else
				success = true;
			Free_AssetsWriter(pOutputWriter);
		}
		else
			MessageBox(hDlg, TEXT("Unable to open the output file!"), TEXT("Asset Bundle Extractor"), MB_ICONERROR);
		return success;
	}
}

static void FreePostDialogProc(HWND hDlg)
{
	HWND hIIcon = GetDlgItem(hDlg, IDC_IICON);
	if (hIIcon)
	{
		HICON hOldIcon = (HICON)SendMessage(hIIcon, STM_GETIMAGE, IMAGE_ICON, NULL);
		if (hOldIcon)
			DestroyIcon(hOldIcon);
	}
}

void Win32ModInstallerEditor::AskSave()
{
	if ((this->visibleFiles.empty() || !this->changedFlag)
		&& Edit_GetTextLength(GetDlgItem(hDlg, IDC_EMODNAME)) == 0
		&& Edit_GetTextLength(GetDlgItem(hDlg, IDC_EAUTHORS)) == 0
		&& Edit_GetTextLength(GetDlgItem(hDlg, IDC_EDESCRIPTION)) == 0)
	{
		EndDialog(hDlg, 0);
		FreePostDialogProc(hDlg);
	}
	else if (this->saveType == ModDataSaveType_PackageFile //No question for saving to a package file needed.
		|| this->visibleFiles.empty() || !this->changedFlag) //Text fields changed.
	{
		switch (MessageBox(hDlg, 
				TEXT("Are you sure you want to discard the progress?"),
				TEXT("Asset Bundle Extractor"),
				MB_YESNO))
		{
		case IDNO:
			break;
		case IDYES:
		default:
			EndDialog(hDlg, 0);
			FreePostDialogProc(hDlg);
			break;
		}
	}
	else
	{
		switch (MessageBox(hDlg, 
				TEXT("Are you sure you want to discard the progress?\n")\
				TEXT("To use the changes inside UABE, you need to save them to an installer package first. Press \"No\" to do that."), 
				TEXT("Asset Bundle Extractor"), 
				MB_YESNOCANCEL))
		{
		case IDNO:
			this->saveType = ModDataSaveType_PackageFile;
			if (!this->SaveChanges())
				break;
		case IDYES:
			EndDialog(hDlg, 0);
			FreePostDialogProc(hDlg);
		case IDCANCEL:
		default:
			break;
		}
	}
}

INT_PTR CALLBACK Win32ModInstallerEditor::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	Win32ModInstallerEditor *pThis = reinterpret_cast<Win32ModInstallerEditor*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
		case WM_INITDIALOG:
			{
				SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
				pThis = reinterpret_cast<Win32ModInstallerEditor*>(lParam);
				pThis->hDlg = hDlg;
				pThis->hTreeModifications = GetDlgItem(hDlg, IDC_TREECHANGES);
				if (pThis->hTreeModifications == NULL)
				{
					EndDialog(hDlg, (INT_PTR)0);
					break;
				}
				pThis->UpdateModsTree();
				SetWindowPos(hDlg, NULL, 0, 0, 510, 390, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
				pThis->iconData.clear();
				switch (pThis->saveType)
				{
					case ModDataSaveType_PackageFile:
						ShowWindow(GetDlgItem(hDlg, IDC_SICON), SW_HIDE);
						ShowWindow(GetDlgItem(hDlg, IDC_IICON), SW_HIDE);
						ShowWindow(GetDlgItem(hDlg, IDC_BTNLOADICON), SW_HIDE);
						SetWindowText(hDlg, TEXT("Create an installer package"));
						break;
					case ModDataSaveType_Installer:
					{
						HMODULE hModule = GetModuleHandle(NULL);
						HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(IDI_ASSETBUNDLEEXTRACTOR), MAKEINTRESOURCE(3));
						if (hResource)
						{
							DWORD resourceSize = SizeofResource(hModule, hResource);
							HGLOBAL resourceHandle = LoadResource(hModule, hResource);
							if (resourceHandle)
							{
								PVOID pResource = LockResource(resourceHandle);
								uint8_t *pBuf = reinterpret_cast<uint8_t*>(pResource);
								size_t size = resourceSize;
								pThis->iconData.assign(pBuf, pBuf + size);
							}
						}
						HICON hIcon = (HICON)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ASSETBUNDLEEXTRACTOR));
						HWND hIIcon = GetDlgItem(hDlg, IDC_IICON);
						SendMessage(hIIcon, STM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
					}
					break;
				}
			}
			return (INT_PTR)TRUE;
		case WM_SIZE:
			{
				int width = LOWORD(lParam); int height = HIWORD(lParam);
				SetWindowPos(GetDlgItem(hDlg, IDC_EMODNAME), NULL, 90, 23, width / 2 - 95, 23, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_EAUTHORS), NULL, 90, 54, width / 2 - 95, 23, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_EDESCRIPTION), NULL, 90, 93, width / 2 - 95, height - 146, SWP_NOZORDER | SWP_NOACTIVATE);
				//if (userData->saveType != ModDataSaveType_Installer)
				{
					SetWindowPos(GetDlgItem(hDlg, IDC_IICON), NULL, 90, height - 50, 32, 32, SWP_NOZORDER | SWP_NOACTIVATE); 
					SetWindowPos(GetDlgItem(hDlg, IDC_BTNLOADICON), NULL, 130, height - 45, (width / 2) - 135, 21, SWP_NOZORDER | SWP_NOACTIVATE); 
					SetWindowPos(GetDlgItem(hDlg, IDC_SICON), NULL, 9, height - 55, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); 
				}
				SetWindowPos(GetDlgItem(hDlg, IDC_SBASEFOLDER), NULL, width / 2 + 5, 23, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_EBASEFOLDER), NULL, width / 2 + 5, 54, (int)((float)(width / 2 - 22) * 0.64), 23, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_BTNBASEFOLDER), NULL, (int)((float)width / 2.0 * 1.64), 55, (int)((float)(width / 2 - 22) * 0.36), 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_SCHANGES), NULL, width / 2 + 5, 84, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_TREECHANGES), NULL, width / 2 + 5, 102, width / 2 - 13, height - 168, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_BTNIMPORTPAK), NULL, width / 2 + 5, height - 64, (int)(((float)width / 2.0 - 25.0) / 2.0), 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDC_BTNREMCHANGE), NULL, (int)((float)width * 0.75), height - 64, (int)(((float)width / 2.0 - 25.0) / 2.0), 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDOK), NULL, width / 2 + 5, height - 33, (int)(((float)width / 2.0 - 74.0) / 2.0), 21, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hDlg, IDCANCEL), NULL, (int)((float)width * 0.75 + 24.0), height - 33, (int)(((float)width / 2.0 - 74.0) / 2.0), 21, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			return (INT_PTR)TRUE;
		case WM_CLOSE:
			pThis->AskSave();
			return (INT_PTR)TRUE;
		case WM_DESTROY:
			EndDialog(hDlg, LOWORD(wParam));
			FreePostDialogProc(hDlg);
			break;
		case WM_COMMAND:
			wmId    = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			switch (wmId)
			{
				case IDC_EBASEFOLDER:
					if (!pThis)
						break;
					pThis->UpdateDisplayedRelPaths();
					return (INT_PTR)TRUE;
				case IDC_BTNBASEFOLDER:
					if (!pThis)
						break;
					{
						HWND hEditBaseFolder = GetDlgItem(hDlg, IDC_EBASEFOLDER);
						WCHAR *folderPath = NULL;
						if (hEditBaseFolder && ShowFolderSelectDialog(hDlg, &folderPath, L"Select a base directory", UABE_FILEDIALOG_FILE_GUID))
						{
							SetWindowTextW(hEditBaseFolder, folderPath);
							FreeCOMFilePathBuf(&folderPath);
						}
						return (INT_PTR)TRUE;
					}
				case IDC_BTNLOADICON:
					if (!pThis)
						break;
					pThis->SelectAndLoadIcon();
					return (INT_PTR)TRUE;
				case IDC_BTNIMPORTPAK:
					if (!pThis)
						break;
					pThis->SelectAndImportPackage();
					return (INT_PTR)TRUE;
				case IDC_BTNREMCHANGE:
					if (!pThis)
						break;
					{
						HWND hTreeChanges = GetDlgItem(hDlg, IDC_TREECHANGES);
						if (!hTreeChanges)
							break;
						HTREEITEM selection = TreeView_GetSelection(hTreeChanges);
						pThis->RemoveChange(selection);
						return (INT_PTR)TRUE;
					}
				case IDOK:
					if (!pThis)
						break;
					if (pThis->SaveChanges())
					{
						EndDialog(hDlg, LOWORD(wParam));
						FreePostDialogProc(hDlg);
					}
					return (INT_PTR)TRUE;
				case IDCANCEL:
					if (!pThis)
					{
						EndDialog(hDlg, LOWORD(wParam));
						FreePostDialogProc(hDlg);
						break;
					}
					pThis->AskSave();
					return (INT_PTR)TRUE;
			}
			break;
	}
	return (INT_PTR)FALSE;
}

Win32ModInstallerEditor::Win32ModInstallerEditor(Win32AppContext &appContext,
	std::vector<std::shared_ptr<FileContextInfo>> &contextInfo,
	EModDataSaveType saveType)
	: Win32ModTreeDialogBase(appContext), saveType(saveType), changedFlag(false)
{
	for (size_t i = 0; i < contextInfo.size(); ++i)
	{
		if (contextInfo[i] != nullptr && contextInfo[i]->getFileContext() != nullptr && contextInfo[i]->getParentFileID() == 0
			&& contextInfo[i]->hasAnyChanges(appContext))
			this->visibleFiles.push_back(VisibleFileEntry(appContext, contextInfo[i]));
	}

	//Copy relevant Unity basic types, assuming that we only have one class database across all files.
	//-> Assumption is not always correct.
	struct {
		void operator()(VisibleFileEntry &file, std::unordered_set<int> &classIDs, std::shared_ptr<ClassDatabaseFile> &pFoundClassDatabase)
		{
			if (file.fileType == FileContext_Assets && file.pContextInfo != nullptr)
			{
				if (!pFoundClassDatabase)
					pFoundClassDatabase = reinterpret_cast<AssetsFileContextInfo*>(file.pContextInfo.get())->GetClassDatabase();
				for (size_t i = 0; i < file.replacers.size(); ++i)
				{
					auto *pReplacer = reinterpret_cast<AssetsReplacer*>(file.replacers[i].pReplacer.get());
					std::shared_ptr<ClassDatabaseFile> typeDbFile; ClassDatabaseType *pType;
					if (pReplacer != nullptr
						&& pReplacer->GetType() == AssetsReplacement_AddOrModify)
					{
						AssetsEntryReplacer* pEntryReplacer = reinterpret_cast<AssetsEntryReplacer*>(pReplacer);
						if (pEntryReplacer->GetClassID() >= 0 && !pEntryReplacer->GetTypeInfo(typeDbFile, pType))
							classIDs.insert(pEntryReplacer->GetClassID());
					}
				}
			}
			for (size_t i = 0; i < file.subFiles.size(); ++i)
				(*this)(file.subFiles[i], classIDs, pFoundClassDatabase);
		}
	} enumerateClassIDs;
	std::shared_ptr<ClassDatabaseFile> pFoundClassDatabase;
	std::unordered_set<int> classIDs;
	for (size_t i = 0; i < visibleFiles.size(); ++i)
		enumerateClassIDs(visibleFiles[i], classIDs, pFoundClassDatabase);
	if (pFoundClassDatabase != nullptr)
	{
		for (size_t i = 0; i < pFoundClassDatabase->classes.size(); i++)
		{
			if (classIDs.find(pFoundClassDatabase->classes[i].classId) != classIDs.end())
			{
				typesToExport.InsertFrom(pFoundClassDatabase.get(), &pFoundClassDatabase->classes[i]);
			}
		}
	}
}

void Win32ModInstallerEditor::open()
{
	DialogBoxParam(appContext.getMainWindow().getHInstance(),
		MAKEINTRESOURCE(IDD_MAKEINSTALLER), appContext.getMainWindow().getWindow(),
		DialogProc,
		(LPARAM)this);
}

