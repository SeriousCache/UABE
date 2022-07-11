#include "Mesh.h"
#include "AssimpMesh.h"
#include "../libStringConverter/convert.h"
#include "../UABE_Generic/AssetPluginUtil.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/AppContext.h"
#include "../AssetsTools/AssetsReplacer.h"
#include "../AssetsTools/AssetsFileTable.h"
#include "../AssetsTools/ResourceManagerFile.h"
#include <unordered_map>
#include <array>
#include <format>
#include <atomic>
#include <mutex>

//void OpenExportFile(HWND hParentWnd, IPluginInterface *pInterface,
//	char *outFolderPath, const char *fileName, IAssetInterface *pAsset, const char *extension, const wchar_t *extensionFilters,
//	char **prevAssetNames, size_t i, size_t assetCount,
//	IAssetsWriter *&pWriter);

//Throws an AssetUtilError if the GameObject type cannot be resolved.
bool GetTransformName(AppContext &appContext, AssetIdentifier &asset, TypeTemplateCache &typeCache,
	AssetTypeValueField *pTransformValue, std::string &name)
{
	name.clear();
	AssetTypeValueField *pGameObjectField = pTransformValue->Get("m_GameObject");
	AssetTypeValueField *pGameObjectFileIDField = pGameObjectField->Get("m_FileID");
	AssetTypeValueField *pGameObjectPathIDField = pGameObjectField->Get("m_PathID");
	if (!pGameObjectFileIDField->GetValue() || !pGameObjectPathIDField->GetValue())
		return false;
	if (!asset.resolve(appContext))
		return false;
	unsigned int targetFileID = asset.pFile->resolveRelativeFileID(pGameObjectFileIDField->GetValue()->AsUInt());
	AssetIdentifier gameObjectAsset(targetFileID, (pathid_t)pGameObjectPathIDField->GetValue()->AsUInt64());
	if (!gameObjectAsset.resolve(appContext))
		return false;
	IAssetsReader_ptr pAssetReader = gameObjectAsset.makeReader();
	if (pAssetReader == nullptr)
		return false;
	QWORD assetSize = 0;
	if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
		return false;
	AssetTypeTemplateField &gameObjectBase = typeCache.getTemplateField(appContext, gameObjectAsset);
	AssetTypeTemplateField *pGameObjectBase = &gameObjectBase;
	AssetTypeInstance gameObjectInstance(1, &pGameObjectBase, assetSize, pAssetReader.get(), gameObjectAsset.isBigEndian());

	AssetTypeValueField *pBaseField = gameObjectInstance.GetBaseField();
	AssetTypeValueField *pNameField = nullptr;
	if (pBaseField && (pNameField = pBaseField->Get("m_Name"))->GetValue() && pNameField->GetValue()->AsString())
	{
		name.assign(pNameField->GetValue()->AsString());
		return true;
	}

	return false;
}
struct BoneLocation
{
	BoneLocation(){hierarchyIndex = (unsigned int)-1; fileID = 0; pathID = 0;}
	unsigned int hierarchyIndex;
	unsigned int fileID; pathid_t pathID;
};
class BoneHierarchy
{
public:
	BoneHierarchy(){level = -1; fileID = 0; pathID = 0;}
	std::string name;
	int level;
	unsigned int fileID; pathid_t pathID;
	aiMatrix4x4 transform; //relative to the parent transform
};
void RestoreBoneHierarchy(AppContext &appContext, TypeTemplateCache& typeCache,
	std::vector<BoneHierarchy> &output, std::vector<BoneLocation> &bones, BoneLocation &baseBoneTransform)
{
	class HierarchyStackEntry
	{
	public:
		AssetIdentifier transformAsset; //not necessarily a bone
		aiMatrix4x4 transform; //updated (i.e. multiplied with the current transform) if processedBase==true
		int level; //level of the next children fields (increased by 1 if !processedBase and transformDesc is a bone).
		bool processedBase = false; //says if transformDesc was already processed (i.e. checked if it is a bone and added to output in case it is).
		bool isBone = false; //assigned if processedBase==true
		std::unique_ptr<AssetTypeInstance> pInstance;
		AssetTypeValueField *pChildrenField = nullptr; //pInstance->GetBaseField()->Get("m_Children")->Get("Array")
		unsigned int curChildIndex = 0; //next transform child index to check in pChildrenField
	public:
		HierarchyStackEntry(unsigned int fileID, pathid_t pathID, const aiMatrix4x4 &transform, int level)
			: transformAsset(fileID, pathID), transform(transform), level(level)
		{}
	};
	std::vector<HierarchyStackEntry> stack;
	stack.push_back(HierarchyStackEntry(baseBoneTransform.fileID, baseBoneTransform.pathID, aiMatrix4x4(), 0));
	while (stack.size())
	{
		HierarchyStackEntry *pCurEntry = &stack[stack.size()-1];
		if ((stack.size() > 512)
			|| (output.size() > 0x7FFFFFFE) 
			|| (!pCurEntry->transformAsset.resolve(appContext)))
		{
			stack.pop_back();
			continue;
		}
		bool doFree = false;
		AssetTypeTemplateField& transformTemplate = typeCache.getTemplateField(appContext, pCurEntry->transformAsset);

		//If necessary, generate a type instance.
		if (!pCurEntry->pInstance)
		{
			IAssetsReader_ptr pAssetReader = pCurEntry->transformAsset.makeReader();
			if (pAssetReader == nullptr)
			{
				stack.pop_back();
				continue;
			}
			QWORD transformAssetSize = 0;
			if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(transformAssetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
			{
				stack.pop_back();
				continue;
			}
			AssetTypeTemplateField* pTransformTemplate = &transformTemplate;
			pCurEntry->pInstance.reset(
				new AssetTypeInstance(1, &pTransformTemplate, transformAssetSize, pAssetReader.get(), pCurEntry->transformAsset.isBigEndian()));
		}
		AssetTypeValueField *pBaseField;
		if ((pBaseField = pCurEntry->pInstance->GetBaseField()) == nullptr || pBaseField->IsDummy())
		{
			stack.pop_back();
			continue;
		}
		if (!pCurEntry->processedBase)
		{
			//Multiply this node's base transform (i.e. transform of the parent) with the local transform.
			AssetTypeValueField *pRotationField = pBaseField->Get("m_LocalRotation");
			AssetTypeValueField *pPositionField = pBaseField->Get("m_LocalPosition");
			AssetTypeValueField *pScaleField = pBaseField->Get("m_LocalScale");
			if (!pRotationField->IsDummy() && !pPositionField->IsDummy() && !pScaleField->IsDummy())
			{
				Quaternionf rot;
				Vector3f pos;
				Vector3f scale;
				if (rot.Read(pRotationField) && pos.Read(pPositionField) && scale.Read(pScaleField))
				{
					aiMatrix4x4 transform;
					ComposeMatrix(rot, pos, scale, transform);
					//Account for the negated x vertex coordinates (see AssimpMesh.cpp in the bind pose loop for a detailed explanation comment).
					//Even though the root node has no flipped transformation, we're not getting into trouble since it starts at (0 0 0).
					//The child nodes will have flipped x coordinates to match the flipped vertices.
					transform.a2 *= -1;
					transform.a3 *= -1;
					transform.a4 *= -1;
					transform.b1 *= -1;
					transform.c1 *= -1;
					pCurEntry->transform *= transform;
				}
			}

			//Find the matching bone entry and add it to the bone hierarchy (if it exists).
			BoneLocation *pBaseBoneEntry = nullptr;
			for (size_t i = 0; i < bones.size(); i++)
			{
				if (bones[i].hierarchyIndex == (unsigned int)-1
					&& bones[i].fileID == pCurEntry->transformAsset.fileID
					&& bones[i].pathID == pCurEntry->transformAsset.pathID)
				{
					pBaseBoneEntry = &bones[i];
					break;
				}
			}
			if (pBaseBoneEntry)
			{
				BoneHierarchy newOutEntry;
				newOutEntry.level = pCurEntry->level++; //Increase the level to assign to child transforms.
				newOutEntry.fileID = pCurEntry->transformAsset.fileID;
				newOutEntry.pathID = pCurEntry->transformAsset.pathID;
				newOutEntry.transform = pCurEntry->transform;
				pBaseBoneEntry->hierarchyIndex = (unsigned int)output.size();
				output.push_back(newOutEntry);
				//Set the bone name.
				GetTransformName(appContext, pCurEntry->transformAsset, typeCache, pBaseField, output[pBaseBoneEntry->hierarchyIndex].name);
				//Escape the bone name (should not inject XML tags / be multiple elements in a Collada Name_array or IDREF_array).
				for (size_t i = 0; i < output[pBaseBoneEntry->hierarchyIndex].name.size(); i++)
				{
					switch (output[pBaseBoneEntry->hierarchyIndex].name[i])
					{
					case ' ':
					case '<':
					case '>':
						output[pBaseBoneEntry->hierarchyIndex].name[i] = '_';
						break;
					}
				}

				pCurEntry->isBone = true; //Do not pass on the transformation matrix since it's always relative to the parent bone in the hierarchy.
			}
			pCurEntry->processedBase = true;
		}
		//Locate the child array of the transform.
		if (!pCurEntry->pChildrenField)
		{
			pCurEntry->pChildrenField = pBaseField->Get("m_Children")->Get("Array");
			if (pCurEntry->pChildrenField->GetValue() == nullptr || pCurEntry->pChildrenField->GetValue()->GetType() != ValueType_Array)
				pCurEntry->pChildrenField = nullptr;
		}
		if (pCurEntry->pChildrenField)
		{
			//Add a stack entry for the next valid child transform, or remove the current entry.
			bool addedNewEntry = false;
			for (; pCurEntry->curChildIndex < pCurEntry->pChildrenField->GetChildrenCount(); pCurEntry->curChildIndex++)
			{
				AssetTypeValueField *pCurChild = pCurEntry->pChildrenField->Get(pCurEntry->curChildIndex);
				AssetTypeValueField *pFileIDField = pCurChild->Get("m_FileID");
				AssetTypeValueField *pPathIDField = pCurChild->Get("m_PathID");
				if (pFileIDField->GetValue() && pPathIDField->GetValue())
				{
					unsigned int targetFileID = pCurEntry->transformAsset.pFile->resolveRelativeFileID(pFileIDField->GetValue()->AsUInt());
					if (targetFileID != 0)
					{
						pCurEntry->curChildIndex++;
						stack.push_back(HierarchyStackEntry(targetFileID,
							pPathIDField->GetValue()->AsInt64(),
							pCurEntry->isBone ? aiMatrix4x4() : pCurEntry->transform, pCurEntry->level)
						);
						pCurEntry = &stack[stack.size()-2]; //Precaution
						addedNewEntry = true;
						break;
					}
				}
			}
			if (!addedNewEntry && pCurEntry->curChildIndex >= pCurEntry->pChildrenField->GetChildrenCount())
				doFree = true;
		}
		else
			doFree = true;
		if (doFree)
		{
			stack.pop_back();
			continue;
		}
	}

}

//skeletonName should be unique and will be a prefix for all bone names!
void AddBoneHierarchyToScene(aiScene &scene, std::vector<BoneHierarchy> &bones, const char *skeletonName)
{
	if (!scene.mRootNode)
		scene.mRootNode = new aiNode("Root");
	aiNode *baseNode = new aiNode(skeletonName ? skeletonName : "");
	baseNode->mParent = scene.mRootNode;
	aiNode **newChildNodes = new aiNode*[scene.mRootNode->mNumChildren + 1];
	memcpy(newChildNodes, scene.mRootNode->mChildren, scene.mRootNode->mNumChildren * sizeof(aiNode*));
	newChildNodes[scene.mRootNode->mNumChildren++] = baseNode;
	delete[] scene.mRootNode->mChildren;
	scene.mRootNode->mChildren = newChildNodes;

	class NodeLevelStackEntry
	{
	public:
		aiNode *pNode;
		unsigned int childBufferSize;
		NodeLevelStackEntry(aiNode *pNode, unsigned int childBufferSize)
			: pNode(pNode), childBufferSize(childBufferSize)
		{}
	};
	std::vector<NodeLevelStackEntry> nodeLevelStack;
	nodeLevelStack.push_back(NodeLevelStackEntry(baseNode, 0));
	for (size_t i = 0; i < bones.size(); i++)
	{
		if (nodeLevelStack.size() == 0)
			break;
		NodeLevelStackEntry *pCurEntry = &nodeLevelStack[nodeLevelStack.size()-1];
		if (i > 0 && bones[i].level > bones[i-1].level)
		{
			if (bones[i].level != (bones[i-1].level + 1))
				break;
			if (pCurEntry->pNode->mNumChildren == 0)
				break;
			nodeLevelStack.push_back(NodeLevelStackEntry(pCurEntry->pNode->mChildren[pCurEntry->pNode->mNumChildren-1], 0));
			pCurEntry = &nodeLevelStack[nodeLevelStack.size()-1];
		}
		else if (i > 0 && bones[i].level < bones[i-1].level)
		{
			if (bones[i].level < 0)
				break;
			nodeLevelStack.erase(nodeLevelStack.end() - (bones[i-1].level - bones[i].level), nodeLevelStack.end());
			if (nodeLevelStack.size() == 0)
				break;
			pCurEntry = &nodeLevelStack[nodeLevelStack.size()-1];
		}
		if (pCurEntry->childBufferSize == 0)
		{
			size_t sameLevelCount = 1;
			for (size_t j = i+1; j < bones.size(); j++)
			{
				if (bones[j].level == bones[i].level)
					sameLevelCount++;
				else if (bones[j].level < bones[i].level)
					break;
			}
			if (sameLevelCount > 0x7FFFFFFE)
				break;
			pCurEntry->pNode->mNumChildren = 0;
			pCurEntry->pNode->mChildren = new aiNode*[sameLevelCount]();
			pCurEntry->childBufferSize = (unsigned int)sameLevelCount;
		}
		if ((pCurEntry->pNode->mNumChildren + 1) > pCurEntry->childBufferSize)
			break;
		aiNode *pNewNode = new aiNode(skeletonName ? (std::string(skeletonName) + "_" + bones[i].name) : bones[i].name);
		pNewNode->mParent = pCurEntry->pNode;
		pNewNode->mTransformation = bones[i].transform;
		pCurEntry->pNode->mChildren[pCurEntry->pNode->mNumChildren++] = pNewNode;
	}
}

class MeshDAEExportTask : public AssetExportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
	bool combineToSingleFile;
	std::mutex combinedSceneMutex;
	aiScene combinedScene;
	std::atomic_uint assetCounter;
public:
	MeshDAEExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir, bool combineToSingleFile,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export Mesh", ".dae", std::move(_baseDir), stopOnError, combineToSingleFile),
		appContext(appContext), combineToSingleFile(combineToSingleFile)
	{}
	void onCompletion(const std::string& outputPath, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
	{
		if (combineToSingleFile && combinedScene.mRootNode != nullptr && combinedScene.mNumMeshes > 0)
		{
			std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(outputPath.c_str(), true, true, RWOpenFlags_Immediately));
			if (pWriter == nullptr)
				throw AssetUtilError("Unable to create the output file.");
			if (!WriteScene(combinedScene, pWriter.get()))
				throw AssetUtilError("Unable to write the data.");
		}
	}

	bool exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
	{
		if (desc.asset.pFile == nullptr)
			throw AssetUtilError("Unable to find the target .assets file.");

		IAssetsReader_ptr pAssetReader = desc.asset.makeReader();
		if (pAssetReader == nullptr)
			throw AssetUtilError("Unable to read the asset.");
		QWORD assetSize = 0;
		if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
			throw AssetUtilError("Unable to read the asset.");

		AssetTypeTemplateField& templateBaseInitial = templateCache.getTemplateField(appContext, desc.asset);
		AssetTypeTemplateField* pTemplateBase = &templateBaseInitial;

		AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
		AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
		if (pBaseField == nullptr || pBaseField->IsDummy())
			throw AssetUtilError("Unable to deserialize the asset.");

		AssetIdentifier actualMeshAsset = desc.asset;
			
		std::vector<std::string> boneNames;
		std::vector<BoneHierarchy> bones;
		
		if (desc.asset.getClassID(appContext) != 0x2B //'heuristic' comparison with the original Mesh ID to save GetClassName_ calls.
			&& desc.asset.pFile->GetClassName_(appContext, desc.asset.getClassID(), desc.asset.getMonoScriptID(), &desc.asset)
			    == "SkinnedMeshRenderer")
		{
			AssetTypeValueField *pMeshField = pBaseField->Get("m_Mesh"); //PPtr<Mesh>
			AssetTypeValueField *pMeshFileIDField = pMeshField->Get("m_FileID"); //int
			AssetTypeValueField *pMeshPathIDField = pMeshField->Get("m_PathID"); //int or SInt64
	
			AssetTypeValueField *pBonesField = pBaseField->Get("m_Bones")->Get("Array"); //Array<PPtr<Transform>>
	
			AssetTypeValueField *pRootBoneField = pBaseField->Get("m_RootBone"); //PPtr<Transform>
			AssetTypeValueField *pRootBoneFileIDField = pRootBoneField->Get("m_FileID"); //int
			AssetTypeValueField *pRootBonePathIDField = pRootBoneField->Get("m_PathID"); //int or SInt64
	
			if (pBonesField->GetValue() && pBonesField->GetValue()->GetType() == ValueType_Array
				&& pRootBoneFileIDField->GetValue() && pRootBoneFileIDField->GetValue())
			{
				//Reconstruct the bone hierarchy.
				AssetTypeTemplateField transformTemplateBase, gameObjectTemplateBase;
				bones.reserve(pBonesField->GetChildrenCount());
				std::vector<BoneLocation> boneLocations(pBonesField->GetChildrenCount());
				for (unsigned int i = 0; i < pBonesField->GetChildrenCount(); i++)
				{
					AssetTypeValueField *pCurEntry = pBonesField->Get(i);
					AssetTypeValueField *pFileIDField = pCurEntry->Get("m_FileID");
					AssetTypeValueField *pPathIDField = pCurEntry->Get("m_PathID");
					if (pFileIDField->GetValue() && pPathIDField->GetValue())
					{
						boneLocations[i].fileID = desc.asset.pFile->resolveRelativeFileID(pFileIDField->GetValue()->AsUInt());
						boneLocations[i].pathID = pPathIDField->GetValue()->AsUInt64();
					}
					else
					{
						boneLocations[i].fileID = 0;
						boneLocations[i].pathID = 0;
					}
					boneLocations[i].hierarchyIndex = (unsigned int)-1;
				}
				BoneLocation rootLocation;
				rootLocation.fileID = desc.asset.pFile->resolveRelativeFileID(pRootBoneFileIDField->GetValue()->AsUInt());
				rootLocation.pathID = pRootBonePathIDField->GetValue()->AsUInt64();
				rootLocation.hierarchyIndex = (unsigned int)-1;
				RestoreBoneHierarchy(appContext, templateCache, bones, boneLocations, rootLocation);
				for (size_t i = 0; i < boneLocations.size(); i++)
				{
					if (boneLocations[i].hierarchyIndex == (unsigned int)-1 && boneLocations[i].fileID != 0 && boneLocations[i].pathID != 0)
					{
						std::vector<BoneLocation> tempLocations; tempLocations.push_back(boneLocations[i]);
						RestoreBoneHierarchy(appContext, templateCache, bones, tempLocations, boneLocations[i]);
					}
				}
				boneNames.resize(boneLocations.size());
				for (size_t i = 0; i < boneLocations.size(); i++)
				{
					if (boneLocations[i].hierarchyIndex < bones.size())
						boneNames[i] = bones[boneLocations[i].hierarchyIndex].name;
					else
						boneNames[i] = "";
				}
			}
			else if (progressManager)
				progressManager->get().logMessage(std::format(
					"WARNING: Unable to locate the bone list (SkinnedMeshRenderer File ID {}, Path ID {}).",
					desc.asset.fileID, desc.asset.pathID)
				);
	
			if (pMeshFileIDField->GetValue() && pMeshPathIDField->GetValue())
			{
				unsigned int targetFileID = desc.asset.pFile->resolveRelativeFileID(pMeshFileIDField->GetValue()->AsUInt());
				pathid_t targetPathID = pMeshPathIDField->GetValue()->AsUInt64();
				if (targetPathID == 0) //can happen
				{
					if (progressManager)
						progressManager->get().logMessage(std::format(
							"WARNING: Unable to locate the actual mesh (SkinnedMeshRenderer File ID {}, Path ID {}).",
							desc.asset.fileID, desc.asset.pathID)
						);
					return true;
				}
				actualMeshAsset = AssetIdentifier(targetFileID, targetPathID);
				if (!actualMeshAsset.resolve(appContext))
				{
					throw AssetUtilError(std::format(
						"Unable to find the referenced mesh asset (File ID {}, Path ID {}).",
						targetFileID, targetPathID)
					);
				}
				pTemplateBase = &templateCache.getTemplateField(appContext, actualMeshAsset);
			}
			else
			{
				throw AssetUtilError(std::format(
					"Unable to find the referenced mesh asset (SkinnedMeshRenderer File ID {}, Path ID {}).",
					desc.asset.fileID, desc.asset.pathID)
				);
			}
			pAssetReader = actualMeshAsset.makeReader();
			if (pAssetReader == nullptr)
				throw AssetUtilError("Unable to read the asset.");
			assetSize = 0;
			if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
				throw AssetUtilError("Unable to read the asset.");
			assetInstance = AssetTypeInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), actualMeshAsset.isBigEndian());
			pBaseField = assetInstance.GetBaseField();
			if (pBaseField == nullptr || pBaseField->IsDummy())
				throw AssetUtilError("Unable to deserialize the asset.");
		}

		bool isU5 = actualMeshAsset.pFile->getAssetsFileContext()->getAssetsFile()->header.format >= 0x0D;
		Mesh mesh(pBaseField, isU5, appContext, actualMeshAsset);
		const char *errorMessage = NULL;
		if (!mesh.wasAbleToRead)
			throw AssetUtilError("Unable to read the mesh asset! (unknown asset format)");
		else if (mesh.m_MeshCompression != 0)
			throw AssetUtilError("Compressed meshes are not supported! Use .obj export instead.");
		else if (mesh.m_VertexData.m_Streams.size() == 0)
			throw AssetUtilError("Invalid vertex data (no streams available)!");
		else if (mesh.m_VertexData.m_Channels.size() < 6)
			throw AssetUtilError("Invalid shader channels (less than 6 channels)!");
		else if (mesh.m_VertexData.m_Channels[0].dimension != 3)
			throw AssetUtilError("Invalid shader channels (vertex position doesn't have 3 floats)!");

		if (!combineToSingleFile) //Max. one mesh and skeleton per file.
		{
			aiScene curScene;
			if (!AddMeshToScene(curScene, mesh, boneNames, isU5))
				throw AssetUtilError("Unable to process the mesh.");
			AddBoneHierarchyToScene(curScene, bones, nullptr);

			std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(path.c_str(), true, true, RWOpenFlags_Immediately));
			if (pWriter == nullptr)
				throw AssetUtilError("Unable to create the output file.");

			if (!WriteScene(curScene, pWriter.get()))
				throw AssetUtilError("Unable to write the data.");
		}
		else
		{
			unsigned int exportIndex = this->assetCounter++;
			std::string meshIndexStr = std::format("skeleton{}_", exportIndex);
	
			for (size_t i = 0; i < boneNames.size(); i++)
			{
				boneNames[i].insert(0, meshIndexStr);
			}
			
			//For proper parallelism with combined meshes, 
			// working on per-thread sub scenes and reducing them in onCompletion
			// may be a viable approach.
			std::scoped_lock<std::mutex> combinedSceneLock(combinedSceneMutex);
			if (!AddMeshToScene(combinedScene, mesh, boneNames, isU5))
				throw AssetUtilError("Unable to process the mesh.");
			meshIndexStr.pop_back(); //Remove the "_" character.
			AddBoneHierarchyToScene(combinedScene, bones, meshIndexStr.c_str());
		}


		return true;
	}
};

