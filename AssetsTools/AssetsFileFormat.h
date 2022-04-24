#pragma once
#include <vector>
#include <cstdint>
#include "defines.h"
#include "ClassDatabaseFile.h"
#include "AssetsReplacer.h"
#include "AssetsFileFormatTypes.h"

class AssetsFile
{
	public:
		AssetsFileHeader header;
		TypeTree typeTree;
		
		PreloadList preloadTable;
		AssetsFileDependencyList dependencies;

		uint32_t secondaryTypeCount; //format >= 0x14
		Type_0D *pSecondaryTypeList; //format >= 0x14
		char unknownString[64]; //format >= 5; seemingly always empty

		uint32_t AssetTablePos;
		uint32_t AssetCount;

		IAssetsReader *pReader;

		ASSETSTOOLS_API AssetsFile(IAssetsReader *pReader);
		AssetsFile(const AssetsFile& other) = delete;
		AssetsFile(AssetsFile&& other) = delete;
		ASSETSTOOLS_API ~AssetsFile();
		AssetsFile &operator=(const AssetsFile& other) = delete;
		AssetsFile &operator=(AssetsFile&& other) = delete;

		//set fileID to -1 if all replacers are for this .assets file but don't have the fileID set to the same one
		//typeMeta is used to add the type information (hash and type fields) for format >= 0x10 if necessary
		ASSETSTOOLS_API QWORD Write(IAssetsWriter *pWriter, QWORD filePos, class AssetsReplacer **pReplacers, size_t replacerCount, uint32_t fileID,
			class ClassDatabaseFile *typeMeta = NULL);

		ASSETSTOOLS_API bool VerifyAssetsFile(AssetsFileVerifyLogger logger = NULL);
};

//Returns whether a Unity type is known to start with a m_Name string field.
// May not be accurate depending on the engine version,
// since this does not take type information into account.
//Types with an m_Name field, but not at the beginning (i.e. where HasName also returns false):
//  GameObject (1)
//  MonoBehaviour (114)
ASSETSTOOLS_API bool HasName(uint32_t type);
