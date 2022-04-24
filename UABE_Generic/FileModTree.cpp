#include "FileModTree.h"
#include "AppContext.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include "../AssetsTools/InternalAssetsReplacer.h"
#include "../AssetsTools/InternalBundleReplacer.h"
#include <algorithm>

VisibleFileEntry::VisibleFileEntry(AppContext &appContext, std::shared_ptr<FileContextInfo> pContextInfo)
			: treeViewEntry(0), pathNull(false)
{
	assert(pContextInfo->getFileContext() != nullptr);

	this->pathOrName.assign(pContextInfo->getFileContext()->getFilePath());
	this->fileType = pContextInfo->getFileContext()->getType();
	std::vector<unsigned int> childFileIDs;
	pContextInfo->getChildFileIDs(childFileIDs);
	switch (pContextInfo->getFileContext()->getType())
	{
	case FileContext_Bundle:
		{
			std::unique_ptr<BundleReplacer> bundleReplacer = pContextInfo->makeBundleReplacer(appContext, "", "", 0, false);
			assert(dynamic_cast<BundleEntryModifierFromBundle*>(bundleReplacer.get()) != nullptr);
			if (BundleEntryModifierFromBundle *pBundleModifier = dynamic_cast<BundleEntryModifierFromBundle*>(bundleReplacer.get()))
			{
				struct {
					std::shared_ptr<FileContextInfo> pContextInfo;
					void operator()(BundleReplacer *pBundleReplacer) { FreeBundleReplacer(pBundleReplacer); pContextInfo.reset(); }
				} replacerOwnerDeleter;
				std::shared_ptr<BundleReplacer> bundleReplacerShared(bundleReplacer.release(), replacerOwnerDeleter);
				constructFromReplacer(appContext, bundleReplacerShared, BundleReplacer_BundleEntryModifierFromBundle);
				this->pathOrName.assign(pContextInfo->getFileContext()->getFilePath());
				this->pContextInfo = std::move(pContextInfo);
				return;
			}
		}
		break;
	case FileContext_Assets:
		assert(childFileIDs.empty());
		{
			auto *pAssetsContextInfo = reinterpret_cast<AssetsFileContextInfo*>(pContextInfo.get());
			AssetsFileContext *pAssetsContext = pAssetsContextInfo->getAssetsFileContext();
			auto replacersVec = pAssetsContextInfo->getAllReplacers();

			std::sort(replacersVec.begin(), replacersVec.end(),
				[](const std::shared_ptr<AssetsReplacer>& a, const std::shared_ptr<AssetsReplacer>& b) {
					bool aIsEntryModifier = (a->GetType() == AssetsReplacement_AddOrModify || a->GetType() == AssetsReplacement_Remove);
					bool bIsEntryModifier = (b->GetType() == AssetsReplacement_AddOrModify || b->GetType() == AssetsReplacement_Remove);
					if (!aIsEntryModifier && bIsEntryModifier)
						return true; //Put non-entry modifiers before entry modifiers.
					else if (!aIsEntryModifier || !bIsEntryModifier)
						return false; //No particular order between non-entry modifiers.
					return reinterpret_cast<AssetsEntryReplacer*>(a.get())->GetPathID()
						< reinterpret_cast<AssetsEntryReplacer*>(b.get())->GetPathID();
				});

			replacers.resize(replacersVec.size());
			for (size_t i = 0; i < replacersVec.size(); ++i)
				replacers[i].pReplacer = std::move(replacersVec[i]);
		}
		break;
	case FileContext_Resources:
		assert(childFileIDs.empty());
		{
			std::unique_ptr<BundleReplacer> bundleReplacer = pContextInfo->makeBundleReplacer(appContext, "", "", (uint32_t)-1, false);
			assert(dynamic_cast<BundleEntryModifierByResources*>(bundleReplacer.get()) != nullptr);
			if (BundleEntryModifierByResources* pBundleModifier = dynamic_cast<BundleEntryModifierByResources*>(bundleReplacer.get()))
			{
				replacers.resize(1);
				replacers[0].pReplacer.reset(bundleReplacer.release());
			}
			else
			{
				throw std::invalid_argument("VisibleFileEntry: Bundle replacer generated from a ResourcesFileContextInfo is null or has an unexpected type!");
			}
		}
		break;
	case FileContext_Generic:
		assert(childFileIDs.empty());
		throw std::domain_error("VisibleFileEntry: FileContext_Generic is not supported as a base file!");
		break;
	default:
		throw std::domain_error("VisibleFileEntry constructed with an unknown file type!");
	}
	this->pContextInfo = std::move(pContextInfo);
}
//shared_ptr<BundleReplacer> deleter that keeps a shared_ptr reference
// on a parent BundleEntryModifierFromBundle or BundleEntryModifierFromAssets,
// which in turn frees all child replacers on destruction.
struct GenericReplacerOwnershipWrapper
{
	std::shared_ptr<GenericReplacer> memoryOwner;
	GenericReplacerOwnershipWrapper(std::shared_ptr<GenericReplacer> owner)
		: memoryOwner(std::move(owner)) {}
	void operator()(GenericReplacer* pReplacer) {memoryOwner.reset();}
};
//Generate a VisibleFileEntry from a BundleEntryModifierFromAssets, BundleEntryModifierFromBundle or BundleEntryModifierByResources.
//-> type: EBundleReplacers (InternalBundleReplacer.h)
VisibleFileEntry::VisibleFileEntry(class AppContext &appContext, std::shared_ptr<BundleReplacer> &fromReplacer, unsigned int type)
			: treeViewEntry(0), pathNull(true)
{
	constructFromReplacer(appContext, fromReplacer, type);
}
void VisibleFileEntry::constructFromReplacer(class AppContext &appContext, std::shared_ptr<BundleReplacer> &fromReplacer, unsigned int type)
{
	const char *_origEntryName = fromReplacer->GetOriginalEntryName();
	this->pathOrName = (_origEntryName == nullptr) ? "" : _origEntryName;
	this->pathNull = (_origEntryName == nullptr);
	const char *_newEntryName = fromReplacer->GetEntryName();
	this->newName = (_newEntryName == nullptr) ? "" : _newEntryName;
	switch ((EBundleReplacers)type)
	{
	case BundleReplacer_BundleEntryModifierFromAssets:
		{
			this->fileType = FileContext_Assets;
			BundleEntryModifierFromAssets *pAssetsFileReplacer = reinterpret_cast<BundleEntryModifierFromAssets*>(fromReplacer.get());
			size_t nReplacers = 0;
			AssetsReplacer **ppReplacers = pAssetsFileReplacer->GetReplacers(nReplacers);
			this->replacers.resize(nReplacers);
			for (size_t i = 0; i < nReplacers; ++i)
				this->replacers[i].pReplacer = std::shared_ptr<AssetsReplacer>(ppReplacers[i], GenericReplacerOwnershipWrapper(fromReplacer));
		}
		break;
	case BundleReplacer_BundleEntryModifierFromBundle:
		{
			this->fileType = FileContext_Bundle;
			BundleEntryModifierFromBundle *pBundleFileReplacer = reinterpret_cast<BundleEntryModifierFromBundle*>(fromReplacer.get());
			size_t nReplacers = 0;
			BundleReplacer **ppReplacers = pBundleFileReplacer->GetReplacers(nReplacers);
			for (size_t i = 0; i < nReplacers; ++i)
			{
				auto pChildReplacer = std::shared_ptr<BundleReplacer>(ppReplacers[i], GenericReplacerOwnershipWrapper(fromReplacer));
				if (ppReplacers[i]->GetType() == BundleReplacement_AddOrModify)
				{
					if (dynamic_cast<BundleEntryModifierFromAssets*>(ppReplacers[i]) != nullptr)
					{
						BundleEntryModifierFromAssets* pBundleModifierFromAssets = reinterpret_cast<BundleEntryModifierFromAssets*>(ppReplacers[i]);
						uint32_t subFileID = pBundleModifierFromAssets->GetFileID();
						this->subFiles.push_back(VisibleFileEntry(appContext, pChildReplacer, BundleReplacer_BundleEntryModifierFromAssets));
						if (subFileID != (uint32_t)-1)
						{
							FileContextInfo_ptr pChildContextInfo = appContext.getContextInfo(subFileID);
							if (pChildContextInfo != nullptr)
								this->subFiles.back().pContextInfo = pChildContextInfo;
						}
					}
					else if (dynamic_cast<BundleEntryModifierFromBundle*>(ppReplacers[i]) != nullptr)
					{
						this->subFiles.push_back(VisibleFileEntry(appContext, pChildReplacer, BundleReplacer_BundleEntryModifierFromBundle));
					}
					else if (dynamic_cast<BundleEntryModifierByResources*>(ppReplacers[i]) != nullptr
						&& ppReplacers[i]->RequiresEntryReader())
					{
						//This resources replacer is based on an existing bundle entry, i.e. requires the original file reader to work.
						// If !ppReplacers[i]->RequiresEntryReader(), the file may not exist (but it could, e.g. if all resources are replaced).
						this->subFiles.push_back(VisibleFileEntry(appContext, pChildReplacer, BundleReplacer_BundleEntryModifierByResources));
					}
					else
						this->replacers.push_back(VisibleReplacerEntry(pChildReplacer));
				}
				else
					this->replacers.push_back(VisibleReplacerEntry(pChildReplacer));
			}
		}
		break;
	case BundleReplacer_BundleEntryModifierByResources:
		{
			//Modified resource files just store the bundle replacer inside.
			//-> Top-level resource files have a bundle replacer with empty names.
			this->fileType = FileContext_Resources;
			//BundleEntryModifierByResources* pResourcesReplacer = reinterpret_cast<BundleEntryModifierByResources*>(fromReplacer.get());
			this->replacers.resize(1);
			this->replacers[0].pReplacer = fromReplacer;
		}
		break;
	default:
		throw std::domain_error("VisibleFileEntry constructed with an unknown/unsupported replacer type!");
	}
}
VisibleFileEntry::VisibleFileEntry(class AppContext &appContext, InstallerPackageAssetsDesc &installerPackageDesc)
			: treeViewEntry(0), pathNull(false)
{
	this->pathOrName.assign(installerPackageDesc.path);
	switch (installerPackageDesc.type)
	{
		case InstallerPackageAssetsType::Assets:
		{
			this->fileType = FileContext_Assets;
			this->replacers.resize(installerPackageDesc.replacers.size());
			for (size_t i = 0; i < installerPackageDesc.replacers.size(); ++i)
				this->replacers[i].pReplacer = installerPackageDesc.replacers[i];
		}
		break;
		case InstallerPackageAssetsType::Bundle:
		{
			this->fileType = FileContext_Bundle;
			for (size_t i = 0; i < installerPackageDesc.replacers.size(); ++i)
			{
				std::shared_ptr<BundleReplacer> pReplacer = std::reinterpret_pointer_cast<BundleReplacer>(installerPackageDesc.replacers[i]);
				if (pReplacer->GetType() == BundleReplacement_AddOrModify)
				{
					if (dynamic_cast<BundleEntryModifierFromAssets*>(pReplacer.get()) != nullptr)
					{
						this->subFiles.push_back(VisibleFileEntry(appContext, pReplacer, BundleReplacer_BundleEntryModifierFromAssets));
					}
					else if (dynamic_cast<BundleEntryModifierFromBundle*>(pReplacer.get()) != nullptr)
					{
						this->subFiles.push_back(VisibleFileEntry(appContext, pReplacer, BundleReplacer_BundleEntryModifierFromBundle));
					}
					else if (dynamic_cast<BundleEntryModifierByResources*>(pReplacer.get()) != nullptr
						&& pReplacer->RequiresEntryReader())
					{
						this->subFiles.push_back(VisibleFileEntry(appContext, pReplacer, BundleReplacer_BundleEntryModifierByResources));
					}
					else
						this->replacers.push_back(VisibleReplacerEntry(std::move(pReplacer)));
				}
				else
					this->replacers.push_back(VisibleReplacerEntry(std::move(pReplacer)));
			}
		}
		break;
		case InstallerPackageAssetsType::Resources:
		{
			this->fileType = FileContext_Resources;
			if (installerPackageDesc.replacers.size() != 1
				|| dynamic_cast<BundleEntryModifierByResources*>(installerPackageDesc.replacers[0].get()) == nullptr)
			{
				throw std::invalid_argument("VisibleFileEntry: Resources installer package entry does not consist of a singular resources bundle replacer!");
			}
			this->replacers.resize(1);
			this->replacers[0].pReplacer = installerPackageDesc.replacers[0];
		}
		break;
		default:
			throw std::domain_error("VisibleFileEntry constructed with an unknown/unsupported installer package entry type!");
	}
}

