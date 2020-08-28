#include "stdafx.h"
#include "..\inc\AssetBundleExtractor\IPluginInterface.h"
#include "..\inc\AssetsTools\TextureFileFormat.h"
#include "..\inc\AssetsTools\AssetsReplacer.h"
#include <tchar.h>

#pragma comment(lib, "AssetsTools.lib")

void TextAsset_FreeMemoryResource(void *pResource)
{
	free(pResource);
}


class CTextBatchImportDesc : public IBatchImportDialogDesc
{
public:
	IPluginInterface *pInterface;
	std::vector<IImportDescriptor*> &elements;
	std::vector<std::string> elementDescriptions;
	std::vector<std::string> elementAssetsFileNames;
	std::string regex;

	std::vector<std::string> importFilePaths;
	std::vector<std::string> importFilePathOverrides;
public:
	CTextBatchImportDesc(IPluginInterface *pInterface, std::vector<IImportDescriptor*> &elements)
		: pInterface(pInterface), elements(elements)
	{
		importFilePaths.resize(elements.size());
		importFilePathOverrides.resize(elements.size());
		elementDescriptions.resize(elements.size());
		elementAssetsFileNames.resize(elements.size());
		for (size_t i = 0; i < elements.size(); i++)
		{
			IAssetInterface *pAsset = elements[i]->GetTargetAssetInterface();
			elementAssetsFileNames[i].assign(pAsset->GetAssetsFileName());
			elementDescriptions[i].assign(pAsset->GetAssetName());
		}
		char *regexCStr = pInterface->MakeImportFileNameRegex("\\.txt");
		if (regexCStr)
		{
			regex.assign(regexCStr);
			pInterface->MemFree(regexCStr);
		}
	}
	~CTextBatchImportDesc()
	{
	}

	bool GetImportableAssetDescs(OUT std::vector<AssetDesc> &nameList)
	{
		nameList.reserve(elementDescriptions.size());
		for (size_t i = 0; i < elementDescriptions.size(); i++)
		{
			AssetDesc curDesc;
			curDesc.description = elementDescriptions[i].c_str();
			curDesc.assetsFileName = elementAssetsFileNames[i].c_str();
			curDesc.pathID = elements[i]->GetTargetAssetInterface()->GetPathID();
			nameList.push_back(curDesc);
		}
		return true;
	}
	
	bool GetFilenameMatchStrings(OUT std::vector<const char*> &regexList, OUT bool &checkSubDirs)
	{
		regexList.push_back(regex.c_str());
		checkSubDirs = false;
		return true;
	}

	bool GetFilenameMatchInfo(IN const char *filename, IN std::vector<const char*> &capturingGroups, OUT size_t &matchIndex)
	{
		const char *assetsFileName = nullptr; __int64 pathId = 0;
		if (pInterface->RetrieveImportRegexInfo(capturingGroups, assetsFileName, pathId))
		{
			for (size_t i = 0; i < elements.size(); i++)
			{
				IAssetInterface *pAsset = elements[i]->GetTargetAssetInterface();
				if (pathId == pAsset->GetPathID() && !strcmp(assetsFileName, pAsset->GetAssetsFileName()))
				{
					matchIndex = i;
					return true;
				}
			}
			matchIndex = (size_t)-1;
		}
		return false;
	}

	void SetInputFilepath(IN size_t matchIndex, IN const char *filepath)
	{
		if (matchIndex < importFilePaths.size())
		{
			importFilePaths[matchIndex].assign(filepath ? filepath : "");
		}
	}

	bool ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow)
	{
		if (matchIndex == (size_t)-1)
			return true;
		if (matchIndex >= importFilePathOverrides.size())
			return false;
		
		importFilePathOverrides[matchIndex].clear();
		
		char *filePathBuf;
		HRESULT hr = pInterface->ShowFileOpenDialogA(hParentWindow, &filePathBuf, "*.txt|Text file:*.*|All types:");
		if (SUCCEEDED(hr))
		{
			importFilePathOverrides[matchIndex].assign(filePathBuf);
			pInterface->FreeUTF8DialogBuf(&filePathBuf);
		}

		return true;
	}

	bool HasFilenameOverride(IN size_t matchIndex, OUT std::string &filenameOverride, OUT bool &relativeToBasePath)
	{
		relativeToBasePath = false;
		if (matchIndex >= importFilePathOverrides.size())
			return false;
		if (importFilePathOverrides[matchIndex].size() > 0)
		{
			filenameOverride.assign(importFilePathOverrides[matchIndex]);
			return true;
		}
		filenameOverride.clear();
		return false;
	}
};

