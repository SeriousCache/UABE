#include "stdafx.h"
#include "../AssetsTools/AssetsFileFormat.h"
#include "../AssetsTools/ResourceManagerFile.h"

#define fwwrite(source,count) {if (pWriter->Write(count, source) != count) return false; filePos += count;}
#define fwalign() {uint32_t zero = 0; uint32_t nAlign = _fmalign(filePos) - filePos; if (nAlign != 0 && pWriter->Write(nAlign, &zero) != nAlign) return false; filePos += nAlign;}
#define _fmalign(fpos) (fpos + 3) & ~3
#define fmalign() {*filePos = _fmalign(*filePos);}

ASSETSTOOLS_API bool ResourceManagerFile::Write(IAssetsWriter *pWriter, size_t *size)
{
	if (!isRead)
		return false;
	size_t filePos = 0;
	int iTmp;

	uint32_t containerArrayLen = (uint32_t)containers.size();
	fwwrite(&containerArrayLen, 4);
	for (size_t i = 0; i < containerArrayLen; i++)
	{
		ResourceManager_ContainerData &cd = containers[i];

		iTmp = cd.name.size();
		if (iTmp < 0) iTmp = 0;
		fwwrite(&iTmp, 4);
		fwwrite(cd.name.c_str(), iTmp);
		fwalign();

		fwwrite(&cd.ids.fileId, 4);
		fwwrite(&cd.ids.pathId, ((unityVersion>=0x0E)?8:4));
	}
	
	uint32_t dependenciesArrayLen = (uint32_t)dependencyLists.size();
	fwwrite(&dependenciesArrayLen, 4);
	for (size_t i = 0; i < dependenciesArrayLen; i++)
	{
		ResourceManager_AssetDependencies &dependencyList = dependencyLists[i];
		fwwrite(&dependencyList.asset.fileId, 4);
		fwwrite(&dependencyList.asset.pathId, ((unityVersion>=0x0E)?8:4));

		uint32_t dependencyCount = dependencyList.dependencies.size();
		fwwrite(&dependencyCount, 4);
		for (int k = 0; k < dependencyCount; k++)
		{
			ResourceManager_PPtr &pptr = dependencyList.dependencies[k];
			fwwrite(&pptr.fileId, 4);
			fwwrite(&pptr.pathId, ((unityVersion>=0x0E)?8:4));
		}
	}

	if (size)
		*size = filePos;
	return true;
}

ASSETSTOOLS_API size_t ResourceManagerFile::GetFileSize()
{
	if (!isRead)
		return -1;
	int ret = 0;

	ret += 4; //containerArrayLen
	for (size_t i = 0; i < containers.size(); i++)
		ret += _fmalign(containers[i].name.size()); //strlen(containerArray::name)
	ret += (4 + ((unityVersion>=0x0E)?12:8)) * containers.size(); //sizeof(containerArray::ids)
	
	ret += 4; //dependenciesArrayLen
	ret += dependencyLists.size() * (4 + ((unityVersion>=0x0E)?12:8)); //sizeof(assetDependency::asset)
	for (size_t i = 0; i < dependencyLists.size(); i++)
		ret += (dependencyLists[i].dependencies.size() * ((unityVersion>=0x0E)?12:8)); //assetDependency::dependencies
	
	return ret;
}

ASSETSTOOLS_API ResourceManagerFile::ResourceManagerFile()
{
	isModified = false;
	isRead = false;
	unityVersion = 0;
}

ASSETSTOOLS_API void ResourceManagerFile::Clear()
{
	isRead = false;
	containers.clear();
	dependencyLists.clear();
}

ASSETSTOOLS_API ResourceManagerFile::~ResourceManagerFile()
{
	this->Clear();
}