std::shared_ptr<BundleReplacer> VisibleFileEntry::produceBundleReplacer()
{
	struct {
		std::vector<std::shared_ptr<GenericReplacer>> subOwnerships;
		void operator()(AssetsReplacer *del)
		{
			FreeAssetsReplacer(del);
			subOwnerships.clear();
		}
		void operator()(BundleReplacer *del)
		{
			FreeBundleReplacer(del);
			subOwnerships.clear();
		}
	} bundleModifierDeleter;
	switch (this->fileType)
	{
	case FileContext_Bundle:
		{
			std::vector<BundleReplacer*> childReplacers;
			for (size_t i = 0; i < this->replacers.size(); ++i)
			{
				bundleModifierDeleter.subOwnerships.push_back(this->replacers[i].pReplacer);
				childReplacers.push_back(reinterpret_cast<BundleReplacer*>(this->replacers[i].pReplacer.get()));
			}
			for (size_t i = 0; i < this->subFiles.size(); ++i)
			{
				std::shared_ptr<BundleReplacer> pChildReplacer = this->subFiles[i].produceBundleReplacer();
				childReplacers.push_back(pChildReplacer.get());
				bundleModifierDeleter.subOwnerships.push_back(std::move(pChildReplacer));
			}
			return std::shared_ptr<BundleReplacer>(
				MakeBundleEntryModifierFromBundle(this->pathNull ? nullptr : this->pathOrName.c_str(),
				    this->newName.c_str(), childReplacers.data(), childReplacers.size(),
					(unsigned int)-1), bundleModifierDeleter);
		}
		break;
	case FileContext_Assets:
		{
			std::vector<AssetsReplacer*> childReplacers;
			for (size_t i = 0; i < this->replacers.size(); ++i)
			{
				bundleModifierDeleter.subOwnerships.push_back(this->replacers[i].pReplacer);
				childReplacers.push_back(reinterpret_cast<AssetsReplacer*>(this->replacers[i].pReplacer.get()));
			}
			return std::shared_ptr<BundleReplacer>(
				MakeBundleEntryModifierFromAssets(this->pathNull ? nullptr : this->pathOrName.c_str(),
				    this->newName.c_str(), nullptr,
					childReplacers.data(), childReplacers.size(), 0), bundleModifierDeleter);
		}
		break;
	case FileContext_Resources:
		{
			if (this->replacers.size() != 1
				|| dynamic_cast<BundleEntryModifierByResources*>(this->replacers[0].pReplacer.get()) == nullptr)
			{
				throw std::invalid_argument("VisibleFileEntry: Resources file entry does not consist of a singular resources bundle replacer!");
			}
			return std::reinterpret_pointer_cast<BundleReplacer>(this->replacers[0].pReplacer);
		}
		break;
	default:
		throw std::domain_error("VisibleFileEntry::produceBundleReplacer - unsupported file type!");
	}
}