void ImportFromTXT(HWND hParentWnd, IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine)
{
	std::vector<IImportDescriptor*> importElements; importElements.reserve(elements.size() / 2);
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i]->GetType() == ActionElement_ImportDescriptor && ((IImportDescriptor*)elements[i])->GetTargetAssetInterface() != nullptr)
		{
			importElements.push_back((IImportDescriptor*)elements[i]);
		}
	}
	if (importElements.size() > 0)
	{
		CTextBatchImportDesc batchDesc(pInterface, importElements);
		bool doImport = false;
		if (importElements.size() > 1)
		{
			char *inFolderPath = NULL; size_t inFolderPathLen = 0;
			if (pInterface->ShowFolderSelectDialogA(hParentWnd, &inFolderPath, &inFolderPathLen, "Select an input path"))
			{	
				doImport = pInterface->ShowBatchImportDialog(hParentWnd, &batchDesc, inFolderPath);
			}
		}
		else
		{
			char *sourceFileName;
			if (SUCCEEDED(pInterface->ShowFileOpenDialogA(hParentWnd, &sourceFileName, "*.txt|Text file:*.*|All types:")))
			{
				batchDesc.importFilePaths[0].assign(sourceFileName);
				pInterface->FreeUTF8DialogBuf(&sourceFileName);

				doImport = true;
			}
		}
		if (doImport)
		{
			IProgressIndicator *pProgress = (importElements.size() > 1) ? pInterface->ShowProgressIndicator(hParentWnd, 500) : NULL;
			unsigned int actualImportCount = 0;
			if (pProgress)
			{
				pProgress->SetDontCloseIfLog();
				pProgress->SetTitle(std::string("Import TextAssets"));
				for (size_t i = 0; i < batchDesc.importFilePaths.size(); i++)
				{
					if (!batchDesc.importFilePaths[i].empty())
					{
						actualImportCount++;
					}
				}
				pProgress->SetStepRange(0, actualImportCount);
			}

			unsigned int actualImportIndex = 0;
			for (size_t i = 0; i < batchDesc.importFilePaths.size(); i++)
			{
				IAssetInterface *pAsset = importElements[i]->GetTargetAssetInterface();
				if (!batchDesc.importFilePaths[i].empty())
				{
					char descTmp[80];
					sprintf_s(descTmp, "%u/%u (FileID %u, PathID %lli)", actualImportIndex + 1, actualImportCount, pAsset->GetFileID(), pAsset->GetPathID());
					if (pProgress)
					{
						if (pProgress->IsCancelled())
							break;
						pProgress->SetStepStatus(actualImportIndex);
						pProgress->SetDescription(std::string(descTmp) + " Importing");
					}

					IAssetsReader *pSourceReader = Create_AssetsReaderFromFile(batchDesc.importFilePaths[i].c_str(), true, RWOpenFlags_Immediately);
					if (pSourceReader)
					{
						pSourceReader->Seek(AssetsSeek_End, 0);
						QWORD inFileSize = 0;
						pSourceReader->Tell(inFileSize);
						pSourceReader->Seek(AssetsSeek_Begin, 0);

						void *pFileBuffer = malloc(inFileSize);
						if (pFileBuffer)
						{
							pSourceReader->Read(inFileSize, pFileBuffer);

							AssetTypeTemplateField templateBase;
							char *name = "";
							char *pathName = "";
							if (pInterface->MakeTemplateField(pAsset->GetAssetsFile(), pAsset->GetClassID(), &templateBase))
							{
								//make m_Script a TypelessData array because sometimes binary data (including 00s) is stored there
								for (DWORD i = 0; i < templateBase.childrenCount; i++)
								{
									if (!strcmp(templateBase.children[i].name, "m_Script"))
									{
										templateBase.children[i].type = "_string";
										templateBase.children[i].valueType = ValueType_None;
										for (DWORD k = 0; k < templateBase.children[i].childrenCount; k++)
										{
											if (!strcmp(templateBase.children[i].children[k].name, "Array"))
											{
												templateBase.children[i].children[k].type = "TypelessData";
												break;
											}
										}
										break;
									}
								}
								AssetTypeTemplateField *pTemplateBase = &templateBase;
								IAssetsReader *pReader;
								unsigned __int64 assetFilePos;
								unsigned __int64 assetFileSize = pAsset->GetFileReader(pReader, assetFilePos);
								AssetTypeInstance instance(1, &pTemplateBase, assetFileSize, pReader, pAsset->GetAssetsFile()->header.endianness, assetFilePos);
								pAsset->FreeFileReader(pReader);
								AssetTypeValueField *pBase = instance.GetBaseField();
								if (pBase)
								{
									AssetTypeValueField *nameField = pBase->Get("m_Name");
									AssetTypeValueField *scriptField = pBase->Get("m_Script");
									AssetTypeValueField *dataArrayField = scriptField->Get("Array");
									if (!nameField->IsDummy() && !scriptField->IsDummy() && !dataArrayField->IsDummy())
									{
										name = nameField->GetValue()->AsString();
										AssetTypeByteArray byteArrayData;
										byteArrayData.size = (DWORD)inFileSize;
										byteArrayData.data = (BYTE*)pFileBuffer;
										dataArrayField->GetValue()->Set(&byteArrayData);

										QWORD newByteSize = pBase->GetByteSize(0);
										void *newAssetBuffer = malloc((size_t)newByteSize);
										if (newAssetBuffer)
										{
											IAssetsWriter *pWriter = Create_AssetsWriterToMemory(newAssetBuffer, (size_t)newByteSize);
											if (pWriter)
											{
												newByteSize = pBase->Write(pWriter, 0, pAsset->GetAssetsFile()->header.endianness ? true : false);
												AssetsReplacer *pReplacer = MakeAssetModifierFromMemory(pAsset->GetFileID(), pAsset->GetPathID(),
													pAsset->GetClassID(), pAsset->GetMonoClassID(),
													newAssetBuffer, newByteSize, TextAsset_FreeMemoryResource);
												if (pReplacer)
												{
													pInterface->AddReplacer(pReplacer);
												}
												else
												{
													MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("Error"), 16);
													free(newAssetBuffer);
												}

												Free_AssetsWriter(pWriter);
											}
											else
											{
												MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("Error"), 16);
												free(newAssetBuffer);
											}
										}
										else
											MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("Error"), 16);
									}
									else
									{
										if (pProgress)
										{
											pProgress->AddLogLine(std::string(descTmp) + " ERROR: Unknown TextAsset format!");
										}
										else
											MessageBox(hParentWnd, TEXT("Unknown TextAsset format!"), TEXT("ERROR"), 16);
									}
								}
								else
								{
									if (pProgress)
									{
										pProgress->AddLogLine(std::string(descTmp) + " ERROR: Unable to load the TextAsset!");
									}
									else
										MessageBox(hParentWnd, TEXT("Unable to load the TextAsset!"), TEXT("ERROR"), 16);
								}
							}
							else
							{
								if (pProgress)
								{
									pProgress->AddLogLine(std::string(descTmp) + " ERROR: TextAsset format not found!");
								}
								else
									MessageBox(hParentWnd, TEXT("TextAsset format not found!"), TEXT("ERROR"), 16);
							}
							free(pFileBuffer);
						}
						else
							MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("Error"), 16);
						Free_AssetsReader(pSourceReader);
					}
					actualImportIndex++;
				}
			}

			if (pProgress)
			{
				pInterface->CloseProgressIndicator(pProgress);
			}
		}
	}
}

