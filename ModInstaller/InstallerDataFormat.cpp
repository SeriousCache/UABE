#include "stdafx.h"
#include "InstallerDataFormat.h"
#include <InternalBundleReplacer.h>
#include <cassert>

MODINSTALLER_API InstallerPackageFile::InstallerPackageFile()
{
}
MODINSTALLER_API InstallerPackageFile::~InstallerPackageFile()
{
}

bool InstallerPackageFile::Read(QWORD& pos, IAssetsReader *pReader, std::shared_ptr<IAssetsReader> ref_pReader, bool prefReplacersInMemory = false)
{
	DWORD assetsCount = 0;
	uint8_t version = 1;
	//opCount = 0;
	*(DWORD*)(&magic[0]) = 0;
	affectedAssets.clear();
	modName.clear();
	modCreators.clear();
	modDescription.clear();

	pos += pReader->Read(pos, 4, &magic[0]);
	if (*(DWORD*)(&magic[0]) != 0x50494D45)
		return false;
	
	pos += pReader->Read(pos, 1, &version);
	if (version > 2)
		return false;

	uint16_t nameLen = 0;
	pos += pReader->Read(pos, 2, &nameLen);
	std::vector<char> modNameBuf(nameLen);
	pos += pReader->Read(pos, nameLen, modNameBuf.data());
	modName.assign(modNameBuf.begin(), modNameBuf.end());
	
	uint16_t creatorsLen = 0;
	pos += pReader->Read(pos, 2, &creatorsLen);
	std::vector<char> modCreatorsBuf(creatorsLen);
	pos += pReader->Read(pos, creatorsLen, modCreatorsBuf.data());
	modCreators.assign(modCreatorsBuf.begin(), modCreatorsBuf.end());
	
	uint16_t descriptionLen = 0;
	pos += pReader->Read(pos, 2, &descriptionLen);
	std::vector<char> modDescriptionBuf(descriptionLen);
	pos += pReader->Read(pos, descriptionLen, modDescriptionBuf.data());
	modDescription.assign(modDescriptionBuf.begin(), modDescriptionBuf.end());

	if (version >= 1)
	{
		pos = addedTypes.Read(pReader, pos);
	}

	pos += pReader->Read(pos, 4, &assetsCount);
	affectedAssets.reserve(assetsCount);
	for (DWORD i = 0; i < assetsCount; i++)
	{
		affectedAssets.push_back(InstallerPackageAssetsDesc());
		InstallerPackageAssetsDesc *pDesc = &affectedAssets[i];
		uint8_t typeVal = 0;
		pos += pReader->Read(pos, 1, &typeVal);
		if (typeVal > 2)
			return false;
		pDesc->type = static_cast<InstallerPackageAssetsType>((int)typeVal);
		uint16_t pathLen = 0;
		pos += pReader->Read(pos, 2, &pathLen);
		std::vector<char> pathBuf(pathLen);
		pos += pReader->Read(pos, pathLen, pathBuf.data());
		pDesc->path.assign(pathBuf.begin(), pathBuf.end());
		DWORD replacerCount = 0;
		pos += pReader->Read(pos, 4, &replacerCount);
		switch (pDesc->type)
		{
		case InstallerPackageAssetsType::Assets:
			for (DWORD k = 0; k < replacerCount; k++)
			{
				pDesc->replacers.emplace_back(ref_pReader
					? ReadAssetsReplacer(pos, ref_pReader, prefReplacersInMemory)
					: ReadAssetsReplacer(pos, pReader, prefReplacersInMemory));
				if (pDesc->replacers.back().get() == nullptr)
				{
					pDesc->replacers.clear();
					return false;
				}
			}
			break;
		case InstallerPackageAssetsType::Bundle:
			for (DWORD k = 0; k < replacerCount; k++)
			{
				pDesc->replacers.emplace_back(ref_pReader
					? ReadBundleReplacer(pos, ref_pReader, prefReplacersInMemory)
					: ReadBundleReplacer(pos, pReader, prefReplacersInMemory));
				if (pDesc->replacers.back().get() == nullptr)
				{
					pDesc->replacers.clear();
					return false;
				}
			}
			break;
		case InstallerPackageAssetsType::Resources:
			if (replacerCount != 1)
				return false;
			pDesc->replacers.emplace_back(ref_pReader
				? ReadBundleReplacer(pos, ref_pReader, prefReplacersInMemory)
				: ReadBundleReplacer(pos, pReader, prefReplacersInMemory));
			if (dynamic_cast<BundleEntryModifierByResources*>(pDesc->replacers.back().get()) == nullptr)
			{
				pDesc->replacers.clear();
				return false;
			}
			break;
		}
	}
	return true;
}
MODINSTALLER_API bool InstallerPackageFile::Read(QWORD& pos, IAssetsReader *pReader, bool prefReplacersInMemory)
{
	return Read(pos, pReader, nullptr, prefReplacersInMemory);
}
MODINSTALLER_API bool InstallerPackageFile::Read(QWORD& pos, std::shared_ptr<IAssetsReader> pReader, bool prefReplacersInMemory)
{
	IAssetsReader *_pReader = pReader.get();
	return Read(pos, _pReader, std::move(pReader), prefReplacersInMemory);
}
MODINSTALLER_API InstallerPackageAssetsDesc::InstallerPackageAssetsDesc() : type(InstallerPackageAssetsType::Assets){}
MODINSTALLER_API InstallerPackageAssetsDesc::InstallerPackageAssetsDesc(const InstallerPackageAssetsDesc &src)
{
	type = src.type;
	replacers = src.replacers;
	path = src.path;
}
MODINSTALLER_API InstallerPackageAssetsDesc::~InstallerPackageAssetsDesc(){}
MODINSTALLER_API bool InstallerPackageFile::Write(QWORD& pos, IAssetsWriter *pWriter)
{
	*(DWORD*)(&magic[0]) = 0x50494D45;
	pos += pWriter->Write(pos, 4, &magic[0]);

	uint8_t version = 2;
	pos += pWriter->Write(pos, 1, &version);

	uint16_t nameLen = (uint16_t)modName.size();
	pos += pWriter->Write(pos, 2, &nameLen);
	pos += pWriter->Write(pos, nameLen, modName.c_str());
	
	uint16_t creatorsLen = (uint16_t)modCreators.size();
	pos += pWriter->Write(pos, 2, &creatorsLen);
	pos += pWriter->Write(pos, creatorsLen, modCreators.c_str());
	
	uint16_t descriptionLen = (uint16_t)modDescription.size();
	pos += pWriter->Write(pos, 2, &descriptionLen);
	pos += pWriter->Write(pos, descriptionLen, modDescription.c_str());
	
	if (version >= 1)
	{
		pos = addedTypes.Write(pWriter, pos);
	}

	DWORD assetsCount = (DWORD)affectedAssets.size();
	pos += pWriter->Write(pos, 4, &assetsCount);
	for (DWORD i = 0; i < assetsCount; i++)
	{
		InstallerPackageAssetsDesc *pDesc = &affectedAssets[i];
		uint8_t typeVal = (uint8_t)(uint32_t)pDesc->type;
		pos += pWriter->Write(pos, 1, &typeVal);

		uint16_t pathLen = (uint16_t)pDesc->path.size();
		pos += pWriter->Write(pos, 2, &pathLen);
		pos += pWriter->Write(pos, pathLen, pDesc->path.c_str());

		DWORD replacerCount = (DWORD)pDesc->replacers.size();
		pos += pWriter->Write(pos, 4, &replacerCount);
		switch (pDesc->type)
		{
		case InstallerPackageAssetsType::Assets:
			for (DWORD k = 0; k < replacerCount; k++)
				pos = ((AssetsReplacer*)pDesc->replacers[k].get())->WriteReplacer(pos, pWriter);
			break;
		case InstallerPackageAssetsType::Bundle:
			for (DWORD k = 0; k < replacerCount; k++)
				pos = ((BundleReplacer*)pDesc->replacers[k].get())->WriteReplacer(pos, pWriter);
			break;
		case InstallerPackageAssetsType::Resources:
			assert(replacerCount == 1);
			if (replacerCount != 1 || dynamic_cast<BundleEntryModifierByResources*>(pDesc->replacers[0].get()) == nullptr)
				return false;
			pos = ((BundleReplacer*)pDesc->replacers[0].get())->WriteReplacer(pos, pWriter);
			break;
		default:
			assert(false);
			return false;
		}
	}
	return true;
}