class MeshOBJExportTask : public AssetExportTask
{
	AppContext& appContext;
	TypeTemplateCache templateCache;
public:
	MeshOBJExportTask(AppContext& appContext,
		std::vector<AssetUtilDesc> _assets, std::string _baseDir,
		bool stopOnError = false)

		: AssetExportTask(std::move(_assets), "Export Mesh", ".obj", std::move(_baseDir), stopOnError),
		appContext(appContext)
	{}

	bool exportAsset(AssetUtilDesc& desc, std::string path, std::optional<std::reference_wrapper<TaskProgressManager>> progressManager)
	{
		if (desc.asset.pFile == nullptr)
			throw AssetUtilError("Unable to find the target .assets file.");

		IAssetsReader_ptr pAssetReader = desc.asset.makeReader();
		if (pAssetReader == nullptr)
			throw AssetUtilError("Unable to read the asset.");
		QWORD assetSize = 0;
		if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
			throw AssetUtilError("Unable to read the asset.");

		AssetTypeTemplateField& templateBase = templateCache.getTemplateField(appContext, desc.asset);
		AssetTypeTemplateField* pTemplateBase = &templateBase;

		AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), desc.asset.isBigEndian());
		AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
		if (pBaseField == nullptr || pBaseField->IsDummy())
			throw AssetUtilError("Unable to deserialize the asset.");

		bool isU5 = desc.asset.pFile->getAssetsFileContext()->getAssetsFile()->header.format >= 0x0D;
		Mesh mesh(pBaseField, isU5, appContext, desc.asset);

		const char* errorMessage = NULL;
		if (!mesh.wasAbleToRead)
			throw AssetUtilError("Unable to read the mesh asset. (unknown or unsupported asset format)");
		else if (mesh.m_MeshCompression != 0 && !mesh.m_CompressedMesh.wasAbleToRead)
			throw AssetUtilError("The compressed mesh has an unknown format.");
		else if (mesh.m_VertexData.m_Streams.size() == 0)
			throw AssetUtilError("Invalid vertex data (no streams available).");
		else if (mesh.m_MeshCompression == 0)
		{
			if (mesh.m_VertexData.m_Channels.size() < 6)
				throw AssetUtilError("Invalid shader channels (less than 6 channels).");
			else if (mesh.m_VertexData.m_Channels[0].dimension != 3)
				throw AssetUtilError("Invalid shader channels (vertex position doesn't have 3 floats).");
			else if (!mesh.m_VertexData.IsFloatFormat(mesh.m_VertexData.m_Channels[0].format, isU5))
				throw AssetUtilError("Invalid shader channels (vertex position is not a float vector).");
		}

		std::unique_ptr<IAssetsWriter> pWriter(Create_AssetsWriterToFile(path.c_str(), true, true, RWOpenFlags_Immediately));
		if (pWriter == nullptr)
			throw AssetUtilError("Unable to create the output file.");

		auto _writerputs = [&pWriter](const std::string& msg) {pWriter->Write(msg.size(), msg.data()); };
		std::string formatTmp;
		unsigned int vertexCount = 0, indexCount = 0;
		for (size_t i = 0; i < mesh.m_SubMeshes.size(); i++)
		{
			SubMesh& subMesh = mesh.m_SubMeshes[i];
			vertexCount = subMesh.firstVertex + subMesh.vertexCount;
			indexCount += subMesh.indexCount;
		}
		//if (!vertexCount && mesh.m_MeshCompression != 0)
		//	vertexCount = mesh.m_CompressedMesh.m_Vertices.m_NumItems / 3;
		unsigned int bitsPerVertex = 0;
		if (!mesh.m_MeshCompression)
		{
			unsigned int bytesPerVertex = 0;
			for (size_t i = 0; i < mesh.m_VertexData.m_Streams.size(); i++)
				bytesPerVertex += mesh.m_VertexData.m_Streams[i].stride;
			unsigned int expectedVertexBytes = vertexCount * bytesPerVertex;
			if (expectedVertexBytes > (unsigned int)mesh.m_VertexData.dataByteCount)
			{
				throw AssetUtilError(std::format("Expected {} vertices ({} bytes) but got only {} bytes.", 
					vertexCount, expectedVertexBytes, mesh.m_VertexData.dataByteCount));

				vertexCount = mesh.m_VertexData.dataByteCount / expectedVertexBytes;
			}
			bitsPerVertex = bytesPerVertex * 8;
		}
		else// if (mesh.m_VertexData.m_VertexCount > 0)
		{
			unsigned int totalVertexCount = mesh.m_CompressedMesh.m_Vertices.m_NumItems / 3;
			if (totalVertexCount > 0)
			{
				if (mesh.m_CompressedMesh.m_Vertices.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_Vertices.m_BitSize *
					(mesh.m_CompressedMesh.m_Vertices.m_NumItems / totalVertexCount); //should be m_BitSize*3
				if (mesh.m_CompressedMesh.m_UV.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_UV.m_BitSize *
					(mesh.m_CompressedMesh.m_UV.m_NumItems / totalVertexCount); //should be m_BitSize*2,4,6 or 8 (uv1-4)
				//Bind poses don't belong to the vertex data; 4x4 float matrices;

				//Contains the normals data without sign bit (the bit vectors are unsigned);
				//The m_NormalSigns value has the sign bit of the third component, z.
				//The length of the normal vector |v| = sqrt(x²+y²+z²) = 1²; z is not stored, so z = sqrt(1²-x²-y²).
				if (mesh.m_CompressedMesh.m_Normals.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_Normals.m_BitSize *
					(mesh.m_CompressedMesh.m_Normals.m_NumItems / totalVertexCount); //shuold be m_BitSize*2
				//Tangents are stored like normals, so the description above applies to tangents, too.
				if (mesh.m_CompressedMesh.m_Tangents.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_Tangents.m_BitSize *
					(mesh.m_CompressedMesh.m_Tangents.m_NumItems / totalVertexCount); //should be m_BitSize * 2
				//Weights don't belong to the vertex data; Assumed single floats;
				if (mesh.m_CompressedMesh.m_NormalSigns.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_NormalSigns.m_BitSize *
					(mesh.m_CompressedMesh.m_NormalSigns.m_NumItems / totalVertexCount); //should be m_BitSize
				if (mesh.m_CompressedMesh.m_TangentSigns.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_TangentSigns.m_BitSize *
					(mesh.m_CompressedMesh.m_TangentSigns.m_NumItems / totalVertexCount); //should be m_BitSize
				if (mesh.m_CompressedMesh.m_FloatColors.wasAbleToRead && mesh.m_CompressedMesh.m_FloatColors.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_FloatColors.m_BitSize *
					(mesh.m_CompressedMesh.m_FloatColors.m_NumItems / totalVertexCount); //should be m_BitSize*3
				else if (mesh.m_CompressedMesh.m_Colors.wasAbleToRead && mesh.m_CompressedMesh.m_Colors.m_NumItems)
					bitsPerVertex += mesh.m_CompressedMesh.m_Colors.m_BitSize *
					(mesh.m_CompressedMesh.m_Colors.m_NumItems / totalVertexCount); //should be m_BitSize
			}
		}
		std::format_to(std::back_inserter(formatTmp), "# Mesh \"{}\" exported with UABE; {} vertices, {} indices, {} bits per vertex\n",
			mesh.m_Name, vertexCount, indexCount, bitsPerVertex);
		_writerputs(formatTmp);
		formatTmp.clear();
		std::format_to(std::back_inserter(formatTmp), "g {}\n", mesh.m_Name);
		_writerputs(formatTmp);
		formatTmp.clear();
		if (!mesh.m_MeshCompression)
		{
			ChannelInfo& posChannel = mesh.m_VertexData.m_Channels[0];
			ChannelInfo& normalChannel = mesh.m_VertexData.m_Channels[1];
			ChannelInfo& colorChannel = mesh.m_VertexData.m_Channels[2];
			ChannelInfo& uv1Channel = mesh.m_VertexData.m_Channels[3];
			ChannelInfo& uv2Channel = mesh.m_VertexData.m_Channels[4];
			//m_Channels[5] : UV3 for Unity >= 5.0, else tangents
			//m_Channels[6] : UV4
			//m_Channels[7] : Tangents
			ChannelInfo* pUVChannel = &uv1Channel;

			StreamInfo& posStream = mesh.m_VertexData.m_Streams[posChannel.stream];
			StreamInfo& normalStream = mesh.m_VertexData.m_Streams[normalChannel.stream];
			StreamInfo& colorStream = mesh.m_VertexData.m_Streams[colorChannel.stream];
			StreamInfo& uv1Stream = mesh.m_VertexData.m_Streams[uv1Channel.stream];
			StreamInfo& uv2Stream = mesh.m_VertexData.m_Streams[uv2Channel.stream];
			StreamInfo* pUVStream = &uv1Stream;

			bool hasNormals = (mesh.m_VertexData.m_Channels[1].dimension == 3 || mesh.m_VertexData.m_Channels[1].dimension == 4)
				&& mesh.m_VertexData.IsFloatFormat(mesh.m_VertexData.m_Channels[1].format, isU5);
			bool hasColors = mesh.m_VertexData.m_Channels[2].dimension > 0;
			bool hasUV1 = mesh.m_VertexData.m_Channels[3].dimension == 2 && mesh.m_VertexData.IsFloatFormat(mesh.m_VertexData.m_Channels[3].format, isU5);
			bool hasUV2 = mesh.m_VertexData.m_Channels[4].dimension == 2 && mesh.m_VertexData.IsFloatFormat(mesh.m_VertexData.m_Channels[4].format, isU5);
			if (!hasUV1)
			{
				pUVChannel = (hasUV2 ? &uv2Channel : NULL);
				pUVStream = (hasUV2 ? &uv2Stream : NULL);
			}
			uint8_t vertexPosElementSize = mesh.m_VertexData.ChannelElementSize(posChannel.format, isU5);
			uint8_t vertexNormalElementSize = mesh.m_VertexData.ChannelElementSize(normalChannel.format, isU5);
			uint8_t vertexUVElementSize = pUVChannel ? mesh.m_VertexData.ChannelElementSize(pUVChannel->format, isU5) : 0;

			std::string_view faceFormat;
			{
				faceFormat = " {}";
				if (hasUV1 || hasUV2)
				{
					if (hasNormals)
						faceFormat = " {}/{}/{}";
					else
						faceFormat = " {}/{}";
				}
				else if (hasNormals)
					faceFormat = " {}//{}";
			}
			for (size_t i = 0; i < mesh.m_SubMeshes.size(); i++)
			{
				std::format_to(std::back_inserter(formatTmp), "# SubMesh {}\n# Vertices\n", i);
				_writerputs(formatTmp);
				formatTmp.clear();
				/*#define MeshChannel_Pos 0
				#define MeshChannel_Normal 1
				#define MeshChannel_Color 2
				#define MeshChannel_UV1 3
				#define MeshChannel_UV2 4*/
				SubMesh& subMesh = mesh.m_SubMeshes[i];
				for (unsigned int j = subMesh.firstVertex; j < (subMesh.firstVertex + subMesh.vertexCount) && j < vertexCount; j++)
				{
					void *pCurPosVertex = &((uint8_t*)mesh.m_VertexData.m_DataSize)[posStream.offset + j * posStream.stride];
					void *pCurColorVertex = &((uint8_t*)mesh.m_VertexData.m_DataSize)[colorStream.offset + j * colorStream.stride];
					void *pCurUVVertex = NULL;
					if (pUVStream)
						pCurUVVertex = &((uint8_t*)mesh.m_VertexData.m_DataSize)[pUVStream->offset + j * pUVStream->stride];
					void *pCurNormalVertex = &((uint8_t*)mesh.m_VertexData.m_DataSize)[normalStream.offset + j * normalStream.stride];
					//PVOID pCurStreamVertex = &((BYTE*)mesh.m_VertexData.m_DataSize)[j * mesh.m_VertexData.m_Streams[pos];
					Vector3f vertexPos;
					if ((posStream.offset + j * posStream.stride + posChannel.offset + 3 * vertexPosElementSize) <= mesh.m_VertexData.dataByteCount)
						mesh.m_VertexData.ConvertChannelFloat(
							&((uint8_t*)pCurPosVertex)[posChannel.offset],
							posChannel.format, 3, (float*)&vertexPos,
							isU5
						);
					else
					{
						throw AssetUtilError("Invalid vertex data (out of bounds).");
					}
					_writerputs(std::format("v {} {} {}\n", 
						-vertexPos.x/* * subMesh.localAABB.m_Extent.x*//* + subMesh.localAABB.m_Center.x*/,
						vertexPos.y/* * subMesh.localAABB.m_Extent.y*//* + subMesh.localAABB.m_Center.y*/,
						vertexPos.z/* * subMesh.localAABB.m_Extent.z*//* + subMesh.localAABB.m_Center.z*/));
					if (hasColors)
					{
						uint32_t* colors = (uint32_t*)&((uint8_t*)pCurColorVertex)[colorChannel.offset];
						//TODO : add an additional .mtl file; usemtl, mtllib
					}
					if (pUVChannel)
					{
						Vector2f vertexUV1;
						if ((pUVStream->offset + j * pUVStream->stride + pUVChannel->offset + 2 * vertexUVElementSize) <= mesh.m_VertexData.dataByteCount)
							mesh.m_VertexData.ConvertChannelFloat(
								&((uint8_t*)pCurUVVertex)[pUVChannel->offset],
								pUVChannel->format, 2, (float*)&vertexUV1,
								isU5
							);
						else
						{
							throw AssetUtilError("Invalid vertex data (out of bounds).");
						}
						//Vector2f *vertexUV1 = (Vector2f*)&((BYTE*)pCurVertex)[mesh.m_VertexData.m_Channels[3].offset];
						_writerputs(std::format("vt {} {}\n", vertexUV1.x, vertexUV1.y));
					}
					if (hasNormals)
					{
						Vector3f vertexNormals;
						if ((normalStream.offset + j * normalStream.stride + normalChannel.offset + 3 * vertexNormalElementSize) <= mesh.m_VertexData.dataByteCount)
							mesh.m_VertexData.ConvertChannelFloat(
								&((uint8_t*)pCurNormalVertex)[normalChannel.offset],
								normalChannel.format, 3, (float*)&vertexNormals,
								isU5
							);
						else
						{
							throw AssetUtilError("Invalid vertex data (out of bounds).");
						}
						//Vector3f *vertexNormals = (Vector3f*)&((BYTE*)pCurVertex)[mesh.m_VertexData.m_Channels[1].offset];
						_writerputs(std::format("vn {} {} {}\n", -vertexNormals.x, vertexNormals.y, vertexNormals.z));
					}
				}
				std::format_to(std::back_inserter(formatTmp), "# Faces (%u to %u)\n",
					subMesh.firstVertex + 1, (subMesh.firstVertex + subMesh.vertexCount));
				_writerputs(formatTmp);
				formatTmp.clear();
				std::format_to(std::back_inserter(formatTmp), "g %s_%u\n", mesh.m_Name, i);
				_writerputs(formatTmp);
				formatTmp.clear();
				//sprintf_s(sprntTmp, "usemtl %s_%u\n", mesh.m_Name, i);
				//fputs(sprntTmp, pFile);
				//sprintf_s(sprntTmp, "usemap %s_%u\n", mesh.m_Name, i);
				//fputs(sprntTmp, pFile);
				unsigned int curIndexCount = 0;
				int triIndices[3];
				unsigned int definedVertices = (subMesh.firstVertex + subMesh.vertexCount);
				unsigned int firstIndex = subMesh.firstByte / ((mesh.m_IndexFormat == 1) ? 4 : 2);
				for (unsigned int i = firstIndex; i < (firstIndex + subMesh.indexCount) && i < mesh.m_IndexBuffer.size(); i++)
				{
					if (!curIndexCount)
						_writerputs("f");

					triIndices[curIndexCount] = (int)(mesh.m_IndexBuffer[i] + subMesh.baseVertex - (subMesh.firstVertex + subMesh.vertexCount));
					curIndexCount++;

					if (curIndexCount == 3)
					{
						if (subMesh.topology && (i & 1))
						{
							//always switch the winding
							for (int i = 0; i < 3; i++)
							{
								std::format_to(std::back_inserter(formatTmp), faceFormat, triIndices[i], triIndices[i], triIndices[i]);
								_writerputs(formatTmp);
								formatTmp.clear();
							}
						}
						else
						{
							for (int i = 2; i >= 0; i--)
							{
								std::format_to(std::back_inserter(formatTmp), faceFormat, triIndices[i], triIndices[i], triIndices[i]);
								_writerputs(formatTmp);
								formatTmp.clear();
							}
						}

						_writerputs("\n");
						curIndexCount = 0;
					}
					/*int relIndex = mesh.m_IndexBuffer[i] - (subMesh.firstVertex+subMesh.vertexCount);
					sprintf_s(sprntTmp, faceFormat, relIndex, relIndex, relIndex);
					//sprintf_s(sprntTmp, faceFormat, mesh.m_IndexBuffer[i]+1, mesh.m_IndexBuffer[i]+1, mesh.m_IndexBuffer[i]+1);
					//sprintf_s(sprntTmp, " %d", mesh.m_IndexBuffer[i]);
					fputs(sprntTmp, pFile);*/
				}
				_writerputs("\n");
			}
		}
		else
		{
			unsigned int vertexDim = 0;
			unsigned int vertexCount = 0;

			if (mesh.m_VertexData.m_VertexCount > 0)
			{
				vertexDim = (mesh.m_CompressedMesh.m_Vertices.m_NumItems / mesh.m_VertexData.m_VertexCount);
				vertexCount = mesh.m_VertexData.m_VertexCount;
			}
			else
			{
				//m_VertexData.m_VertexCount definitiviely tells us dimension and vertex count, but some mesh imports appear to omit it.
				//We'll make an educated guess at dimension based on whether or not the bounding box is flat along any axis, and calculate vertex count accordingly.
				if (mesh.m_LocalAABB.m_Center.x == mesh.m_LocalAABB.m_Extent.x ||
					mesh.m_LocalAABB.m_Center.y == mesh.m_LocalAABB.m_Extent.y ||
					mesh.m_LocalAABB.m_Center.z == mesh.m_LocalAABB.m_Extent.z)
				{
					vertexDim = 2;
				}
				else
				{
					vertexDim = 3;
				}

				vertexCount = mesh.m_CompressedMesh.m_Vertices.m_NumItems / vertexDim;
			}

			unsigned int uvCount = (mesh.m_CompressedMesh.m_UV.m_NumItems / vertexCount) / 2;
			bool hasNormals = mesh.m_CompressedMesh.m_Normals.m_NumItems != 0 &&
				(mesh.m_CompressedMesh.m_Normals.m_NumItems / vertexCount) == 2;
			bool hasNormalSigns = (mesh.m_CompressedMesh.m_NormalSigns.m_NumItems / mesh.m_CompressedMesh.m_Normals.m_NumItems) == 2;

			/*bool hasFloatColors = false;
			bool has32BitColors = false;
			hasFloatColors = mesh.m_CompressedMesh.m_FloatColors.m_NumItems != 0 &&
				(mesh.m_CompressedMesh.m_FloatColors.m_NumItems / vertexCount) == 4;
			has32BitColors = mesh.m_CompressedMesh.m_Colors.m_NumItems != 0 &&
				(mesh.m_CompressedMesh.m_Colors.m_NumItems / vertexCount) == 4;*/
			if (vertexDim < 2 || vertexDim > 3)
			{
				throw AssetUtilError(std::format("Expected vertex dimension 2 or 3 but got {}.", vertexDim));
			}
			std::string_view faceFormat;
			{
				faceFormat = " {}";
				if (uvCount > 0)
				{
					if (hasNormals)
						faceFormat = " {}/{}/{}";
					else
						faceFormat = " {}/{}";
				}
				else if (hasNormals)
					faceFormat = " {}//{}";
			}
			for (size_t i = 0; i < mesh.m_SubMeshes.size(); i++)
			{
				std::format_to(std::back_inserter(formatTmp), "# SubMesh {}\n# Vertices\n", i);
				_writerputs(formatTmp);
				formatTmp.clear();

				SubMesh& subMesh = mesh.m_SubMeshes[i];
				for (unsigned int j = subMesh.firstVertex; j < (subMesh.firstVertex + subMesh.vertexCount) && j < mesh.m_CompressedMesh.m_Vertices.m_NumItems; j++)
				{
					float position[3] = { 0,0,0 };
					for (unsigned int k = 0; k < vertexDim; k++)
					{
						unsigned int index = j * vertexDim + k;
						if (!mesh.m_CompressedMesh.m_Vertices.ReadValueFloat(index, position[k]))
						{
							throw AssetUtilError(std::format("Unable to read vertex position {} (m_Vertices item {}).", j, index));
						}
					}
					float uv[2] = { 0,0 };
					if (uvCount > 0)
					{
						for (unsigned int k = 0; k < 2; k++)
						{
							unsigned int index = j * (2 * uvCount) + k;
							if (!mesh.m_CompressedMesh.m_UV.ReadValueFloat(index, uv[k]))
							{
								throw AssetUtilError(std::format("Unable to read vertex uv {} (m_UV item {}).", j, index));
							}
						}
					}
					float normals[3] = { 0,0,0 };
					if (hasNormals)
					{
						for (unsigned int k = 0; k < 2; k++)
						{
							unsigned int index = j * 2 + k;
							if (!mesh.m_CompressedMesh.m_Normals.ReadValueFloat(index, normals[k]))
							{
								throw AssetUtilError(std::format("Unable to read vertex normal {} (m_Normals item {}).", j, index));
							}
						}
						normals[2] = sqrt(1 - normals[0] * normals[0] - normals[1] * normals[1]);
						if (hasNormalSigns)
						{
							unsigned int sign = 0;
							if (!mesh.m_CompressedMesh.m_NormalSigns.ReadValue(j, sign))
							{
								throw AssetUtilError(std::format("Unable to read vertex normal sign {}.", j));
							}
							if (!sign)
								normals[2] = -normals[2];
						}
					}

					if (vertexDim == 2)
						std::format_to(std::back_inserter(formatTmp), "v {} {}\n", -position[0], position[1]);
					else
						std::format_to(std::back_inserter(formatTmp), "v {} {} {}\n", -position[0], position[1], position[2]);
					_writerputs(formatTmp);
					formatTmp.clear();
					if (uvCount > 0)
					{
						std::format_to(std::back_inserter(formatTmp), "vt {} {}\n", uv[0], uv[1]);
						_writerputs(formatTmp);
						formatTmp.clear();
					}
					if (hasNormals)
					{
						std::format_to(std::back_inserter(formatTmp), "vn {} {} {}\n", -normals[0], normals[1], normals[2]);
						_writerputs(formatTmp);
						formatTmp.clear();
					}
				}
				std::format_to(std::back_inserter(formatTmp), "# Faces (%u to %u)\n",
					subMesh.firstVertex + 1, (subMesh.firstVertex + subMesh.vertexCount));
				_writerputs(formatTmp);
				formatTmp.clear();

				std::format_to(std::back_inserter(formatTmp), "g %s_%u\n", mesh.m_Name, i);
				_writerputs(formatTmp);
				formatTmp.clear();

				//sprintf_s(sprntTmp, "usemtl %s_%u\n", mesh.m_Name, i);
				//fputs(sprntTmp, pFile);
				//sprintf_s(sprntTmp, "usemap %s_%u\n", mesh.m_Name, i);
				//fputs(sprntTmp, pFile);

				int curIndexCount = 0;
				int triIndices[3];
				unsigned int definedVertices = (subMesh.firstVertex + subMesh.vertexCount);
				unsigned int firstIndex = subMesh.firstByte / 2;
				for (unsigned int j = firstIndex; j < (firstIndex + subMesh.indexCount)
					&& i < mesh.m_CompressedMesh.m_Triangles.m_NumItems; j++)
				{
					if (!curIndexCount)
						_writerputs("f");
					unsigned int curIndex = 0;
					if (!mesh.m_CompressedMesh.m_Triangles.ReadValue(j, curIndex))
					{
						throw AssetUtilError(std::format("Unable to read triangle index {}.", j));
					}

					triIndices[curIndexCount] = ((int)curIndex) - (subMesh.firstVertex + subMesh.vertexCount);
					curIndexCount++;

					if (curIndexCount == 3)
					{
						if (subMesh.topology && (j & 1))
						{
							//always switch the winding
							for (int k = 0; k < 3; k++)
							{
								std::format_to(std::back_inserter(formatTmp), faceFormat, triIndices[k], triIndices[k], triIndices[k]);
								_writerputs(formatTmp);
								formatTmp.clear();
							}
						}
						else
						{
							for (int k = 2; k >= 0; k--)
							{
								std::format_to(std::back_inserter(formatTmp), faceFormat, triIndices[k], triIndices[k], triIndices[k]);
								_writerputs(formatTmp);
								formatTmp.clear();
							}
						}

						_writerputs("\n");
						curIndexCount = 0;
					}
				}
				_writerputs("\n");
			}
		}


		return true;
	}
};

enum class MeshExportMode
{
	OBJ,
	DAESeparate,
	DAECombined
};
static bool SupportsElements(AppContext& appContext, std::vector<AssetUtilDesc>& elements, MeshExportMode mode)
{
	bool allowSkinnedMeshRenderer = (mode != MeshExportMode::OBJ);
	if (mode == MeshExportMode::DAECombined && elements.size() < 2)
		return false;
	std::unordered_map<AssetsFileContextInfo*, std::array<int32_t,2>> meshClassIDs;
	for (size_t i = 0; i < elements.size(); i++)
	{
		if (elements[i].asset.pFile == nullptr)
			return false;
		AssetsFileContextInfo* pFile = elements[i].asset.pFile.get();
		auto classIDsit = meshClassIDs.find(pFile);
		std::array<int32_t, 2> ids = { -1, -1 };
		if (classIDsit == meshClassIDs.end())
		{
			ids[0] = pFile->GetClassByName("Mesh");
			ids[1] = pFile->GetClassByName("SkinnedMeshRenderer");
			meshClassIDs[pFile] = ids;
		}
		else
			ids = classIDsit->second;
		int32_t classId = elements[i].asset.getClassID();
		if (classId == -1 || (classId != ids[0] && (!allowSkinnedMeshRenderer || classId != ids[1])))
			return false;
	}
	return true;
}
class MeshExportProvider : public IAssetOptionProviderGeneric
{
	MeshExportMode mode;
public:
	inline MeshExportProvider(MeshExportMode mode)
		: mode(mode)
	{}
	class Runner : public IOptionRunner
	{
		AppContext& appContext;
		std::vector<AssetUtilDesc> selection;
		MeshExportMode mode;
	public:
		Runner(AppContext& appContext, std::vector<AssetUtilDesc> _selection, MeshExportMode mode)
			: appContext(appContext), selection(std::move(_selection)), mode(mode)
		{}
		void operator()()
		{
			std::string exportLocation;
			switch (mode)
			{
			case MeshExportMode::OBJ:
				exportLocation = appContext.QueryAssetExportLocation(selection, ".obj", "*.obj|OBJ file:");
				break;
			case MeshExportMode::DAESeparate:
				exportLocation = appContext.QueryAssetExportLocation(selection, ".dae", "*.dae|Collada file:");
				break;
			case MeshExportMode::DAECombined:
				if (!selection.empty())
					exportLocation = appContext.QueryAssetExportLocation({ selection[0] }, ".dae", "*.dae|Collada file:");
				break;
			}
			if (!exportLocation.empty())
			{
				std::shared_ptr<ITask> pTask = nullptr;
				switch (mode)
				{
				case MeshExportMode::OBJ:
					pTask = std::make_shared<MeshOBJExportTask>(appContext, std::move(selection), std::move(exportLocation));
					break;
				case MeshExportMode::DAESeparate:
					pTask = std::make_shared<MeshDAEExportTask>(appContext, std::move(selection), std::move(exportLocation), false);
					break;
				case MeshExportMode::DAECombined:
					pTask = std::make_shared<MeshDAEExportTask>(appContext, std::move(selection), std::move(exportLocation), true);
					break;
				default:
					return;
				}
				appContext.taskManager.enqueue(pTask);
			}
		}
	};
	EAssetOptionType getType()
	{
		return EAssetOptionType::Export;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		class AppContext& appContext,
		std::vector<struct AssetUtilDesc> selection,
		std::string& optionName)
	{
		if (!SupportsElements(appContext, selection, mode))
			return nullptr;
		const char* modeDesc = "";
		switch (mode)
		{
		case MeshExportMode::OBJ:
			modeDesc = ".obj";
			break;
		case MeshExportMode::DAESeparate:
			modeDesc = ".dae";
			break;
		case MeshExportMode::DAECombined:
			modeDesc = ".dae (combined)";
			break;
		default:
			return nullptr;
		}
		optionName = std::string("Export mesh to ") + modeDesc;
		return std::make_unique<Runner>(appContext, std::move(selection), mode);
	}
};

class MeshPluginDesc : public IPluginDesc
{
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	MeshPluginDesc()
	{
		pProviders = {
			std::make_shared<MeshExportProvider>(MeshExportMode::OBJ),
			std::make_shared<MeshExportProvider>(MeshExportMode::DAESeparate),
			std::make_shared<MeshExportProvider>(MeshExportMode::DAECombined)
		};
	}
	std::string getName()
	{
		return "Mesh";
	}
	std::string getAuthor()
	{
		return "";
	}
	std::string getDescriptionText()
	{
		return "Export Mesh and SkinnedMeshRenderer assets.";
	}
	//The IPluginDesc object should keep a reference to the returned options, as the caller may keep only std::weak_ptrs.
	//Note: May be called early, e.g. before program UI initialization.
	std::vector<std::shared_ptr<IOptionProvider>> getPluginOptions(class AppContext& appContext)
	{
		return pProviders;
	}
};

IPluginDesc* GetUABEPluginDesc1(size_t sizeof_AppContext, size_t sizeof_BundleFileContextInfo)
{
	if (sizeof_AppContext != sizeof(AppContext) || sizeof_BundleFileContextInfo != sizeof(BundleFileContextInfo))
	{
		assert(false);
		return nullptr;
	}
	return new MeshPluginDesc();
}