void ExportToTXT(HWND hParentWnd, IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine)
{
	char *outFolderPath = NULL; size_t outFolderPathLen = 0;
	char **prevAssetNames = NULL;

	size_t exportCount = 0;
	for (size_t i = 0; i < elements.size(); i++)
		if (elements[i]->GetType() == ActionElement_ExportDescriptor)
			exportCount++;
	if (exportCount > 1)
	{
		prevAssetNames = (char**)malloc(sizeof(char*) * (exportCount - 1));
		if (prevAssetNames == NULL)
		{
			MessageBox(NULL, TEXT("Out of memory!"), TEXT("ERROR"), 16);
			return;
		}
		memset(prevAssetNames, 0, sizeof(char*) * (exportCount - 1));
		if (!pInterface->ShowFolderSelectDialogA(hParentWnd, &outFolderPath, &outFolderPathLen, "Select an output path"))
			return;
	}

	IProgressIndicator *pProgress = nullptr;
	if (exportCount > 1 && (pProgress = pInterface->ShowProgressIndicator(hParentWnd, 500)))
	{
		pProgress->SetDontCloseIfLog();
		pProgress->SetTitle(std::string("Export TextAssets to TXT"));
		pProgress->SetStepRange(0, 10 * exportCount);
	}
	
	size_t exportIndex = 0;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i]->GetType() != ActionElement_ExportDescriptor)
			continue;
		IExportDescriptor *pExportDesc = (IExportDescriptor*)elements[i];
		IAssetInterface *pAsset = pExportDesc->GetSourceAssetInterface();
		
		char descTmp[80];
		sprintf_s(descTmp, "%u/%u (FileID %u, PathID %lli)", (unsigned int)(exportIndex + 1), (unsigned int)exportCount, pAsset->GetFileID(), pAsset->GetPathID());
		unsigned int curStepBase = 10 * exportIndex;
		if (pProgress)
		{
			if (pProgress->IsCancelled())
				break;
			pProgress->SetStepStatus(curStepBase);
			pProgress->SetDescription(std::string(descTmp) + " Reading asset...");
			//pProgress->AddLogLine(description);
		}
		
		AssetTypeTemplateField templateBase;
		if (pInterface->MakeTemplateField(pAsset->GetAssetsFile(), pAsset->GetClassID(), &templateBase))
		{
			//make m_Script a TypelessData array because it might contain binary data (even though it's a string)
			for (DWORD i = 0; i < templateBase.childrenCount; i++)
			{
				if (!strcmp(templateBase.children[i].name, "m_Script"))
				{
					templateBase.children[i].type = "_string";
					templateBase.children[i].valueType = ValueType_None;
					for (DWORD k = 0; k < templateBase.children[i].childrenCount; k++)
					{
						if (!strcmp(templateBase.children[i].children[k].name, "Array"))
						{
							templateBase.children[i].children[k].type = "TypelessData";
							break;
						}
					}
					break;
				}
			}
			IAssetsReader *pReader;
			unsigned __int64 filePos;
			unsigned __int64 fileSize = pAsset->GetFileReader(pReader, filePos);
			AssetTypeTemplateField *pTemplateBase = &templateBase;
			AssetTypeInstance instance(1, &pTemplateBase, fileSize, pReader, pAsset->GetAssetsFile()->header.endianness, filePos);
			pAsset->FreeFileReader(pReader);
			AssetTypeValueField *pBase = instance.GetBaseField();
			//templateBase.MakeValue(reader, readerPar, (QWORD)filePos, &pBase);
		
			if (pBase)
			{
				AssetTypeValueField *nameField = pBase->Get("m_Name");
				AssetTypeValueField *scriptField = pBase->Get("m_Script");
				AssetTypeValueField *dataArrayField = scriptField->Get("Array");
				if (!nameField->IsDummy() && !scriptField->IsDummy() && !dataArrayField->IsDummy())
				{
					char *name = nameField->GetValue()->AsString();
					
					WCHAR *dlgTargetFileName = NULL;
					char *fileName = name;
					if (outFolderPath == NULL)
					{
#ifdef _UNICODE
						WCHAR *exportSuggestion = pInterface->MakeExportFileNameW(
#else
						char *exportSuggestion = pInterface->MakeExportFileName(
#endif
							outFolderPath, fileName,
							pAsset->GetAssetsFileName(), pAsset->GetPathID(),
							prevAssetNames,
							exportIndex, exportCount,
							".txt");
						WCHAR *filePath;
						if (SUCCEEDED(pInterface->ShowFileSaveDialog(hParentWnd, &filePath, L"*.txt|Text file:", NULL, exportSuggestion)))
						{
							size_t outFilePathLen = wcslen(filePath);
							if ((outFilePathLen < 4) || (_wcsicmp(&filePath[outFilePathLen-4], L".txt")))
							{
								dlgTargetFileName = (WCHAR*)malloc((outFilePathLen + 5) * sizeof(WCHAR));
								if (dlgTargetFileName == NULL)
									MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("ERROR"), 16);
								else
								{
									memcpy(dlgTargetFileName, filePath, outFilePathLen * sizeof(WCHAR));
									memcpy(&dlgTargetFileName[outFilePathLen], L".txt", 5 * sizeof(WCHAR));
								}
							}
							else
							{
								dlgTargetFileName = (WCHAR*)malloc((outFilePathLen + 1) * sizeof(WCHAR));
								if (dlgTargetFileName == NULL)
									MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("ERROR"), 16);
								else
								{
									memcpy(dlgTargetFileName, filePath, (outFilePathLen+1) * sizeof(WCHAR));
								}
							}
							pInterface->FreeCOMFilePathBuf(&filePath);
						}
						pInterface->MemFree(exportSuggestion);
					}
					else
					{
						char *cOutFilePath = pInterface->MakeExportFileName(
							outFolderPath, fileName,
							pAsset->GetAssetsFileName(), pAsset->GetPathID(),
							prevAssetNames,
							exportIndex, exportCount,
							".txt");
						if ((exportIndex+1) < exportCount)
						{
							size_t fileNameLen = strlen(fileName);
							prevAssetNames[exportIndex] = (char*)malloc((fileNameLen + 1) * sizeof(char));
							if (prevAssetNames[exportIndex] == NULL)
								MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("ERROR"), 16);
							else
							{
								memcpy(prevAssetNames[exportIndex], fileName, (fileNameLen + 1) * sizeof(char));
								exportIndex++;
							}
						}
						if (cOutFilePath == NULL)
							MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("ERROR"), 16);
						else
						{
							int filePathALen = (int)strlen(cOutFilePath);
							int filePathWLen = MultiByteToWideChar(CP_UTF8, 0, cOutFilePath, (int)filePathALen, NULL, 0);
							dlgTargetFileName = (WCHAR*)malloc((filePathWLen + 1) * sizeof(wchar_t));
							if (dlgTargetFileName != NULL)
							{
								MultiByteToWideChar(CP_UTF8, 0, cOutFilePath, (int)filePathALen, dlgTargetFileName, filePathWLen);
								dlgTargetFileName[filePathWLen] = 0;
							}
							else
								MessageBox(hParentWnd, TEXT("Out of memory!"), TEXT("ERROR"), 16);
							pInterface->MemFree(cOutFilePath);
							//free(cOutFilePath);
						}
					}

					if (dlgTargetFileName != NULL)
					{
						IAssetsWriter *pFileWriter = Create_AssetsWriterToFile(dlgTargetFileName, true, true, RWOpenFlags_Immediately);
						if (pFileWriter != NULL)
						{
							if (pProgress)
							{
								pProgress->SetDescription(std::string(descTmp) + " Exporting TextAsset to file...");
								//pProgress->AddLogLine(description);
								pProgress->SetStepStatus(curStepBase + 6);
							}

							AssetTypeByteArray *pByteArray = dataArrayField->GetValue()->AsByteArray();
							pFileWriter->Write(pByteArray->size, pByteArray->data);
							Free_AssetsWriter(pFileWriter);

							if (pProgress) pProgress->SetStepStatus(curStepBase + 10);
						}
						else
						{
							if (pProgress)
							{
								pProgress->AddLogLine(std::string(descTmp) + " ERROR: Unable to open the output file!");
							}
							else
								MessageBox(hParentWnd, TEXT("Unable to open the output file!"), TEXT("ERROR"), 16);
						}

						free(dlgTargetFileName);
					}
				}
				else
				{
					if (pProgress)
					{
						pProgress->AddLogLine(std::string(descTmp) + " ERROR: Unknown TextAsset format!");
					}
					else
						MessageBox(hParentWnd, TEXT("Unknown TextAsset format!"), TEXT("ERROR"), 16);
				}
			}
			else
			{
				if (pProgress)
				{
					pProgress->AddLogLine(std::string(descTmp) + " ERROR: Unable to load the TextAsset!");
				}
				else
					MessageBox(hParentWnd, TEXT("Unable to load the TextAsset!"), TEXT("ERROR"), 16);
			}
		}
		else
		{
			if (pProgress)
			{
				pProgress->AddLogLine(std::string(descTmp) + " ERROR: TextAsset format not found!");
			}
			else
				MessageBox(hParentWnd, TEXT("TextAsset format not found!"), TEXT("ERROR"), 16);
		}
	}
	pInterface->FreeUTF8DialogBuf(&outFolderPath);
	if (prevAssetNames)
	{
		for (size_t i = 0; i < (exportCount-1); i++)
		{
			if (prevAssetNames[i])
				free(prevAssetNames[i]);
		}
		free(prevAssetNames);
	}
	if (pProgress) pInterface->CloseProgressIndicator(pProgress);
}


