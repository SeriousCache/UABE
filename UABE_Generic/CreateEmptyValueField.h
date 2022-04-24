#pragma once
#include "api.h"
#include "AppContext.h"
#include "../AssetsTools/AssetTypeClass.h"
#include <vector>
#include <memory>
#include <stdint.h>

UABE_Generic_API AssetTypeValueField *CreateEmptyValueFieldFromTemplate(AssetTypeTemplateField *pTemplate,
	std::vector<std::unique_ptr<uint8_t[]>> &allocatedMemory);

UABE_Generic_API AssetsEntryReplacer *MakeEmptyAssetReplacer(
	AppContext &appContext, std::shared_ptr<AssetsFileContextInfo> pFileInfo, long long int pathID, int classID, int monoClassID,
		unsigned int relFileID_MonoScript, long long int pathID_MonoScript, Hash128 propertiesHash_MonoScript);
