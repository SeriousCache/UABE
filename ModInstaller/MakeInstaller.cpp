#include "stdafx.h"
#include <stdio.h>
#include <tchar.h>
#include <malloc.h>
#include <assert.h>
#include "InstallerDataFormat.h"
#include "ModInstaller.h"
#include "MakeIconResource.h"
#include <algorithm>

DWORD GetFileOffsetFromRVA(IMAGE_DOS_HEADER *pDosHeader, DWORD rva)
{
	IMAGE_NT_HEADERS *pNtHeaders = (IMAGE_NT_HEADERS*)((uintptr_t)pDosHeader + pDosHeader->e_lfanew);
	IMAGE_SECTION_HEADER *pSectionHeaders = (IMAGE_SECTION_HEADER*)((uintptr_t)pNtHeaders + 
		sizeof(IMAGE_NT_SIGNATURE) + sizeof(IMAGE_FILE_HEADER) + pNtHeaders->FileHeader.SizeOfOptionalHeader);
	for (unsigned int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++)
	{
		if (rva >= pSectionHeaders[i].VirtualAddress && rva < (pSectionHeaders[i].VirtualAddress + pSectionHeaders[i].SizeOfRawData))
		{
			return rva - pSectionHeaders[i].VirtualAddress + pSectionHeaders[i].PointerToRawData;
		}
	}
	return 0;
}
//searches an export in a not mapped module (directly loaded as a file)
//Assumes that all section headers are in bounds.
DWORD SearchExportByName_RawModule(PIMAGE_DOS_HEADER pModule, size_t fileSize, const char *name)
{
	PIMAGE_NT_HEADERS32 pNTHeaders32 = (PIMAGE_NT_HEADERS32)&((uint8_t*)pModule)[pModule->e_lfanew];
	PIMAGE_EXPORT_DIRECTORY pExportsDirectory;
	if (pNTHeaders32->FileHeader.Machine == 0x014C)
	{
		DWORD offset = GetFileOffsetFromRVA(pModule, pNTHeaders32->OptionalHeader.DataDirectory[0].VirtualAddress);
		if (!offset)
			return 0;
		pExportsDirectory = (PIMAGE_EXPORT_DIRECTORY)&((uint8_t*)pModule)[offset];
	}
	else if (pNTHeaders32->FileHeader.Machine == 0x8664) //is AMD64 / 64bit
	{
		PIMAGE_NT_HEADERS64 pNTHeaders64 = (PIMAGE_NT_HEADERS64)pNTHeaders32;
		DWORD offset = GetFileOffsetFromRVA(pModule, pNTHeaders64->OptionalHeader.DataDirectory[0].VirtualAddress);
		if (!offset)
			return 0;
		pExportsDirectory = (PIMAGE_EXPORT_DIRECTORY)&((uint8_t*)pModule)[offset];
	}
	else
		return 0;
	if ((uintptr_t)((uint8_t*)pExportsDirectory - (uint8_t*)pModule) >= fileSize
		|| (uintptr_t)((uint8_t*)&pExportsDirectory[1] - (uint8_t*)pModule) > fileSize)
		return 0;
	DWORD nameTableOffset = GetFileOffsetFromRVA(pModule, pExportsDirectory->AddressOfNames);
	DWORD nameOrdinalTableOffset = GetFileOffsetFromRVA(pModule, pExportsDirectory->AddressOfNameOrdinals);
	DWORD funcTableOffset = GetFileOffsetFromRVA(pModule, pExportsDirectory->AddressOfFunctions);
	if ((!nameTableOffset) || (!nameOrdinalTableOffset) || (!funcTableOffset))
		return 0;
	DWORD *nameTable = (DWORD*)&((uint8_t*)pModule)[nameTableOffset];
	if (nameTableOffset >= fileSize || (uint64_t)(nameTableOffset + pExportsDirectory->NumberOfNames * sizeof(DWORD)) > (uint64_t)fileSize)
		return 0;
	uint16_t *nameOrdinalTable = (uint16_t*)&((uint8_t*)pModule)[nameOrdinalTableOffset];
	if (nameOrdinalTableOffset >= fileSize || (uint64_t)(nameOrdinalTableOffset + pExportsDirectory->NumberOfNames * sizeof(uint16_t)) > (uint64_t)fileSize)
		return 0;
	DWORD *funcTable = (DWORD*)&((uint8_t*)pModule)[funcTableOffset];
	if (funcTableOffset >= fileSize || (uint64_t)(funcTableOffset + pExportsDirectory->NumberOfFunctions * sizeof(DWORD)) > (uint64_t)fileSize)
		return 0;
	for (DWORD i = 0; i < pExportsDirectory->NumberOfNames; i++)
	{
		if (nameTable[i] != 0)
		{
			DWORD nameOffset = GetFileOffsetFromRVA(pModule, nameTable[i]);
			if ((nameOffset != 0) && !strcmp((char*)&((uint8_t*)pModule)[nameOffset], name))
			{
				//The name ordinal table actually contains indices into the function table (name index -> function ordinal)
				DWORD functionIndex = nameOrdinalTable[i]; 
				if (functionIndex < pExportsDirectory->NumberOfFunctions)
				{
					return funcTable[functionIndex];
				}
			}
		}
	}
	return 0;
}