//#define fmxread(target,count,onerr) {if ((*filePos + count) > dataLen) {memset(target, 0, count);onerr;} else {memcpy(target, &((uint8_t*)data)[*filePos], count); *filePos = *filePos + count;}}
//#define fmread(target,count) fmxread(target,count,0)
inline bool fmread(void *target, size_t count, void *data, size_t dataLen, size_t &dataPos)
{
	if ((dataPos + count) > dataLen)
	{
		memset(target, 0, count);
		return false;
	}
	else
	{
		memcpy(target, &((uint8_t*)data)[dataPos], count);
		dataPos += count;
		return true;
	}
}
#include "AssetTypeClass.h"
ASSETSTOOLS_API bool ResourceManagerFile::Read(AssetTypeValueField *pBase)
{
	/*
      ResourceManager Base
        map m_Container
         Array Array (IsArray)
          int size
          pair data
           string first
            Array Array (IsArray)
             int size
             char data
           PPtr<Object> second
            int m_FileID
            SInt64 m_PathID
        vector m_DependentAssets
         Array Array (IsArray)
          int size
          ResourceManager_Dependency data
           PPtr<Object> m_Object
            int m_FileID
            SInt64 m_PathID
           vector m_Dependencies
            Array Array (IsArray)
             int size
             PPtr<Object> data
              int m_FileID
              SInt64 m_PathID
	*/
	Clear();
	unityVersion = -1;
	isModified = false;
	AssetTypeValueField *pContainers = pBase->Get("m_Container")->Get("Array");

	uint32_t containerArrayLen;
	if (pContainers->IsDummy())
	{
		containerArrayLen = 0;
		Clear();
		return false;
	}
	else
		containerArrayLen = pContainers->GetChildrenCount();
	containers.resize(containerArrayLen);
	for (uint32_t i = 0; i < containerArrayLen; i++)
	{
		AssetTypeValueField *pContainerItem = pContainers->Get(i);
		AssetTypeValue *nameValue = pContainerItem->Get(0U)->GetValue(); //"first"
		if (nameValue && nameValue->AsString())
			containers[i].name.assign(nameValue->AsString());
		else
		{
			Clear();
			return false;
		}
		AssetTypeValueField *pContainerPPtr = pContainerItem->Get(1U); //"second"
		AssetTypeValueField *pContainerPPtr_FileID = pContainerPPtr->Get(0U); //"m_FileID"
		AssetTypeValueField *pContainerPPtr_PathID = pContainerPPtr->Get(1U); //"m_PathID"
		if (pContainerPPtr_FileID->GetValue() && pContainerPPtr_FileID->GetValue()->GetType() == ValueType_Int32
			&& pContainerPPtr_PathID->GetValue() && (pContainerPPtr_PathID->GetValue()->GetType() == ValueType_Int32 || pContainerPPtr_PathID->GetValue()->GetType() == ValueType_Int64))
		{
			containers[i].ids.fileId = pContainerPPtr_FileID->GetValue()->AsInt();
			containers[i].ids.pathId = pContainerPPtr_PathID->GetValue()->AsInt64();
		}
		else
		{
			Clear();
			return false;
		}
	}

	AssetTypeValueField *pDependencyLists = pBase->Get("m_DependentAssets")->Get("Array");
	uint32_t dependencyListCount;
	if (pDependencyLists->IsDummy() || !pDependencyLists->GetValue() || !pDependencyLists->GetValue()->AsArray())
	{
		Clear();
		return false;
	}
	else
		dependencyListCount = pDependencyLists->GetChildrenCount();

	dependencyLists.resize(dependencyListCount);
	for (uint32_t i = 0; i < dependencyListCount; i++)
	{
		AssetTypeValueField *pDependencyList = pDependencyLists->Get(i);
		ResourceManager_AssetDependencies &dependencyList = dependencyLists[i];
		AssetTypeValueField *pObjectFileID = pDependencyList->Get(0U)->Get(0U);
		AssetTypeValueField *pObjectPathID = pDependencyList->Get(0U)->Get(1U);
		if (pObjectFileID->GetValue() && pObjectFileID->GetValue()->GetType() == ValueType_Int32
			&& pObjectPathID->GetValue() && (pObjectPathID->GetValue()->GetType() == ValueType_Int32 || pObjectPathID->GetValue()->GetType() == ValueType_Int64))
		{
			dependencyList.asset.fileId = pObjectFileID->GetValue()->AsInt();
			dependencyList.asset.pathId = pObjectPathID->GetValue()->AsInt64();
		}
		else
		{
			Clear();
			return false;
		}
		AssetTypeValueField *pDependencies = pDependencyList->Get(1U)->Get(0U);
		if (pDependencies->GetValue() && pDependencies->GetValue()->AsArray())
		{
			uint32_t dependencyCount = pDependencies->GetChildrenCount();
			dependencyList.dependencies.resize(dependencyCount);
			for (uint32_t k = 0; k < dependencyCount; k++)
			{
				AssetTypeValueField *pDependency = pDependencies->Get(k);
				ResourceManager_PPtr &dependency = dependencyList.dependencies[k];

				AssetTypeValueField *pDependencyFileID = pDependency->Get(0U);
				AssetTypeValueField *pDependencyPathID = pDependency->Get(1U);
				if (pDependencyFileID->GetValue() && pDependencyFileID->GetValue()->GetType() == ValueType_Int32
					&& pDependencyPathID->GetValue() && (pDependencyPathID->GetValue()->GetType() == ValueType_Int32 || pDependencyPathID->GetValue()->GetType() == ValueType_Int64))
				{
					dependency.fileId = pDependencyFileID->GetValue()->AsInt();
					dependency.pathId = pDependencyPathID->GetValue()->AsInt64();
				}
			}
		}
		else
		{
			Clear();
			return false;
		}
	}

	isRead = true;
	return true;

}
ASSETSTOOLS_API void ResourceManagerFile::Read(void *data, size_t dataLen, size_t *filePos, int assetsVersion, bool bigEndian)
{
	Clear();
	unityVersion = assetsVersion;
	isModified = false;

	bool readErr = false;
	uint32_t containerArrayLen = 0;
	readErr |= !fmread(&containerArrayLen, 4, data, dataLen, *filePos);
	if (bigEndian)
		SwapEndians_(containerArrayLen);
	if (readErr)
	{
		Clear();
		return;
	}
	containers.resize(containerArrayLen);
	for (size_t i = 0; i < containerArrayLen; i++)
	{
		size_t assetNameSize = 0; readErr |= !fmread(&assetNameSize, 4, data, dataLen, *filePos);
		if (bigEndian)
			SwapEndians_(assetNameSize);
		if ((*filePos + assetNameSize) > dataLen)
		{
			Clear();
			return;
		}
		std::unique_ptr<char[]> assetName(new char[assetNameSize+1]);
		readErr |= !fmread(assetName.get(), assetNameSize, data, dataLen, *filePos); assetName[assetNameSize] = 0;
		containers[i].name.assign(assetName.get());
		fmalign();

		readErr |= !fmread(&containers[i].ids.fileId, 4, data, dataLen, *filePos);
		if (bigEndian)
			SwapEndians_(containers[i].ids.fileId);
		containers[i].ids.pathId = 0;
		if (assetsVersion>=0x0E)
		{
			readErr |= !fmread(&containers[i].ids.pathId, 8, data, dataLen, *filePos);
			if (bigEndian)
				SwapEndians_(containers[i].ids.pathId);
		}
		else
		{
			readErr |= !fmread(&containers[i].ids.pathId, 4, data, dataLen, *filePos);
			if (bigEndian)
				SwapEndians_(*(uint32_t*)&containers[i].ids.pathId);
		}
		if (readErr)
		{
			Clear();
			return;
		}
	}

	uint32_t dependenciesArrayLen = 0;
	readErr |= !fmread(&dependenciesArrayLen, 4, data, dataLen, *filePos);
	if (bigEndian)
		SwapEndians_(dependenciesArrayLen);
	if (readErr)
	{
		Clear();
		return;
	}
	dependencyLists.resize(dependenciesArrayLen);
	for (size_t i = 0; i < dependenciesArrayLen; i++)
	{
		ResourceManager_AssetDependencies &dependencyList = dependencyLists[i];
		readErr |= !fmread(&dependencyList.asset.fileId, 4, data, dataLen, *filePos);
		if (bigEndian)
			SwapEndians_(dependencyList.asset.fileId);
		dependencyList.asset.pathId = 0;
		if (assetsVersion>=0x0E)
		{
			readErr |= !fmread(&dependencyList.asset.pathId, 8, data, dataLen, *filePos);
			if (bigEndian)
				SwapEndians_(dependencyList.asset.pathId);
		}
		else
		{
			readErr |= !fmread(&dependencyList.asset.pathId, 4, data, dataLen, *filePos);
			if (bigEndian)
				SwapEndians_(*(uint32_t*)&dependencyList.asset.pathId);
		}
		
		uint32_t dependencyCount = 0;
		readErr |= !fmread(&dependencyCount, 4, data, dataLen, *filePos);
		if (bigEndian)
			SwapEndians_(dependencyCount);
		if (readErr || (*filePos + ((size_t)dependencyCount) * (assetsVersion>=0x0E ? 8 : 4)) > dataLen)
		{
			Clear();
			return;
		}
		dependencyList.dependencies.resize(dependencyCount);
		for (size_t k = 0; k < dependencyCount; k++)
		{
			ResourceManager_PPtr &pptr = dependencyList.dependencies[k];
			readErr |= !fmread(&pptr.fileId, 4, data, dataLen, *filePos);
			if (bigEndian)
				SwapEndians_(pptr.fileId);
			pptr.pathId = 0;
			if (assetsVersion>=0x0E)
			{
				readErr |= !fmread(&pptr.pathId, 8, data, dataLen, *filePos);
				if (bigEndian)
					SwapEndians_(pptr.pathId);
			}
			else
			{
				readErr |= !fmread(&pptr.pathId, 4, data, dataLen, *filePos);
				if (bigEndian)
					SwapEndians_(*(uint32_t*)&pptr.pathId);
			}
		}
		if (readErr)
		{
			Clear();
			return;
		}
	}

	isRead = true;
}