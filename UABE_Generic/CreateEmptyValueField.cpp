#pragma once
#include "CreateEmptyValueField.h"

AssetTypeValueField *CreateEmptyValueFieldFromTemplate(AssetTypeTemplateField *pTemplate,
	std::vector<std::unique_ptr<uint8_t[]>>& allocatedMemory)
{
	static const QWORD nullValue = 0; //Is the 0 or empty value for types below.
	switch (pTemplate->valueType)
	{
	case ValueType_String:
		if (pTemplate->children.size() < 1 || !pTemplate->children[0].isArray) return nullptr; //Invalid string.
	case ValueType_Bool:
	case ValueType_Int8:
	case ValueType_UInt8:
	case ValueType_Int16:
	case ValueType_UInt16:
	case ValueType_Int32:
	case ValueType_UInt32:
	case ValueType_Int64:
	case ValueType_UInt64:
	case ValueType_Float:
	case ValueType_Double:
		{
			uint8_t *pCurMem = new uint8_t[sizeof(AssetTypeValueField) + sizeof(AssetTypeValue)];
			allocatedMemory.push_back(std::unique_ptr<uint8_t[]>(pCurMem));
			AssetTypeValueField *pNewField = (AssetTypeValueField*)pCurMem;
			AssetTypeValue *pNewValue = (AssetTypeValue*)(&pNewField[1]);

			*pNewValue = AssetTypeValue(pTemplate->valueType, const_cast<QWORD*>(&nullValue));
			pNewField->Read(pNewValue, pTemplate, 0, nullptr);
			return pNewField;
		}
	case ValueType_ByteArray:
	case ValueType_Array:
	case ValueType_None:
		if (pTemplate->isArray)
		{
			if (pTemplate->children.size() < 2) return nullptr; //Invalid array.
			uint8_t *pCurMem = new uint8_t[sizeof(AssetTypeValueField) + sizeof(AssetTypeValue)];
			allocatedMemory.push_back(std::unique_ptr<uint8_t[]>(pCurMem));
			AssetTypeValueField *pNewField = (AssetTypeValueField*)pCurMem;
			AssetTypeValue *pNewValue = (AssetTypeValue*)(&pNewField[1]);

			AssetTypeByteArray _tmpArray;
			_tmpArray.size = 0;
			_tmpArray.data = nullptr;
			*pNewValue = AssetTypeValue(ValueType_ByteArray, &_tmpArray);
			pNewField->Read(pNewValue, pTemplate, 0, nullptr);

			return pNewField;
		}
		else
		{
			uint8_t *pCurMem = new uint8_t[sizeof(AssetTypeValueField) + (sizeof(AssetTypeValueField*) * pTemplate->children.size())];
			allocatedMemory.push_back(std::unique_ptr<uint8_t[]>(pCurMem));
			AssetTypeValueField *pNewField = (AssetTypeValueField*)pCurMem;
			AssetTypeValueField **pNewChildList = (AssetTypeValueField**)((uintptr_t)pCurMem + sizeof(AssetTypeValueField));
			for (size_t i = 0; i < pTemplate->children.size(); i++)
			{
				if (! (pNewChildList[i] = CreateEmptyValueFieldFromTemplate(&pTemplate->children[i], allocatedMemory)) )
				{
					return nullptr;
				}
			}
			pNewField->Read(nullptr, pTemplate, (uint32_t)pTemplate->children.size(), pNewChildList);
			return pNewField;
		}
	default:
		return nullptr; //Unknown type.
	}
}

