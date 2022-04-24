#pragma once
#include "api.h"
#include "FileContext.h"
#include "../ModInstaller/InstallerDataFormat.h"
#include <assert.h>
#include <exception>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <stdint.h>

//Note: The UIEntryType members are set to nullptr by default.

struct VisibleReplacerEntry
{
	//Optional, used to ensure no contents of replacers are freed, such as file readers.
	//Do not use its state, as external threads could add replacers, etc. at any time.
	std::shared_ptr<void> pContextInfo;

	uintptr_t treeItem;
	std::shared_ptr<GenericReplacer> pReplacer;
	inline VisibleReplacerEntry() : treeItem(NULL) {}
	inline VisibleReplacerEntry(std::shared_ptr<GenericReplacer> pReplacer, std::shared_ptr<void> pContextInfo = nullptr)
		: pContextInfo(std::move(pContextInfo)), pReplacer(std::move(pReplacer)), treeItem(NULL)
	{}
};
class VisibleFileEntry
{
	void constructFromReplacer(class AppContext &appContext, std::shared_ptr<BundleReplacer> &fromReplacer, unsigned int type);

public:
	//Optional. Do not use its state, as external threads could add replacers, etc. at any time.
	std::shared_ptr<void> pContextInfo;

	//Determines the type of replacers (AssetsReplacer, BundleReplacer).
	EFileContextType fileType;
	//For assets: All AssetsReplacers for the file.
	//For bundles: Certain BundleReplacers (e.g. for generic files) that are not based on an existing file.
	// -> Note: The BundleReplacers may have a bundle list index other than -1, but it will not be saved anyway.
	//For resources: A single entry containing a BundleEntryModifierByResources that represents the whole resources file.
	std::vector<VisibleReplacerEntry> replacers;
	//For bundles: All sub files not represented by a BundleReplacer in replacers.
	std::vector<VisibleFileEntry> subFiles;

	//For base entries (not included in some parent's subFiles vector): Full file path.
	//For child entries: Name of the bundle entry.
	std::string pathOrName; bool pathNull;
	//For bundle child entries: New name (as in the BundleReplacer).
	std::string newName;

	uintptr_t treeViewEntry;
	inline VisibleFileEntry() : fileType(FileContext_Generic), pathNull(true), treeViewEntry((uintptr_t)-1) {}
	UABE_Generic_API VisibleFileEntry(class AppContext &appContext, std::shared_ptr<class FileContextInfo> contextInfo);
	UABE_Generic_API VisibleFileEntry(class AppContext &appContext, InstallerPackageAssetsDesc &installerPackageDesc);
	//Generate a VisibleFileEntry from a BundleEntryModifierFromAssets, BundleEntryModifierFromBundle or BundleEntryModifierByResources.
	//-> type: EBundleReplacers (InternalBundleReplacer.h)
	VisibleFileEntry(class AppContext& appContext, std::shared_ptr<BundleReplacer>& fromReplacer, unsigned int type);