bool _cdecl SupportsBatchElements(IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, ActionElementType type)
{
	struct AssetsFile_ClassIdPair { unsigned int fileId; int classId; };
	std::vector<AssetsFile_ClassIdPair> classIds;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i]->GetType() != type)
			return false;
		IAssetInterface *pAssetInterface = NULL;
		switch (type)
		{
		case ActionElement_ExportDescriptor:
			pAssetInterface = ((IExportDescriptor*)elements[i])->GetSourceAssetInterface();
			break;
		case ActionElement_ImportDescriptor:
			pAssetInterface = ((IImportDescriptor*)elements[i])->GetTargetAssetInterface();
			break;
		}
		if (pAssetInterface == NULL)
			return false;
		int classId = pAssetInterface->GetClassID();
		unsigned int fileId = pAssetInterface->GetFileID();
		bool foundClassId = false; 
		for (size_t j = 0; j < classIds.size(); j++)
		{
			if (classIds[j].fileId == fileId && classIds[j].classId == classId)
			{
				foundClassId = true;
				break;
			}
		}
		if (!foundClassId)
		{
			char nameBuffer[32]; nameBuffer[31] = 0;
			pInterface->GetTypenameA(pAssetInterface->GetAssetsFile(), pAssetInterface->GetClassID(), nameBuffer, 32);
			if (!strcmp(nameBuffer, "TextAsset"))
			{
				AssetsFile_ClassIdPair newPair = {pAssetInterface->GetFileID(), classId};
				classIds.push_back(newPair);
			}
			else
				return false;
		}
	}
	return true;
}
bool _cdecl SupportsBatchExport(IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine, std::string &desc)
{
	desc = std::string("Export to .txt");
	return SupportsBatchElements(pInterface, elements, ActionElement_ExportDescriptor);
}
bool _cdecl SupportsBatchImport(IPluginInterface *pInterface, std::vector<IElementDescriptor*> &elements, const std::vector<const char*> &commandLine, std::string &desc)
{
	desc = std::string("Import from .txt");
	return SupportsBatchElements(pInterface, elements, ActionElement_ImportDescriptor);
}

BYTE PluginInfoBuffer[sizeof(PluginInfo2)+2*sizeof(PluginAssetOption2)];
PluginInfo2 *GetAssetBundlePluginInfo(PluginInfoVersion &version)
{
	version = PluginInfoVersion2;

	PluginInfo2 *pPluginInfo = (PluginInfo2*)PluginInfoBuffer;
	strcpy(pPluginInfo->name, "TextAsset");
	pPluginInfo->optionCount = 2;

	pPluginInfo->options[0].action = PluginAction_EXPORT_Batch;
	pPluginInfo->options[0].supportCallback = SupportsBatchExport;
	pPluginInfo->options[0].performCallback = ExportToTXT;

	pPluginInfo->options[1].action = PluginAction_IMPORT_Batch;
	pPluginInfo->options[1].supportCallback = SupportsBatchImport;
	pPluginInfo->options[1].performCallback = ImportFromTXT;
	return pPluginInfo;
}