static IMAGE_RESOURCE_DIRECTORY_ENTRY *FindResourceDirEntry(IMAGE_DOS_HEADER *pModule, size_t fileSize, DWORD resourceBaseOffset, std::vector<uint16_t> idPath)
{
	IMAGE_RESOURCE_DIRECTORY *pDir = (PIMAGE_RESOURCE_DIRECTORY)&((uint8_t*)pModule)[resourceBaseOffset];
	while (pDir != nullptr && !idPath.empty())
	{
		if ((uintptr_t)pDir - (uintptr_t)pModule >= fileSize || ((uintptr_t)&pDir[1] - (uintptr_t)pModule) > fileSize)
			return nullptr;
		IMAGE_RESOURCE_DIRECTORY_ENTRY *pBeginEntry = &((IMAGE_RESOURCE_DIRECTORY_ENTRY*)&pDir[1])[pDir->NumberOfNamedEntries];
		IMAGE_RESOURCE_DIRECTORY_ENTRY *pEndEntry = &pBeginEntry[pDir->NumberOfIdEntries];
		if ((uintptr_t)pBeginEntry - (uintptr_t)pModule >= fileSize || ((uintptr_t)pEndEntry - (uintptr_t)pModule) > fileSize)
			return nullptr;
		//Note: The resource IDs are actually sorted, so a binary search would be possible.
		// We have only ~4 entries to search through for the UABE installer, so a linear search is good enough.
		for (IMAGE_RESOURCE_DIRECTORY_ENTRY *pCurEntry = pBeginEntry; pCurEntry != pEndEntry; ++pCurEntry)
		{
			if (!pCurEntry->NameIsString && pCurEntry->Id == idPath.front())
			{
				idPath.erase(idPath.begin());
				if (idPath.empty())
					return pCurEntry;
				if (!pCurEntry->DataIsDirectory)
					return nullptr;
				pDir = (PIMAGE_RESOURCE_DIRECTORY)&((uint8_t*)pModule)[resourceBaseOffset + pCurEntry->OffsetToDirectory];
				break;
			}
		}
	}
	return nullptr;
}