	//Merges a VisibleFileEntry (other) into this one.
	//T: Callable bool (VisibleReplacerEntry& existing,const VisibleReplacerEntry& other)
	// -> Returns true <-> mergeWith will set existing.pReplacer = other.pReplacer, and otherwise does not change existing.
	// -> The callee takes care of removing or updating existing.treeItem before returning true.
	template<class T> void mergeWith(VisibleFileEntry &other, T& resolveConflict)
	{
		assert(this->fileType == other.fileType);
		if (this->fileType != other.fileType)
			return;
		switch (this->fileType)
		{
		case FileContext_Bundle:
			{
				//Should be better than Theta(N^2), disregarding the recursive calls.
				std::unordered_map<std::string, size_t> selfReplacersByName(this->replacers.size() * 2);
				for (size_t iThis = 0; iThis < this->replacers.size(); ++iThis)
				{
					BundleReplacer *pReplacer = reinterpret_cast<BundleReplacer*>(this->replacers[iThis].pReplacer.get());
					const char *replacedName = pReplacer->GetOriginalEntryName();
					if (replacedName == nullptr) replacedName = pReplacer->GetEntryName();
					if (replacedName != nullptr)
						selfReplacersByName[std::string(replacedName)] = iThis;
				}
				for (size_t iOther = 0; iOther < other.replacers.size(); ++iOther)
				{
					BundleReplacer *pReplacer = reinterpret_cast<BundleReplacer*>(other.replacers[iOther].pReplacer.get());
					const char *replacedName = pReplacer->GetOriginalEntryName();
					if (replacedName == nullptr) replacedName = pReplacer->GetEntryName();
					auto selfReplIt = selfReplacersByName.end();
					if (replacedName != nullptr)
						selfReplIt = selfReplacersByName.find(std::string(replacedName));
					if (selfReplIt == selfReplacersByName.end())
					{
						//Insert new from other.
						this->replacers.push_back(other.replacers[iOther]);
						this->replacers.back().treeItem = 0;
					}
					else if (true == resolveConflict(this->replacers[selfReplIt->second], other.replacers[iOther]))
					{
						//Replace by other.
						this->replacers[selfReplIt->second].pContextInfo = other.replacers[iOther].pContextInfo;
						this->replacers[selfReplIt->second].pReplacer = other.replacers[iOther].pReplacer;
					}
				}
			

				std::unordered_map<std::string, size_t> selfSubFilesByName(this->subFiles.size() * 2);
				for (size_t iThis = 0; iThis < this->subFiles.size(); ++iThis)
					selfSubFilesByName[this->subFiles[iThis].pathNull ? this->subFiles[iThis].newName : this->subFiles[iThis].pathOrName] = iThis;
				for (size_t iOther = 0; iOther < other.subFiles.size(); ++iOther)
				{
					std::string otherPath = this->subFiles[iOther].pathNull ? this->subFiles[iOther].newName : this->subFiles[iOther].pathOrName;
					auto selfSubIt = selfSubFilesByName.find(otherPath);
					if (selfSubIt == selfSubFilesByName.end())
					{
						//Deep copy other (no more recursive conflicts).
						struct {
							void operator()(VisibleFileEntry &entry)
							{
								for (size_t i = 0; i < entry.replacers.size(); ++i)
									entry.replacers[i].treeItem = 0;
								entry.treeViewEntry = 0;
								for (size_t i = 0; i < entry.subFiles.size(); ++i)
									(*this)(entry.subFiles[i]);
							}
						} nullUIData;
						this->subFiles.push_back(other.subFiles[iOther]);
						nullUIData(this->subFiles.back()); //Null any UI info that needs to be regenerated.
					}
					else
					{
						//Recursively merge own subFile with other subFile.
						this->subFiles[selfSubIt->second].mergeWith(other.subFiles[iOther], resolveConflict);
					}
				}
			}
			break;
		case FileContext_Assets:
			{
				//Roughly O(NlogN)
				std::map<uint64_t, size_t> selfReplacersByPathID;
				size_t iOwnDependenciesReplacer = SIZE_MAX;
				for (size_t iThis = 0; iThis < this->replacers.size(); ++iThis)
				{
					AssetsReplacer *pReplacer = reinterpret_cast<AssetsReplacer*>(this->replacers[iThis].pReplacer.get());
					if (pReplacer->GetType() == AssetsReplacement_AddOrModify || pReplacer->GetType() == AssetsReplacement_Remove)
						selfReplacersByPathID[reinterpret_cast<AssetsEntryReplacer*>(pReplacer)->GetPathID()] = iThis;
					else if (pReplacer->GetType() == AssetsReplacement_Dependencies)
						iOwnDependenciesReplacer = iThis;
				}
				for (size_t iOther = 0; iOther < other.replacers.size(); ++iOther)
				{
					AssetsReplacer *pReplacer = reinterpret_cast<AssetsReplacer*>(other.replacers[iOther].pReplacer.get());
					bool add = false;
					size_t replaceIndex = SIZE_MAX;
					switch (pReplacer->GetType())
					{
						case AssetsReplacement_AddOrModify:
						case AssetsReplacement_Remove:
						{
							auto selfReplIt = selfReplacersByPathID.find(reinterpret_cast<AssetsEntryReplacer*>(pReplacer)->GetPathID());
							if (selfReplIt == selfReplacersByPathID.end())
								add = true;
							else if (true == resolveConflict(this->replacers[selfReplIt->second], other.replacers[iOther]))
								replaceIndex = selfReplIt->second;
						}
						break;
						case AssetsReplacement_Dependencies:
						{
							if (iOwnDependenciesReplacer == SIZE_MAX)
								add = true;
							else
								replaceIndex = iOwnDependenciesReplacer;
						}
						break;
					}
					if (add)
					{
						//Insert new from other.
						this->replacers.push_back(other.replacers[iOther]);
						this->replacers.back().treeItem = 0;
					}
					else if (replaceIndex != SIZE_MAX
						&& true == resolveConflict(this->replacers[replaceIndex], other.replacers[iOther]))
					{
						//Replace by other.
						this->replacers[replaceIndex].pContextInfo = other.replacers[iOther].pContextInfo;
						this->replacers[replaceIndex].pReplacer = other.replacers[iOther].pReplacer;
					}
				}
			}
			break;
		case FileContext_Resources:
			{
				if (this->replacers.empty())
				{
					assert(false); //Should never happen for Resources files.
					if (other.replacers.size() == 1)
					{
						this->replacers.resize(1);
						this->replacers[0].pContextInfo = other.replacers[0].pContextInfo;
						this->replacers[0].pReplacer = other.replacers[0].pReplacer;
					}
				}
				else if (this->replacers.size() == 1 && other.replacers.size() == 1)
				{
					if (true == resolveConflict(this->replacers[0], other.replacers[0]))
					{
						//Replace by other.
						this->replacers[0].pContextInfo = other.replacers[0].pContextInfo;
						this->replacers[0].pReplacer = other.replacers[0].pReplacer;
					}
				}
				else
					assert(false); //Should never happen for Resources files.
			}
			break;
		default:
			throw std::domain_error("VisibleFileEntry merging unknown file types!");
		}
	}
		
	UABE_Generic_API std::shared_ptr<BundleReplacer> produceBundleReplacer();
};