AssetsEntryReplacer *MakeEmptyAssetReplacer(
	AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> pFileInfo, long long int pathID, int classID, int monoClassID,
	unsigned int relFileID_MonoScript, long long int pathID_MonoScript, Hash128 propertiesHash_MonoScript)
{
	AssetIdentifier initialAsset;
	//CAssetInterface *pInitialInterface = nullptr;
	int monoBehaviourClass = pFileInfo->GetClassByName("MonoBehaviour");
	if (monoClassID != -1 && monoBehaviourClass != -1 && pathID_MonoScript != 0)
	{
		AssetTypeTemplateField behavTemplateBase;
		if (pFileInfo->MakeTemplateField(&behavTemplateBase, appContext, monoBehaviourClass))
		{
			std::vector<std::unique_ptr<uint8_t[]>> valueMemory;
			AssetTypeValueField *pBaseField = CreateEmptyValueFieldFromTemplate(&behavTemplateBase, valueMemory);
			if (pBaseField)
			{
				AssetTypeValueField *pFileIDField = pBaseField->Get("m_Script")->Get("m_FileID");
				AssetTypeValueField *pPathIDField = pBaseField->Get("m_Script")->Get("m_PathID");
				if (!pFileIDField->IsDummy() && pFileIDField->GetValue() && !pPathIDField->IsDummy() && pPathIDField->GetValue())
				{
					pFileIDField->GetValue()->Set(&relFileID_MonoScript);
					pPathIDField->GetValue()->Set(&pathID_MonoScript);
					IAssetsWriterToMemory *pWriter = Create_AssetsWriterToMemory();
					bool isBigEndian = false; pFileInfo->getEndianness(isBigEndian);
					pBaseField->Write(pWriter, 0, isBigEndian);
					void *pData = nullptr; size_t dataLen = 0;
					if (pWriter->GetBuffer(pData, dataLen))
					{
						AssetsEntryReplacer *pInitialReplacer = MakeAssetModifierFromMemory(
							(uint32_t)pFileInfo->getFileID(), (QWORD)pathID, classID, (uint16_t)monoClassID, 
							pData, dataLen, Free_AssetsWriterToMemory_DynBuf);
						if (pInitialReplacer)
							pWriter->SetFreeBuffer(false);
						initialAsset = AssetIdentifier(pFileInfo->getFileID(), pathID);
						initialAsset.pFile = pFileInfo;
						initialAsset.pReplacer.reset(pInitialReplacer, FreeAssetsReplacer);
					}
					Free_AssetsWriter(pWriter);
				}
			}
		}
	}
	AssetTypeTemplateField templateBase;
	if (pFileInfo->MakeTemplateField(&templateBase, appContext, classID, (uint16_t)monoClassID, &initialAsset))
	{
		std::vector<std::unique_ptr<uint8_t[]>> valueMemory;
		AssetTypeValueField *pBaseField = CreateEmptyValueFieldFromTemplate(&templateBase, valueMemory);
		if (pBaseField)
		{
			std::shared_ptr<ClassDatabaseFile> pBaseClassDb = pFileInfo->GetClassDatabase();

			ClassDatabaseFile *pAssetTypeDb = nullptr;
			ClassDatabaseType *pAssetType = nullptr;
			ClassDatabaseFile tempCombinedFile;
			Hash128 scriptID; bool hasScriptID = false;
			if (monoClassID != -1 && pathID_MonoScript != 0)
			{
				AssetTypeValueField *pFileIDField = pBaseField->Get("m_Script")->Get("m_FileID");
				AssetTypeValueField *pPathIDField = pBaseField->Get("m_Script")->Get("m_PathID");
				if (!pFileIDField->IsDummy() && pFileIDField->GetValue() && !pPathIDField->IsDummy() && pPathIDField->GetValue())
				{
					pFileIDField->GetValue()->Set(&relFileID_MonoScript);
					pPathIDField->GetValue()->Set(&pathID_MonoScript);
				}
				ClassDatabaseFile *pScriptClassDb = nullptr;
				ClassDatabaseType *pScriptClassType = nullptr;
				if (initialAsset.pReplacer != nullptr && pBaseClassDb &&
					pFileInfo->FindScriptClassDatabaseEntry(pScriptClassDb, pScriptClassType, initialAsset, appContext, &scriptID))
				{
					hasScriptID = true;
					ClassDatabaseType *pMonoBehaviourClassType = nullptr;
					int monoBehaviourClass = pFileInfo->GetClassByName("MonoBehaviour");
					if (monoBehaviourClass >= 0)
					{
						for (size_t i = 0; i < pBaseClassDb->classes.size(); i++)
						{
							if (pBaseClassDb->classes[i].classId == monoBehaviourClass)
							{
								pMonoBehaviourClassType = &pBaseClassDb->classes[i];
								break;
							}
						}
						if (pMonoBehaviourClassType)
						{
							//Generate the combined ClassDatabaseType.
							tempCombinedFile.classes.resize(1);
							ClassDatabaseType &newType = tempCombinedFile.classes[0];

							newType.assemblyFileName.fromStringTable = false;
							newType.assemblyFileName.str.string = "";
							newType.name.fromStringTable = false;
							newType.name.str.string = "";

							newType.classId = -(monoClassID + 1);
							newType.baseClass = monoBehaviourClass;

							for (size_t i = 0; i < pMonoBehaviourClassType->fields.size(); i++)
							{
								newType.fields.push_back(pMonoBehaviourClassType->fields[i]);
								ClassDatabaseTypeField &curMonoBehavField = newType.fields[newType.fields.size() - 1];
								curMonoBehavField.fieldName.str.string = curMonoBehavField.fieldName.GetString(pBaseClassDb.get());
								curMonoBehavField.fieldName.fromStringTable = false;
								curMonoBehavField.typeName.str.string = curMonoBehavField.typeName.GetString(pBaseClassDb.get());
								curMonoBehavField.typeName.fromStringTable = false;
							}
							for (size_t i = 1; i < pScriptClassType->fields.size(); i++)
							{
								newType.fields.push_back(pScriptClassType->fields[i]);
								ClassDatabaseTypeField &curMonoBehavField = newType.fields[newType.fields.size() - 1];
								curMonoBehavField.fieldName.str.string = curMonoBehavField.fieldName.GetString(pScriptClassDb);
								curMonoBehavField.fieldName.fromStringTable = false;
								curMonoBehavField.typeName.str.string = curMonoBehavField.typeName.GetString(pScriptClassDb);
								curMonoBehavField.typeName.fromStringTable = false;
							}

							pAssetTypeDb = &tempCombinedFile;
							pAssetType = &newType;
						}
					}
				}
			}
			else if (pBaseClassDb)
			{
				for (size_t i = 0; i < pBaseClassDb->classes.size(); i++)
				{
					if (pBaseClassDb->classes[i].classId == classID)
					{
						pAssetTypeDb = pBaseClassDb.get();
						pAssetType = &pBaseClassDb->classes[i];
						break;
					}
				}
			}
			std::unique_ptr<IAssetsWriterToMemory> pWriter(Create_AssetsWriterToMemory());
			bool bigEndian = false; pFileInfo->getEndianness(bigEndian);
			pBaseField->Write(pWriter.get(), 0, bigEndian);
			void *pData = nullptr; size_t dataLen = 0;
			if (pWriter->GetBuffer(pData, dataLen))
			{
				AssetsEntryReplacer *pReplacer = MakeAssetModifierFromMemory(
					(uint32_t)pFileInfo->getFileID(), (QWORD)pathID, classID, (uint16_t)monoClassID, 
					pData, dataLen, Free_AssetsWriterToMemory_DynBuf);
				if (pReplacer)
				{
					pWriter->SetFreeBuffer(false);
					if (hasScriptID)
						pReplacer->SetScriptIDHash(scriptID);
					if (pAssetTypeDb && pAssetType)
					{
						if (propertiesHash_MonoScript.qValue[0] == 0 && propertiesHash_MonoScript.qValue[1] == 0)
							pReplacer->SetPropertiesHash(pAssetType->MakeTypeHash(pAssetTypeDb));
						else
							pReplacer->SetPropertiesHash(propertiesHash_MonoScript);
						//TODO: Don't add the type information if it's already in the type tree or not needed (i.e. .assets file has no tree)!
						auto pReplacerDatabase = std::make_shared<ClassDatabaseFile>();
						if (pReplacerDatabase->InsertFrom(pAssetTypeDb, pAssetType))
						{
							assert(pReplacerDatabase->classes.size() == 1);
							if (pReplacerDatabase->classes.size() == 1)
								pReplacer->SetTypeInfo(std::move(pReplacerDatabase), &pReplacerDatabase->classes[0]);
						}
					}
					if (monoClassID != -1 && pathID_MonoScript != 0)
					{
						AssetPPtr pptr; 
						pptr.fileID = relFileID_MonoScript; pptr.pathID = (QWORD)pathID_MonoScript;
						pReplacer->AddPreloadDependency(pptr);
					}
					return pReplacer;
				}
			}
		}
	}
	return NULL;
}
