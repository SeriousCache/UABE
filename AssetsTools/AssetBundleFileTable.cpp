#include "stdafx.h"
#include "AssetsFileFormat.h"
#include "AssetBundleFileTable.h"
#include "AssetsFileReader.h"
#include "AssetTypeClass.h"

#define fmread(target,count) {memcpy(target, &((uint8_t*)data)[*filePos], count); *filePos = *filePos + count;}
#define fmwrite(source,count) {if ((filePos+count)>bufferLen){return false;} memcpy(&((uint8_t*)buffer)[filePos], source, count); filePos = filePos + count;}
#define _fmalign(fpos) (fpos + 3) & ~3
#define fmalign() {*filePos = _fmalign(*filePos);}
#define fmwalign() {int newFilePos = _fmalign(filePos); memset(&((uint8_t*)buffer)[filePos], 0, newFilePos-filePos); filePos = newFilePos;}

char _nullChar = 0;
ASSETSTOOLS_API void AssetBundleAsset::FlushChanges()
{
	if (isRead && unityVersion == -1)
	{
		AssetTypeValueField *pFile = pAssetType->GetBaseField();
		
		AssetTypeValueField *pPreloadTable = pFile ? pFile->Get("m_PreloadTable")->Get(0U) : NULL;
		if (pPreloadTable && (preloadArrayLen > pPreloadTable->GetChildrenCount()))
		{
			//AssetTypeInstance::SetChildList frees this memory
			void *pNewValueList = malloc(preloadArrayLen * (3 * sizeof(AssetTypeValueField*) + 3 * sizeof(AssetTypeValueField) + 2 * sizeof(AssetTypeValue)));
			if (pNewValueList != NULL)
			{
				AssetTypeValueField** basePointerList = (AssetTypeValueField**)pNewValueList;
				AssetTypeValueField** varPointerList = (AssetTypeValueField**)(&((uint8_t*)pNewValueList)[preloadArrayLen * sizeof(AssetTypeValueField*)]);
				AssetTypeValueField* valueFields = (AssetTypeValueField*)(&((uint8_t*)pNewValueList)[preloadArrayLen * 3 * sizeof(AssetTypeValueField*)]);
				AssetTypeValue* values = (AssetTypeValue*)(&((uint8_t*)pNewValueList)[preloadArrayLen * (3 * sizeof(AssetTypeValueField*) + 3 * sizeof(AssetTypeValueField))]);

				for (uint32_t i = 0; i < preloadArrayLen; i++)
				{
					basePointerList[i] = &valueFields[i*3];

					varPointerList[i*2] = &valueFields[i*3+1];
					varPointerList[i*2+1] = &valueFields[i*3+2];

					valueFields[i*3].Read(NULL, &pPreloadTable->GetTemplateField()->children[1], 2, &varPointerList[i*2]); //data

					valueFields[i*3+1].Read(&values[i*2], &pPreloadTable->GetTemplateField()->children[1].children[0], 0, NULL); //fileID
					valueFields[i*3+2].Read(&values[i*2+1], &pPreloadTable->GetTemplateField()->children[1].children[1], 0, NULL); //pathID

					values[i*2] = AssetTypeValue(pPreloadTable->GetTemplateField()->children[1].children[0].valueType, &preloadArray[i].fileId);
					values[i*2+1] = AssetTypeValue(pPreloadTable->GetTemplateField()->children[1].children[1].valueType, &preloadArray[i].pathId);
				}

				pAssetType->SetChildList(pPreloadTable, basePointerList, preloadArrayLen, true);
				if (pPreloadTable->GetValue() != NULL)
					pPreloadTable->GetValue()->AsArray()->size = preloadArrayLen;
			}
		}
		else
		{
			pPreloadTable->SetChildrenList(pPreloadTable->GetChildrenList(), pPreloadTable->GetChildrenCount());
			if (pPreloadTable->GetValue() != NULL)
				pPreloadTable->GetValue()->AsArray()->size = preloadArrayLen;
			for (uint32_t i = 0; i < preloadArrayLen; i++)
			{
				{
					AssetTypeValue valueTmp = AssetTypeValue(pPreloadTable->GetTemplateField()->children[1].children[0].valueType, &preloadArray[i].fileId);
					memcpy(pPreloadTable->Get(i)->Get(0U)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
				{
					AssetTypeValue valueTmp = AssetTypeValue(pPreloadTable->GetTemplateField()->children[1].children[1].valueType, &preloadArray[i].pathId);
					memcpy(pPreloadTable->Get(i)->Get(1)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
			}
		}

		AssetTypeValueField *pContainerTable = pFile->Get("m_Container")->Get(0U);
		if (containerArrayLen > pPreloadTable->GetChildrenCount())
		{
			//AssetTypeInstance::SetChildList frees this memory
			void *pNewValueList = malloc(
				containerArrayLen * (8 * sizeof(AssetTypeValueField*) + 8 * sizeof(AssetTypeValueField) + 5 * sizeof(AssetTypeValue))
				+ 1);
			if (pNewValueList != NULL)
			{
				AssetTypeValueField** dataPointerList = (AssetTypeValueField**)pNewValueList;
				AssetTypeValueField** varPointerList = (AssetTypeValueField**)(&((uint8_t*)pNewValueList)[containerArrayLen * sizeof(AssetTypeValueField*)]);
				AssetTypeValueField* valueFields = (AssetTypeValueField*)(&((uint8_t*)pNewValueList)[containerArrayLen * 8 * sizeof(AssetTypeValueField*)]);
				AssetTypeValue* values = (AssetTypeValue*)(&((uint8_t*)pNewValueList)[containerArrayLen * (8 * sizeof(AssetTypeValueField*) + 8 * sizeof(AssetTypeValueField))]);
				char* nullChar = (char*)(&((uint8_t*)pNewValueList)[containerArrayLen * (8 * sizeof(AssetTypeValueField*) + 8 * sizeof(AssetTypeValueField) + 5 * sizeof(AssetTypeValue))]);
				nullChar[0] = 0;

				for (uint32_t i = 0; i < containerArrayLen; i++)
				{
					dataPointerList[i] = &valueFields[i*8];

					varPointerList[i*7] = &valueFields[i*8+1]; //data.first (string)
					varPointerList[i*7+1] = &valueFields[i*8+2]; //data.second (AssetInfo)

					varPointerList[i*7+2] = &valueFields[i*8+3]; //data.second.preloadIndex
					varPointerList[i*7+3] = &valueFields[i*8+4]; //data.second.preloadSize
					varPointerList[i*7+4] = &valueFields[i*8+5]; //data.second.asset (PPtr<Object>)

					varPointerList[i*7+5] = &valueFields[i*8+6]; //data.second.asset.fileID
					varPointerList[i*7+6] = &valueFields[i*8+7]; //data.second.asset.pathID

					valueFields[i*8].Read(NULL, &pContainerTable->GetTemplateField()->children[1], 2, &varPointerList[i*7]); //data

					valueFields[i*8+1].Read(&values[i*5], &pContainerTable->GetTemplateField()->children[1].children[0], 0, NULL); //data.first (string)
					valueFields[i*8+2].Read(NULL, &pContainerTable->GetTemplateField()->children[1].children[1], 3, &varPointerList[i*7+2]); //data.second (AssetInfo)
					valueFields[i*8+3].Read(&values[i*5+1], &pContainerTable->GetTemplateField()->children[1].children[1].children[0], 0, NULL); //data.second.preloadIndex
					valueFields[i*8+4].Read(&values[i*5+2], &pContainerTable->GetTemplateField()->children[1].children[1].children[1], 0, NULL); //data.second.preloadSize
					valueFields[i*8+5].Read(NULL, &pContainerTable->GetTemplateField()->children[1].children[1].children[2], 2, &varPointerList[i*7+5]); //data.second.asset (PPtr<Object>)
					valueFields[i*8+6].Read(&values[i*5+3], &pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[0], 0, NULL); //data.second.asset.fileID
					valueFields[i*8+7].Read(&values[i*5+4], &pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[1], 0, NULL); //data.second.asset.pathID

					char *curName = containerArray[i].name;
					if (curName == NULL)
						curName = nullChar;
					values[i*5] = AssetTypeValue(ValueType_String, curName);
					values[i*5+1] = AssetTypeValue(pContainerTable->GetTemplateField()->children[1].children[1].children[0].valueType, &containerArray[i].preloadIndex);
					values[i*5+2] = AssetTypeValue(pContainerTable->GetTemplateField()->children[1].children[1].children[1].valueType, &containerArray[i].preloadSize);
					values[i*5+3] = AssetTypeValue(pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[0].valueType, &containerArray[i].ids.fileId);
					values[i*5+4] = AssetTypeValue(pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[1].valueType, &containerArray[i].ids.pathId);
				}

				pAssetType->SetChildList(pContainerTable, dataPointerList, containerArrayLen, true);
				if (pContainerTable->GetValue() != NULL)
					pContainerTable->GetValue()->AsArray()->size = containerArrayLen;
			}
		}
		else
		{
			pContainerTable->SetChildrenList(pContainerTable->GetChildrenList(), pContainerTable->GetChildrenCount());
			if (pContainerTable->GetValue() != NULL)
				pContainerTable->GetValue()->AsArray()->size = containerArrayLen;
			for (uint32_t i = 0; i < containerArrayLen; i++)
			{
				{
					char *curName = containerArray[i].name;
					if (curName == NULL)
						curName = &_nullChar;
					AssetTypeValue valueTmp = AssetTypeValue(pContainerTable->GetTemplateField()->children[1].children[0].valueType, curName);
					memcpy(pContainerTable->Get(i)->Get(0U)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
				{
					AssetTypeValue valueTmp = AssetTypeValue(
						pContainerTable->GetTemplateField()->children[1].children[1].children[0].valueType, &containerArray[i].preloadIndex);
					memcpy(pContainerTable->Get(i)->Get(1)->Get(0U)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
				{
					AssetTypeValue valueTmp = AssetTypeValue(
						pContainerTable->GetTemplateField()->children[1].children[1].children[1].valueType, &containerArray[i].preloadSize);
					memcpy(pContainerTable->Get(i)->Get(1)->Get(1)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
				{
					AssetTypeValue valueTmp = AssetTypeValue(
						pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[0].valueType, &containerArray[i].ids.fileId);
					memcpy(pContainerTable->Get(i)->Get(1)->Get(2)->Get(0U)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
				{
					AssetTypeValue valueTmp = AssetTypeValue(
						pContainerTable->GetTemplateField()->children[1].children[1].children[2].children[1].valueType, &containerArray[i].ids.pathId);
					memcpy(pContainerTable->Get(i)->Get(1)->Get(2)->Get(1)->GetValue(), &valueTmp, sizeof(AssetTypeValue));
				}
			}
		}
	}
}

ASSETSTOOLS_API bool AssetBundleAsset::WriteBundleFile(void *buffer, size_t bufferLen, size_t *size, bool bigEndian)
{
	if (!isRead || !pAssetType->GetBaseField())
		return false;
	if (unityVersion == -1)
	{
		IAssetsWriter *pWriter = Create_AssetsWriterToMemory(buffer, (QWORD)bufferLen);
		if (pWriter == NULL)
			return false;
		*size = (int)pAssetType->GetBaseField()->Write(pWriter, 0, bigEndian);
		Free_AssetsWriter(pWriter);
		return true;
	}
	int filePos = 0;
	int iTmp = (int)strlen(name);
	fmwrite(&iTmp, 4);
	fmwrite(name, iTmp);
	fmwalign();

	fmwrite(&preloadArrayLen, 4);
	for (uint32_t i = 0; i < preloadArrayLen; i++)
	{
		fmwrite(&preloadArray[i].fileId, 4);
		fmwrite(&preloadArray[i].pathId, ((unityVersion>=0x0E)?8:4));
	}

	fmwrite(&containerArrayLen, 4);
	for (uint32_t i = 0; i < containerArrayLen; i++)
	{
		ContainerData *cd = &containerArray[i];

		if (cd->name != NULL)
		{
			iTmp = (int)strlen(cd->name);
			fmwrite(&iTmp, 4);
			fmwrite(cd->name, iTmp);
		}
		else
		{
			iTmp = 0;
			fmwrite(&iTmp, 4);
		}
		fmwalign();

		fmwrite(&cd->preloadIndex, 4);
		fmwrite(&cd->preloadSize, 4);
		fmwrite(&cd->ids.fileId, 4);
		fmwrite(&cd->ids.pathId, ((unityVersion>=0x0E)?8:4));
	}

	{
		ContainerData *cd = &mainAsset;

		fmwrite(&cd->preloadIndex, 4);
		fmwrite(&cd->preloadSize, 4);
		fmwrite(&cd->ids.fileId, 4);
		fmwrite(&cd->ids.pathId, ((unityVersion>=0x0E)?8:4));
	}
	
	if (unityVersion < 0x0E)
	{
		fmwrite(&scriptCompatibilityArrayLen, 4);
		for (int i = 0; i < scriptCompatibilityArrayLen; i++)
		{
			ScriptCompatibilityData *scd = &scriptCompatibilityArray[i];

			iTmp = (int)strlen(scd->className);
			fmwrite(&iTmp, 4);
			fmwrite(scd->className, iTmp);
			fmwalign();

			iTmp = (int)strlen(scd->namespaceName);
			fmwrite(&iTmp, 4);
			fmwrite(scd->namespaceName, iTmp);
			fmwalign();

			iTmp = (int)strlen(scd->assemblyName);
			fmwrite(&iTmp, 4);
			fmwrite(scd->assemblyName, iTmp);
			fmwalign();

			fmwrite(&scd->hash, 4);
		}
	
		fmwrite(&classCompatibilityArrayLen, 4);
		for (int i = 0; i < classCompatibilityArrayLen; i++)
		{
			fmwrite(&classCompatibilityArray[i].first, 4);
			fmwrite(&classCompatibilityArray[i].second, 4);
		}
	}

	fmwrite(&runtimeCompatibility, 4);

	if (unityVersion >= 0x0E)
	{
		iTmp = (int)strlen(assetBundleName);
		fmwrite(&iTmp, 4);
		fmwrite(assetBundleName, iTmp);
		fmwalign();

		fmwrite(&dependenciesArrayLen, 4);
		for (int i = 0; i < dependenciesArrayLen; i++)
		{
			iTmp = (int)strlen(dependencies[i]);
			fmwrite(&iTmp, 4);
			fmwrite(dependencies[i], iTmp);
			fmwalign();
		}

		iTmp = (int)isStreamedSceneAssetBundle;
		fmwrite(&iTmp, 4);
	}

	*size = filePos;
	return true;
}

ASSETSTOOLS_API int AssetBundleAsset::GetFileSize()
{
	if (!isRead || !this->pAssetType->GetBaseField())
		return -1;
	if (unityVersion == -1)
	{
		return (int)this->pAssetType->GetBaseField()->GetByteSize();//this->pBaseValueField->GetByteSize();
	}
	int ret = 0;
	ret += 4; //strlen(name)
	if (name)
		ret += _fmalign(strlen(name));
	ret += 4; //preloadArrayLen
	if (preloadArray)
		ret += preloadArrayLen * ((unityVersion>=0x0E)?12:8);
	ret += 4; //containerArrayLen
	if (containerArray)
	{
		for (uint32_t i = 0; i < containerArrayLen; i++)
		{
			ret += 4; //strlen(name)
			if (containerArray[i].name)
				ret += _fmalign(strlen(containerArray[i].name));
		}
		ret += (8 + ((unityVersion>=0x0E)?12:8)) * containerArrayLen;
	}

	ret += (8 + ((unityVersion>=0x0E)?12:8));
	
	if (unityVersion < 0x0E)
	{
		ret += 4; //scriptCompatibilityArrayLen
		if (scriptCompatibilityArray)
		{
			for (int i = 0; i < scriptCompatibilityArrayLen; i++)
			{
				ScriptCompatibilityData *sca = &scriptCompatibilityArray[i];
				ret += 4; //strlen(className)
				if (sca->className)
					ret += _fmalign(strlen(sca->className));
				ret += 4; //strlen(namespaceName)
				if (sca->namespaceName)
					ret += _fmalign(strlen(sca->namespaceName));
				ret += 4; //strlen(assemblyName)
				if (sca->assemblyName)
					ret += _fmalign(strlen(sca->assemblyName));

				ret += 4; //hash
			}
		}

		ret += 4; //classCompatibilityArrayLen
		if (classCompatibilityArray)
			ret += classCompatibilityArrayLen * sizeof(ClassCompatibilityData);
	}

	ret += 4; //runtimeCompatibility

	if (unityVersion >= 0x0E)
	{
		ret += 4; //strlen(assetBundleName)
		if (assetBundleName != NULL)
			ret += _fmalign(strlen(assetBundleName));

		ret += 4; //dependenciesArrayLen
		for (int i = 0; i < dependenciesArrayLen; i++)
		{
			ret += 4; //strlen(dependencies[i])
			if (dependencies[i] != NULL)
				ret += _fmalign(strlen(dependencies[i]));
		}

		ret += _fmalign(1); //isStreamedSceneAssetBundle
	}
	return ret;
}

ASSETSTOOLS_API AssetBundleAsset::AssetBundleAsset()
{
	isModified = false;
	isRead = false;
	unityVersion = 0;
	pAssetType = NULL;
	//pBaseValueField = NULL;
	name = NULL;
	preloadArrayLen = 0;
	preloadArray = NULL;
	containerArrayLen = 0;
	containerArray = NULL;
	memset(&mainAsset, 0, sizeof(ContainerData));
	scriptCompatibilityArrayLen = 0;
	scriptCompatibilityArray = NULL;
	classCompatibilityArrayLen = 0;
	classCompatibilityArray = NULL;
	runtimeCompatibility = 0;
	assetBundleName = NULL;
	dependenciesArrayLen = 0;
	dependencies = NULL;
}

ASSETSTOOLS_API void AssetBundleAsset::Clear()
{
	if (!isRead)
		return;
	isRead = false;
	if (pAssetType != NULL)
	{
		delete pAssetType; pAssetType = NULL;
	}
	if (name != NULL)
	{
		delete[] name; name = NULL;
	}
	if (preloadArray != NULL)
	{
		delete[] preloadArray; preloadArray = NULL;
		preloadArrayLen = 0;
	}
	if (containerArray != NULL)
	{
		for (uint32_t i = 0; i < containerArrayLen; i++)
		{
			if (containerArray[i].name != NULL)
				delete[] containerArray[i].name;
		}
		delete[] containerArray; containerArray = NULL;
		containerArrayLen = 0;
	}
	if (scriptCompatibilityArray != NULL)
	{
		for (int i = 0; i < scriptCompatibilityArrayLen; i++)
		{
			ScriptCompatibilityData *sca = &scriptCompatibilityArray[i];
			if (sca->className != NULL)
				delete[] sca->className;
			if (sca->namespaceName != NULL)
				delete[] sca->namespaceName;
			if (sca->assemblyName != NULL)
				delete[] sca->assemblyName;
		}
		delete[] scriptCompatibilityArray; scriptCompatibilityArray = NULL;
		scriptCompatibilityArrayLen = 0;
	}
	if (classCompatibilityArray != NULL)
	{
		delete[] classCompatibilityArray; classCompatibilityArray = NULL;
		classCompatibilityArrayLen = 0;
	}
	if (assetBundleName != NULL)
	{
		delete[] assetBundleName; assetBundleName = NULL;
	}
	if (dependencies != NULL)
	{
		for (int i = 0; i < dependenciesArrayLen; i++)
			delete[] dependencies[i];
		delete[] dependencies; dependencies = NULL;
		dependenciesArrayLen = 0;
	}
}

ASSETSTOOLS_API AssetBundleAsset::~AssetBundleAsset()
{
	this->Clear();
}

ASSETSTOOLS_API bool AssetBundleAsset::ReadBundleFile(void *data, size_t dataLen, size_t *filePos, AssetTypeTemplateField *pBaseField, bool bigEndian)
{
	Clear();
	unityVersion = -1;
	IAssetsReader *pReader = Create_AssetsReaderFromMemory(data, dataLen, false);
	if (pReader == NULL)
		return false;

	AssetTypeInstance *pType = new AssetTypeInstance(1, &pBaseField, (QWORD)dataLen, pReader, bigEndian);
	//pBaseField->MakeValue(pReader, 0, &pFile);
	Free_AssetsReader(pReader);
	//this->pBaseValueField = pFile;
	AssetTypeValueField *pFile = pType->GetBaseField();
	if (!pFile || pFile->IsDummy())
	{
		delete pType;
		return false;
	}
	this->pAssetType = pType;

	AssetTypeValue *nameValue = (*pFile)["m_Name"]->GetValue();
	if (nameValue != NULL)
	{
		char *nameString = nameValue->AsString();
		name = new char[strlen(nameString)+1];
		strcpy(name, nameString);
		//name = nameValue->AsString();
	}
	else
		name = NULL;
	AssetTypeValueField *preloadTable = pFile->Get("m_PreloadTable")->Get(0U); //Base.m_PreloadTable.Array
	if (preloadTable->IsDummy())
		preloadArrayLen = 0;
	else
		preloadArrayLen = preloadTable->GetChildrenCount();
	preloadArray = new PreloadData[preloadArrayLen];
	for (uint32_t i = 0; i < preloadArrayLen; i++)
	{
		AssetTypeValueField *dataItem = preloadTable->Get(i);
		AssetTypeValueField *fileIDItem = (*dataItem)["m_FileID"];
		AssetTypeValueField *pathIDItem = (*dataItem)["m_PathID"];
		if (fileIDItem->GetValue())
			preloadArray[i].fileId = fileIDItem->GetValue()->AsInt();
		else
			preloadArray[i].fileId = 0;
		if (pathIDItem->GetValue())
			preloadArray[i].pathId = pathIDItem->GetValue()->AsInt64();
		else
			preloadArray[i].pathId = 0;
	}

	AssetTypeValueField *pContainerList = pFile->Get("m_Container")->Get(0U);
	if (pContainerList->IsDummy())
		containerArrayLen = 0;
	else
		containerArrayLen = pContainerList->GetChildrenCount();
	containerArray = new ContainerData[containerArrayLen];
	memset(containerArray, 0, containerArrayLen * sizeof(ContainerData));
	for (uint32_t i = 0; i < containerArrayLen; i++)
	{
		AssetTypeValueField *dataItem = pContainerList->Get(i);
		AssetTypeValueField *nameItem = (*dataItem)["first"];
		if (nameItem->GetValue())
		{
			char *nameString = nameItem->GetValue()->AsString();
			containerArray[i].name = new char[strlen(nameString)+1];
			strcpy(containerArray[i].name, nameString);
			//containerArray[i].name = nameItem->GetValue()->AsString();
		}
		else
			containerArray[i].name = NULL;
		AssetTypeValueField *assetItem = (*dataItem)["second"];
		AssetTypeValueField *preloadIndexItem = (*assetItem)["preloadIndex"];
		if (preloadIndexItem->GetValue())
			containerArray[i].preloadIndex = preloadIndexItem->GetValue()->AsInt();
		AssetTypeValueField *preloadSizeItem = (*assetItem)["preloadSize"];
		if (preloadSizeItem->GetValue())
			containerArray[i].preloadSize = preloadSizeItem->GetValue()->AsInt();
		AssetTypeValueField *fileIDItem = (*assetItem)["asset"]->Get("m_FileID");
		if (fileIDItem->GetValue())
			containerArray[i].ids.fileId = fileIDItem->GetValue()->AsInt();
		AssetTypeValueField *pathIDItem = (*assetItem)["asset"]->Get("m_PathID");
		if (pathIDItem->GetValue())
			containerArray[i].ids.pathId = pathIDItem->GetValue()->AsInt64();
	}

	{//main asset
		
		AssetTypeValueField *mainAssetItem = pFile->Get("m_MainAsset");
		mainAsset.name = NULL;

		AssetTypeValueField *preloadIndexItem = (*mainAssetItem)["preloadIndex"];
		if (preloadIndexItem->GetValue())
			mainAsset.preloadIndex = preloadIndexItem->GetValue()->AsInt();
		AssetTypeValueField *preloadSizeItem = (*mainAssetItem)["preloadSize"];
		if (preloadSizeItem->GetValue())
			mainAsset.preloadSize = preloadSizeItem->GetValue()->AsInt();
		AssetTypeValueField *fileIDItem = (*mainAssetItem)["asset"]->Get("m_FileID");
		if (fileIDItem->GetValue())
			mainAsset.ids.fileId = fileIDItem->GetValue()->AsInt();
		AssetTypeValueField *pathIDItem = (*mainAssetItem)["asset"]->Get("m_PathID");
		if (pathIDItem->GetValue())
			mainAsset.ids.pathId = pathIDItem->GetValue()->AsInt64();
	}
	AssetTypeValueField *runtimeCompatibilityItem = pFile->Get("m_RuntimeCompatibility");
	if (runtimeCompatibilityItem->GetValue())
		runtimeCompatibility = runtimeCompatibilityItem->GetValue()->AsUInt();
	isRead = true;
	return true;
}
ASSETSTOOLS_API void AssetBundleAsset::ReadBundleFile(void *data, size_t dataLen, size_t *filePos, int assetsVersion, bool bigEndian)
{
	Clear();
	unityVersion = assetsVersion;
	isModified = false;
	{
		int nameSize; fmread(&nameSize, 4);
		if (bigEndian)
			SwapEndians_(nameSize);
		name = new char[nameSize+1];
		fmread(name, nameSize); name[nameSize] = 0;
		fmalign();
	}

	fmread(&preloadArrayLen, 4);
	if (bigEndian)
		SwapEndians_(preloadArrayLen);
	preloadArray = new PreloadData[preloadArrayLen];
	for (uint32_t i = 0; i < preloadArrayLen; i++)
	{
		fmread(&preloadArray[i].fileId, 4);
		if (bigEndian)
			SwapEndians_(preloadArray[i].fileId);
		preloadArray[i].pathId = 0;
		if (assetsVersion>=0x0E)
		{
			fmread(&preloadArray[i].pathId, 8);
			if (bigEndian)
				SwapEndians_(preloadArray[i].pathId);
		}
		else
		{
			fmread(&preloadArray[i].pathId, 4);
			if (bigEndian)
				SwapEndians_(*(uint32_t*)&preloadArray[i].pathId);
		}
	}

	fmread(&containerArrayLen, 4);
	if (bigEndian)
		SwapEndians_(containerArrayLen);
	containerArray = new ContainerData[containerArrayLen];
	for (uint32_t i = 0; i < containerArrayLen; i++)
	{
		int assetNameSize; fmread(&assetNameSize, 4);
		if (bigEndian)
			SwapEndians_(assetNameSize);
		char *assetName = new char[assetNameSize+1];
		fmread(assetName, assetNameSize); assetName[assetNameSize] = 0;
		containerArray[i].name = assetName;
		fmalign();

		fmread(&containerArray[i].preloadIndex, 4);
		if (bigEndian)
			SwapEndians_(containerArray[i].preloadIndex);
		fmread(&containerArray[i].preloadSize, 4);
		if (bigEndian)
			SwapEndians_(containerArray[i].preloadSize);
		fmread(&containerArray[i].ids.fileId, 4);
		if (bigEndian)
			SwapEndians_(containerArray[i].ids.fileId);
		containerArray[i].ids.pathId = 0;
		if (assetsVersion>=0x0E)
		{
			fmread(&containerArray[i].ids.pathId, 8);
			if (bigEndian)
				SwapEndians_(containerArray[i].ids.pathId);
		}
		else
		{
			fmread(&containerArray[i].ids.pathId, 4);
			if (bigEndian)
				SwapEndians_(*(uint32_t*)&containerArray[i].ids.pathId);
		}
	}

	{//main asset
		mainAsset.name = NULL;
		fmread(&mainAsset.preloadIndex, 4);
		if (bigEndian)
			SwapEndians_(mainAsset.preloadIndex);
		fmread(&mainAsset.preloadSize, 4);
		if (bigEndian)
			SwapEndians_(mainAsset.preloadSize);
		fmread(&mainAsset.ids.fileId, 4);
		if (bigEndian)
			SwapEndians_(mainAsset.ids.fileId);
		mainAsset.ids.pathId = 0;
		if (assetsVersion>=0x0E)
		{
			fmread(&mainAsset.ids.pathId, 8);
			if (bigEndian)
				SwapEndians_(mainAsset.ids.pathId);
		}
		else
		{
			fmread(&mainAsset.ids.pathId, 4);
			if (bigEndian)
				SwapEndians_(*(uint32_t*)&mainAsset.ids.pathId);
		}
	}

	if (assetsVersion < 0x0E)
	{
		fmread(&scriptCompatibilityArrayLen, 4);
		if (bigEndian)
			SwapEndians_(scriptCompatibilityArrayLen);
		scriptCompatibilityArray = new ScriptCompatibilityData[scriptCompatibilityArrayLen];
		for (int i = 0; i < scriptCompatibilityArrayLen; i++)
		{
			int classNameSize; fmread(&classNameSize, 4);
			if (bigEndian)
				SwapEndians_(classNameSize);
			char *className = new char[classNameSize+1];
			fmread(className, classNameSize); className[classNameSize] = 0;
			scriptCompatibilityArray[i].className = className;
			fmalign();

			int nameSpaceSize; fmread(&nameSpaceSize, 4);
			if (bigEndian)
				SwapEndians_(nameSpaceSize);
			char *nameSpace = new char[nameSpaceSize+1];
			fmread(nameSpace, nameSpaceSize); nameSpace[nameSpaceSize] = 0;
			scriptCompatibilityArray[i].namespaceName = nameSpace;
			fmalign();

			int assemblyNameSize; fmread(&assemblyNameSize, 4);
			if (bigEndian)
				SwapEndians_(assemblyNameSize);
			char *assemblyName = new char[assemblyNameSize+1];
			fmread(assemblyName, assemblyNameSize); assemblyName[assemblyNameSize] = 0;
			scriptCompatibilityArray[i].assemblyName = assemblyName;
			fmalign();

			unsigned int hash; fmread(&hash, 4);
			if (bigEndian)
				SwapEndians_(hash);
			scriptCompatibilityArray[i].hash = hash;
		}

		fmread(&classCompatibilityArrayLen, 4);
		if (bigEndian)
			SwapEndians_(classCompatibilityArrayLen);
		classCompatibilityArray = new ClassCompatibilityData[classCompatibilityArrayLen];
		for (int i = 0; i < classCompatibilityArrayLen; i++)
		{
			fmread(&classCompatibilityArray[i].first, 4);
			if (bigEndian)
				SwapEndians_(classCompatibilityArray[i].first);
			fmread(&classCompatibilityArray[i].second, 4);
			if (bigEndian)
				SwapEndians_(classCompatibilityArray[i].second);
		}
	}
	else
	{
		scriptCompatibilityArrayLen = 0;
		scriptCompatibilityArray = NULL;
		classCompatibilityArrayLen = 0;
		classCompatibilityArray = NULL;
	}
	fmread(&this->runtimeCompatibility, 4);
	if (bigEndian)
		SwapEndians_(this->runtimeCompatibility);
	if (assetsVersion >= 0x0E)
	{
		int assetBundleNameSize; fmread(&assetBundleNameSize, 4);
		if (bigEndian)
			SwapEndians_(assetBundleNameSize);
		assetBundleName = new char[assetBundleNameSize+1];
		fmread(assetBundleName, assetBundleNameSize); assetBundleName[assetBundleNameSize] = 0;
		fmalign();

		fmread(&dependenciesArrayLen, 4);
		if (bigEndian)
			SwapEndians_(dependenciesArrayLen);
		dependencies = new char*[dependenciesArrayLen];
		for (int i = 0; i < dependenciesArrayLen; i++)
		{
			int dependencyNameSize; fmread(&dependencyNameSize, 4);
			if (bigEndian)
				SwapEndians_(dependencyNameSize);
			dependencies[i] = new char[dependencyNameSize+1];
			fmread(dependencies[i], dependencyNameSize); dependencies[i][dependencyNameSize] = 0;
			fmalign();
		}

		fmread(&isStreamedSceneAssetBundle, 1);
		uint32_t dwTmp;
		fmread(&dwTmp, 3);
	}
	isRead = true;
}


ASSETSTOOLS_API void ASSETBUNDLEASSET_DecreaseIndexRefs(AssetBundleAsset *pFile, int iToRemove)
{
	for (uint32_t i = 0; i < pFile->containerArrayLen; i++)
	{
		ContainerData *cd = &pFile->containerArray[i];
		int minIndex = cd->preloadIndex;
		if ((minIndex > iToRemove))
		{
			cd->preloadIndex--;
		}
	}
}
ASSETSTOOLS_API void ASSETBUNDLEASSET_IncreaseIndexRefs(AssetBundleAsset *pFile, int iToAdd)
{
	for (uint32_t i = 0; i < pFile->containerArrayLen; i++)
	{
		ContainerData *cd = &pFile->containerArray[i];
		int minIndex = cd->preloadIndex;
		if ((minIndex >= iToAdd))
		{
			cd->preloadIndex++;
		}
	}
}
ASSETSTOOLS_API void ASSETBUNDLEASSET_Optimize(AssetBundleAsset *pFile)
{
	for (uint32_t i = 0; i < pFile->preloadArrayLen; i++)
	{
		PreloadData *pd = &pFile->preloadArray[i];
		bool found = false;
		for (uint32_t k = 0; k < pFile->containerArrayLen; k++)
		{
			ContainerData *cd = &pFile->containerArray[k];
			uint32_t minIndex = (uint32_t)cd->preloadIndex;
			uint32_t maxIndex = (uint32_t)(minIndex + cd->preloadSize - 1);
			if (minIndex > maxIndex)
				continue;
			if ((i >= minIndex) && (i <= maxIndex))
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			ASSETBUNDLEASSET_DecreaseIndexRefs(pFile, i);
			PreloadData *newPd = new PreloadData[pFile->preloadArrayLen-1];
			if (i > 0)
				memcpy(newPd, pFile->preloadArray, sizeof(PreloadData) * (i-1));
			for (uint32_t _i = i+1; _i < pFile->preloadArrayLen; _i++)
				memcpy(&newPd[_i-1], &pFile->preloadArray[_i], sizeof(PreloadData));
			pFile->preloadArrayLen--;
			delete[] pFile->preloadArray;
			pFile->preloadArray = newPd;
			i--;
		}
	}
}
ASSETSTOOLS_API int ASSETBUNDLEASSET_GetRefCount(AssetBundleAsset *pFile, int preloadIndex)
{
	int ret = 0;
	bool found = false;
	for (uint32_t k = 0; k < pFile->containerArrayLen; k++)
	{
		ContainerData *cd = &pFile->containerArray[k];
		int minIndex = cd->preloadIndex;
		int maxIndex = minIndex + cd->preloadSize - 1;
		if (minIndex > maxIndex)
			continue;
		if ((preloadIndex >= minIndex) && (preloadIndex <= maxIndex))
		{
			ret++;
			break;
		}
	}
	return ret;
}

ASSETSTOOLS_API int AssetBundleAsset::AddContainer(ContainerData *cd)
{
	PreloadData *newPd = new PreloadData[preloadArrayLen+1];
	memcpy(newPd, preloadArray, sizeof(PreloadData) * preloadArrayLen);

	newPd[preloadArrayLen].fileId = cd->ids.fileId;
	newPd[preloadArrayLen].pathId = cd->ids.pathId;
	int preloadIndex = preloadArrayLen;

	preloadArrayLen++;
	delete[] preloadArray;
	preloadArray = newPd;

	ContainerData *newCd = new ContainerData[containerArrayLen+1];
	memcpy(newCd, containerArray, sizeof(ContainerData) * containerArrayLen);

	newCd[containerArrayLen].preloadIndex = preloadIndex;
	newCd[containerArrayLen].preloadSize = 1;
	newCd[containerArrayLen].name = cd->name;
	memcpy(&newCd[containerArrayLen].ids, &cd->ids, sizeof(PreloadData));

	containerArrayLen++;
	delete[] containerArray;
	containerArray = newCd;

	return (preloadArrayLen-1);
}
ASSETSTOOLS_API void AssetBundleAsset::UpdatePreloadArray(uint32_t containerIndex)
{
	if ((containerArrayLen > containerIndex) && (containerIndex >= 0))
	{
		ContainerData *cd = &containerArray[containerIndex];
		if (preloadArrayLen > (uint32_t)cd->preloadIndex)
		{
			if (ASSETBUNDLEASSET_GetRefCount(this, cd->preloadIndex) == 1)
			{
				preloadArray[cd->preloadIndex].fileId = cd->ids.fileId;
				preloadArray[cd->preloadIndex].pathId = cd->ids.pathId;
			}
			else
			{
				PreloadData *newPd = new PreloadData[preloadArrayLen+cd->preloadSize];
				memcpy(newPd, preloadArray, sizeof(PreloadData)*preloadArrayLen);
				memcpy(&newPd[preloadArrayLen], &newPd[cd->preloadIndex], sizeof(PreloadData)*cd->preloadSize);

				newPd[preloadArrayLen].fileId = cd->ids.fileId;
				newPd[preloadArrayLen].pathId = cd->ids.pathId;
				cd->preloadIndex = preloadArrayLen;
				preloadArrayLen += cd->preloadSize;

				delete[] preloadArray;
				preloadArray = newPd;

				ASSETBUNDLEASSET_Optimize(this);
			}
		}
	}
}
ASSETSTOOLS_API void AssetBundleAsset::RemoveContainer(uint32_t index)
{
	if ((containerArrayLen > index) && index >= 0)
	{
		ContainerData *newCd = new ContainerData[containerArrayLen-1];

		if (index > 0)
			memcpy(newCd, containerArray, sizeof(ContainerData) * (index));
		for (uint32_t _i = index+1; _i < containerArrayLen; _i++)
			memcpy(&newCd[_i-1], &containerArray[_i], sizeof(ContainerData));

		containerArrayLen--;
		delete[] containerArray;
		containerArray = newCd;
		ASSETBUNDLEASSET_Optimize(this);
	}
}