//Also checks that all section headers are in bounds.
bool ConvertToEXE(IMAGE_DOS_HEADER *pDLL, size_t fileSize)
{
	IMAGE_NT_HEADERS32 *pNtHeaders = (IMAGE_NT_HEADERS32*)((uintptr_t)pDLL + pDLL->e_lfanew);
	if (pDLL->e_lfanew > fileSize || (pDLL->e_lfanew + sizeof(IMAGE_NT_SIGNATURE) + sizeof(IMAGE_FILE_HEADER)) > fileSize
		|| (pDLL->e_lfanew + sizeof(IMAGE_NT_SIGNATURE) + sizeof(IMAGE_FILE_HEADER) + pNtHeaders->FileHeader.SizeOfOptionalHeader) > fileSize)
		return false;
	IMAGE_SECTION_HEADER *pSectionHeaders = (IMAGE_SECTION_HEADER*)((uintptr_t)pNtHeaders + 
		sizeof(IMAGE_NT_SIGNATURE) + sizeof(IMAGE_FILE_HEADER) + pNtHeaders->FileHeader.SizeOfOptionalHeader);
	if (fileSize < (DWORD)pNtHeaders->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)
		|| (uintptr_t)((uint8_t*)pSectionHeaders - (uint8_t*)pDLL) > (fileSize - (DWORD)pNtHeaders->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
		return false;

	pNtHeaders->FileHeader.Characteristics &= ~0x2000; //disable dll flag
	//DWORD winMainOffs = (DWORD)((uintptr_t)GetProcAddress(hModule, "_tWinMain") - (uintptr_t)hModule);
	DWORD winMainRVA = SearchExportByName_RawModule(pDLL, fileSize, "_WinMain");
	if (!winMainRVA)
		return false;
	//patch entry point
	if (pNtHeaders->FileHeader.Machine == 0x014C) //is i386 / 32bit
		pNtHeaders->OptionalHeader.AddressOfEntryPoint = winMainRVA;
	else if (pNtHeaders->FileHeader.Machine == 0x8664) //is AMD64 / 64bit
		((IMAGE_NT_HEADERS64*)pNtHeaders)->OptionalHeader.AddressOfEntryPoint = winMainRVA;
	
	//Find the resources directory.
	DWORD resourceDirOffset = 0;
	if (pNtHeaders->FileHeader.Machine == 0x014C) //is i386 / 32bit
	{
		if (((uintptr_t)&pNtHeaders->OptionalHeader.DataDirectory[3] - (uintptr_t)pDLL) >= fileSize)
			return false;
		resourceDirOffset = GetFileOffsetFromRVA(pDLL, pNtHeaders->OptionalHeader.DataDirectory[2].VirtualAddress);
	}
	else if (pNtHeaders->FileHeader.Machine == 0x8664) //is AMD64 / 64bit
	{
		PIMAGE_NT_HEADERS64 pNTHeaders64 = (PIMAGE_NT_HEADERS64)pNtHeaders;
		if (((uintptr_t)&pNTHeaders64->OptionalHeader.DataDirectory[3] - (uintptr_t)pDLL) >= fileSize)
			return false;
		resourceDirOffset = GetFileOffsetFromRVA(pDLL, pNTHeaders64->OptionalHeader.DataDirectory[2].VirtualAddress);
	}
	else
		return false;
	if (resourceDirOffset == 0)
		return false;
	
	//Fix the manifest resource ID:
	//ISOLATIONAWARE_MANIFEST_RESOURCE_ID is the manifest resource ID for dlls, 
	//CREATEPROCESS_MANIFEST_RESOURCE_ID is the manifest resource ID for exes.
	//-> Change the manifest resource ID, since Windows otherwise would not find the manifest upon loading the process.
	//The manifest is required to make Windows load the 6.0+ comctrl.dll instead of the pre-XP one that lacks features and looks outdated.
	std::vector<uint16_t> idPath(2); idPath[0] = (uint16_t)RT_MANIFEST; idPath[1] = (uint16_t)ISOLATIONAWARE_MANIFEST_RESOURCE_ID;
	IMAGE_RESOURCE_DIRECTORY_ENTRY *pSearchedEntry = FindResourceDirEntry(pDLL, fileSize, resourceDirOffset, std::move(idPath));
	if (pSearchedEntry == nullptr)
		return false;
	pSearchedEntry->Id = (uint16_t)CREATEPROCESS_MANIFEST_RESOURCE_ID;

	return true;
}

//Shifts the imports from MSVC++ redist after the import directories' null terminator.
//-> The PE loader will not resolve these imports by itself so they can be resolved by the .exe later.
//Also gives the new .exe pointers to the new import descriptors.
//Assumes that all section headers are in bounds.
int CreateLateResolveImports(IMAGE_DOS_HEADER *pModule, size_t fileSize)
{
	PIMAGE_NT_HEADERS32 pNTHeaders32 = (PIMAGE_NT_HEADERS32)&((uint8_t*)pModule)[pModule->e_lfanew];
	IMAGE_IMPORT_DESCRIPTOR *importDir; DWORD importDirRVA;
	bool isAMD64 = false;
	if (pNTHeaders32->FileHeader.Machine == 0x014C)
	{
		DWORD offset = GetFileOffsetFromRVA(pModule, pNTHeaders32->OptionalHeader.DataDirectory[1].VirtualAddress);
		if (!offset)
			return -1;
		importDirRVA = pNTHeaders32->OptionalHeader.DataDirectory[1].VirtualAddress;
		importDir = (IMAGE_IMPORT_DESCRIPTOR*)&((uint8_t*)pModule)[offset];
	}
	else if (pNTHeaders32->FileHeader.Machine == 0x8664) //is AMD64 / 64bit
	{
		isAMD64 = true;
		PIMAGE_NT_HEADERS64 pNTHeaders64 = (PIMAGE_NT_HEADERS64)pNTHeaders32;
		DWORD offset = GetFileOffsetFromRVA(pModule, pNTHeaders64->OptionalHeader.DataDirectory[1].VirtualAddress);
		if (!offset)
			return -1;
		importDirRVA = pNTHeaders64->OptionalHeader.DataDirectory[1].VirtualAddress;
		importDir = (IMAGE_IMPORT_DESCRIPTOR*)&((uint8_t*)pModule)[offset];
	}
	else
		return -1;
	if ((uintptr_t)((uint8_t*)importDir - (uint8_t*)pModule) >= fileSize || (uintptr_t)((uint8_t*)&importDir[1] - (uint8_t*)pModule) > fileSize
		|| fileSize < sizeof(IMAGE_IMPORT_DESCRIPTOR))
		return -1;

	IMAGE_IMPORT_DESCRIPTOR *curImportDir = importDir;
	bool patchedMSVCR = false, patchedAssetsTools = false;

	static constexpr size_t maxNumDelayedImports = 32;
	std::vector<DWORD> patchImportDirOffsets;
	while (curImportDir	&& curImportDir->Name != 0)
	{
		DWORD offset = GetFileOffsetFromRVA(pModule, curImportDir->Name);
		if (!offset || offset >= fileSize)
			continue;
		const char *moduleName = (const char*)&((uint8_t*)pModule)[offset];
		if (!strncmp(moduleName, "MSVCR", 5) || !strncmp(moduleName, "MSVCP", 5) || !strncmp(moduleName, "api-ms", 5)
			|| !strncmp(moduleName, "VCRUNTIME", 9) || !strncmp(moduleName, "ucrt", 4))
		{
			patchImportDirOffsets.push_back((DWORD)((uintptr_t)curImportDir - (uintptr_t)pModule));
		}
		{
			/*DWORD offset = GetFileOffsetFromRVA(pModule, curImportDir->OriginalFirstThunk);
			if (!offset)
				continue;
			IMAGE_THUNK_DATA *pThunkData = (IMAGE_THUNK_DATA*)&((uint8_t*)pModule)[offset];*/
		}
		++curImportDir;
	}
	//Check bounds for null-terminating import descriptor.
	if (curImportDir && (uintptr_t)((uint8_t*)curImportDir - (uint8_t*)pModule) > fileSize - sizeof(IMAGE_IMPORT_DESCRIPTOR))
		return -1;
	if (patchImportDirOffsets.size() > maxNumDelayedImports)
		patchImportDirOffsets.resize(maxNumDelayedImports);

	for (size_t i = 0; i < patchImportDirOffsets.size(); i++)
	{
		//Move the current import descriptor past the null terminator.
		IMAGE_IMPORT_DESCRIPTOR *curImportDir = ((IMAGE_IMPORT_DESCRIPTOR*)&((uint8_t*)pModule)[patchImportDirOffsets[i]]);
		IMAGE_IMPORT_DESCRIPTOR tempCopy = *curImportDir;
		IMAGE_IMPORT_DESCRIPTOR *lastImportDir;
		do
		{
			lastImportDir = curImportDir;
			curImportDir = (IMAGE_IMPORT_DESCRIPTOR*)&((uint8_t*)curImportDir)[sizeof(IMAGE_IMPORT_DESCRIPTOR)];
			memcpy(lastImportDir, curImportDir, sizeof(IMAGE_IMPORT_DESCRIPTOR));
		} while (curImportDir && curImportDir->Name);
		memcpy(curImportDir, &tempCopy, sizeof(IMAGE_IMPORT_DESCRIPTOR));
		//Correct the offsets for the other descriptors that were just shifted in the opposite direction.
		//-> Note: patchImportDirOffsets[0..i-1] are already moved past the null terminator, and hence are not touched again.
		for (size_t k = i+1; k < patchImportDirOffsets.size(); k++)
		{
			if (patchImportDirOffsets[k] > patchImportDirOffsets[i])
				patchImportDirOffsets[k] -= sizeof(IMAGE_IMPORT_DESCRIPTOR);
		}
		//Correct the offset for the current descriptor.
		patchImportDirOffsets[i] = (DWORD)( ((uintptr_t)curImportDir - (uintptr_t)importDir) + (DWORD)importDirRVA);
	}
	DWORD importDirRVABack_RVA = SearchExportByName_RawModule(pModule, fileSize, "delayResolveImportRVAs");
	if (!importDirRVABack_RVA)
		return -2;
	DWORD importDirRVABack_Offset = GetFileOffsetFromRVA(pModule, importDirRVABack_RVA);
	if (!importDirRVABack_Offset || importDirRVABack_Offset >= fileSize || importDirRVABack_Offset + patchImportDirOffsets.size() > fileSize)
		return -2;
	memcpy(&((uint8_t*)pModule)[importDirRVABack_Offset], patchImportDirOffsets.data(), patchImportDirOffsets.size() * sizeof(DWORD));
	return 0;
}

__declspec(dllexport) bool MakeInstaller(const TCHAR *installerDllPath, InstallerPackageFile *installerData, const TCHAR *outPath, const std::vector<uint8_t> &iconData)
{
	std::unique_ptr<IAssetsReader> pDllReader(Create_AssetsReaderFromFile(installerDllPath, true, RWOpenFlags_Immediately));
	if (!pDllReader)
		return false;

	pDllReader->Seek(AssetsSeek_End, 0);
	QWORD fileSize = 0;
	pDllReader->Tell(fileSize);
	fileSize = (size_t)fileSize;
	pDllReader->Seek(AssetsSeek_Begin, 0);
	std::vector<uint8_t> fileBuf(fileSize+2); //Two extra bytes as a 'security' null terminator for strings.
	pDllReader->Read(fileSize, fileBuf.data());
	pDllReader.reset();

	if (!ConvertToEXE((IMAGE_DOS_HEADER*)fileBuf.data(), fileSize))
		return false;
	if (CreateLateResolveImports((IMAGE_DOS_HEADER*)fileBuf.data(), fileSize) < 0)
		return false;
	
	std::unique_ptr<IAssetsWriter> pExeWriter(Create_AssetsWriterToFile(outPath, true, true, RWOpenFlags_Immediately));
	if (!pExeWriter)
		return false;
	pExeWriter->Write(fileSize, fileBuf.data());
	pExeWriter.reset();
	fileBuf = std::vector<uint8_t>(); assert(fileBuf.empty());

	//Set the icon resource, if requested.
	//-> Do it before appending the resource data, as it can change the PE file size.
	if (!iconData.empty())
		SetProgramIconResource(outPath, iconData);
	
	//Reopen the exe file for reading, potentially with the icon resource attached.
	std::unique_ptr<IAssetsReader> pExeReader(Create_AssetsReaderFromFile(outPath, true, RWOpenFlags_Immediately));
	if (!pExeReader)
		return false;
	//Retrieve the PE overlay offset.
	size_t overlayOffset = GetPEOverlayOffset(pExeReader.get());
	if (overlayOffset == 0)
		return false;
	pExeReader.reset();

	//Now append the installer package as the overlay.
	pExeWriter.reset(Create_AssetsWriterToFile(outPath, false, true, RWOpenFlags_Immediately));
	if (!pExeWriter)
		return false;
	pExeWriter->Seek(AssetsSeek_End, 0);
	QWORD endPos = 0;
	pExeWriter->Tell(endPos);
	//Test the assumption: End of file == overlay start.
	if (endPos != overlayOffset)
		return false;
	bool ret = installerData->Write(endPos, pExeWriter.get());
	pExeWriter.reset();

	return ret;
}