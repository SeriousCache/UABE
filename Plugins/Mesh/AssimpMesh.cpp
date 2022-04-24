#pragma once
#include "AssimpMesh.h"
#include <ColladaExporter.h> //assimp

bool AddMeshToScene(aiScene &scene, Mesh &mesh, std::vector<std::string> &boneNames, bool unity5OrNewer)
{
	if (!mesh.wasAbleToRead) return false;
	if (mesh.m_VertexData.m_Streams.size() == 0) return false;
	if (mesh.m_VertexData.m_Channels.size() < 6) return false;
	if (mesh.m_VertexData.m_Channels[0].dimension != 3) return false;

	//Unity 4.x sees a ColorRGBA as one DWORD, while Unity 5.x+ sees it as four UNORM8 (equivalent to raw color channel value).
	if (unity5OrNewer && mesh.m_VertexData.m_Channels[2].format == 2 && mesh.m_VertexData.m_Channels[2].dimension == 4)
	{
		mesh.m_VertexData.m_Channels[2].dimension = 1;
		mesh.m_VertexData.m_Channels[2].format = 11; //UINT32
	}
	
	if (scene.mRootNode == nullptr)
		scene.mRootNode = new aiNode("Root");

	void* pVertexDataEnd = &((uint8_t*)mesh.m_VertexData.m_DataSize)[mesh.m_VertexData.dataByteCount];

	aiMesh **pNewMeshList = new aiMesh*[scene.mNumMeshes + mesh.m_SubMeshes.size()]();
	memcpy(pNewMeshList, scene.mMeshes, scene.mNumMeshes * sizeof(aiMesh*));
	delete[] scene.mMeshes;
	scene.mMeshes = pNewMeshList;

	aiMaterial **pNewMatList = new aiMaterial*[scene.mNumMeshes + mesh.m_SubMeshes.size()]();
	memcpy(pNewMatList, scene.mMaterials, scene.mNumMaterials * sizeof(aiMaterial*));
	delete[] scene.mMaterials;
	scene.mMaterials = pNewMatList;

	bool blendShapeValid = mesh.m_Shapes.shapes.size() > 0
		&& mesh.m_Shapes.shapes.size() == mesh.m_Shapes.channels.size()
		&& mesh.m_Shapes.shapes.size() == mesh.m_Shapes.fullWeights.size()
		&& mesh.m_Shapes.vertices.size() == (mesh.m_Shapes.shapes.size() * mesh.m_Shapes.shapes[0].vertexCount);
	for (size_t i = 1; i < mesh.m_Shapes.shapes.size(); i++)
	{
		if (mesh.m_Shapes.shapes[i].vertexCount != mesh.m_Shapes.shapes[0].vertexCount)
		{
			blendShapeValid = false;
			break;
		}
	}

	for (size_t i = 0; i < mesh.m_SubMeshes.size(); i++)
	{
		//LOOP NOT DESIGNED TO continue/break!
		SubMesh &subMesh = mesh.m_SubMeshes[i];
		aiMesh *pAiMesh = new aiMesh();
		pAiMesh->mName = mesh.m_Name;
		if (mesh.m_SubMeshes.size() > 0)
		{
			char subTemp[16];
			sprintf_s(subTemp, "_sub%u", (unsigned int)i);
			pAiMesh->mName.Append(subTemp);
		}
		pAiMesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
		pAiMesh->mNumVertices = subMesh.vertexCount;
		for (int j = 0; j < mesh.m_VertexData.m_Channels.size() && j < 8; j++)
		{
			ChannelInfo &channel = mesh.m_VertexData.m_Channels[j];
			if (channel.dimension > 0 && channel.dimension <= 4 && channel.stream < mesh.m_VertexData.m_Streams.size())
			{
				StreamInfo &stream = mesh.m_VertexData.m_Streams[channel.stream];
				uint8_t channelElementSize = mesh.m_VertexData.ChannelElementSize(channel.format, unity5OrNewer);
				if ((channel.offset + channel.dimension * channelElementSize) <= stream.stride 
					&& ((QWORD)stream.offset + (QWORD)stream.stride * (QWORD)subMesh.vertexCount) <= mesh.m_VertexData.dataByteCount)
				{
					void *channelDataBuffer = nullptr;
					int channelType = j;
					size_t outElements = 0;
					switch (j)
					{
					case 0: //pos
						if (channel.dimension == 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mVertices = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mVertices;
							outElements = 3;
						}
						break;
					case 1: //normal
						if (channel.dimension == 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mNormals = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mNormals;
							outElements = 3;
						}
						break;
					case 2: //color
						if (channel.dimension == 1 && mesh.m_VertexData.IsUIntFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mColors[0] = new aiColor4D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mColors[0];
							outElements = 4;
						}
						break;
					case 3: //uv1
						if (channel.dimension <= 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mNumUVComponents[0] = channel.dimension;
							pAiMesh->mTextureCoords[0] = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mTextureCoords[0];
							outElements = 3;
						}
						break;
					case 4: //uv2
						if (channel.dimension <= 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mNumUVComponents[1] = channel.dimension;
							pAiMesh->mTextureCoords[1] = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mTextureCoords[1];
							outElements = 3;
						}
						break;
					case 5: //uv3 or tangent
						if (mesh.m_VertexData.m_Channels.size() > 6)
						{
							if (channel.dimension <= 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
							{
								pAiMesh->mNumUVComponents[2] = channel.dimension;
								pAiMesh->mTextureCoords[2] = new aiVector3D[subMesh.vertexCount];
								channelDataBuffer = pAiMesh->mTextureCoords[2];
								outElements = 3;
							}
						}
						else
						{
							channelType = 7; //tangent
							if (channel.dimension == 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
							{
								pAiMesh->mTangents = new aiVector3D[subMesh.vertexCount];
								channelDataBuffer = pAiMesh->mTangents;
								outElements = 3;
							}
						}
						break;
					case 6: //uv4
						if (channel.dimension <= 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mNumUVComponents[3] = channel.dimension;
							pAiMesh->mTextureCoords[3] = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mTextureCoords[3];
							outElements = 3;
						}
						break;
					case 7: //tangent
						if (channel.dimension == 3 && mesh.m_VertexData.IsFloatFormat(channel.format, unity5OrNewer))
						{
							pAiMesh->mTangents = new aiVector3D[subMesh.vertexCount];
							channelDataBuffer = pAiMesh->mTangents;
							outElements = 3;
						}
						break;
					}

					if (channelDataBuffer != NULL)
					{
						for (unsigned int k = subMesh.firstVertex; k < (subMesh.firstVertex+subMesh.vertexCount); k++)
						{
							uint8_t*pCurVertex = &((uint8_t*)mesh.m_VertexData.m_DataSize)[stream.offset + k * stream.stride + channel.offset];
							float inData[4] = {};
							if (channelType == 2) //color
							{
								uint8_t color[4] = {};
								mesh.m_VertexData.ConvertChannelUInt32(pCurVertex, channel.format, 1, (unsigned int*)&color[0], unity5OrNewer);
								for (unsigned int l = 0; l < 4; l++)
								{
									inData[l] = ((float)color[l]) / 255.0f;
								}
							}
							else
								mesh.m_VertexData.ConvertChannelFloat(pCurVertex, channel.format, channel.dimension, inData, unity5OrNewer);
							memcpy(&((uint8_t*)channelDataBuffer)[outElements * sizeof(float) * (k - subMesh.firstVertex)], inData, outElements * sizeof(float));
						}
					}
				}
			}
		}

		if (pAiMesh->mVertices != NULL)
		{
			for (unsigned int j = 0; j < subMesh.vertexCount; j++)
			{
				/*pAiMesh->mVertices[j].x *= subMesh.localAABB.m_Extent.x;
				pAiMesh->mVertices[j].y *= subMesh.localAABB.m_Extent.y;
				pAiMesh->mVertices[j].z *= subMesh.localAABB.m_Extent.z;
				pAiMesh->mVertices[j].x += subMesh.localAABB.m_Center.x;
				pAiMesh->mVertices[j].y += subMesh.localAABB.m_Center.y;
				pAiMesh->mVertices[j].z += subMesh.localAABB.m_Center.z;*/
				//TODO: Fix orientation
				pAiMesh->mVertices[j].x = -pAiMesh->mVertices[j].x;
			}
		}
		if (pAiMesh->mNormals != NULL)
		{
			for (unsigned int j = 0; j < subMesh.vertexCount; j++)
			{
				pAiMesh->mNormals[j].x = -pAiMesh->mNormals[j].x;
			}
		}
		if (pAiMesh->mTangents != NULL)
		{
			for (unsigned int j = 0; j < subMesh.vertexCount; j++)
			{
				pAiMesh->mTangents[j].x = -pAiMesh->mTangents[j].x;
			}
			pAiMesh->mBitangents = new aiVector3D[subMesh.vertexCount]();
			if (pAiMesh->mNormals != NULL)
			{
				for (unsigned int j = 0; j < subMesh.vertexCount; j++)
				{
					aiVector3D &tan = pAiMesh->mTangents[j];
					aiVector3D &norm = pAiMesh->mNormals[j];
					//Cross product of tangent and normal.
					//TODO: Check if this needs a normalization!
					pAiMesh->mBitangents[j] = aiVector3D(tan.y * norm.z - tan.z * norm.y, tan.z * norm.x - tan.x * norm.z, tan.x * norm.y - tan.y * norm.x);
				}
			}
		}

		pAiMesh->mNumFaces = subMesh.indexCount / 3;
		pAiMesh->mFaces = new aiFace[pAiMesh->mNumFaces]();
		size_t curFaceIndex = 0;

		unsigned int firstIndex = subMesh.firstByte / ((mesh.m_IndexFormat == 1) ? 4 : 2);
		for (unsigned int i = firstIndex; i < (firstIndex+subMesh.indexCount) && i < mesh.m_IndexBuffer.size(); i++)
		{
			if (curFaceIndex == pAiMesh->mNumFaces) break;
			aiFace &curFace = pAiMesh->mFaces[curFaceIndex];
			if (!curFace.mIndices)
				curFace.mIndices = new unsigned int[3];
			curFace.mIndices[curFace.mNumIndices++] = mesh.m_IndexBuffer[i] + subMesh.baseVertex - subMesh.firstVertex;

			if (curFace.mNumIndices == 3)
			{
				if (subMesh.topology && (i&1))
				{
					//always switch the winding
					//curFace.mIndices is {idx0, idx1, idx2}.
				}
				else
				{
					unsigned int newIndices[3] = {curFace.mIndices[2], curFace.mIndices[1], curFace.mIndices[0]};
					memcpy(curFace.mIndices, newIndices, 3 * sizeof(unsigned int));
				}
				curFaceIndex++;
			}
		}
		
		if (blendShapeValid && mesh.m_Shapes.vertices.size() >= ((size_t)subMesh.firstVertex + subMesh.vertexCount))
		{
			pAiMesh->mNumAnimMeshes = (unsigned int)mesh.m_Shapes.shapes.size();
			pAiMesh->mAnimMeshes = new aiAnimMesh*[pAiMesh->mNumAnimMeshes];
			for (unsigned int j = 0; j < pAiMesh->mNumAnimMeshes; j++)
			{
				aiAnimMesh *pAiAnimMesh = new aiAnimMesh();

				pAiAnimMesh->mNumVertices = subMesh.vertexCount;
				pAiAnimMesh->mVertices = new aiVector3D[subMesh.vertexCount];
				if (mesh.m_Shapes.shapes[j].hasNormals)
					pAiAnimMesh->mNormals = new aiVector3D[subMesh.vertexCount];
				if (mesh.m_Shapes.shapes[j].hasTangents)
				{
					pAiAnimMesh->mTangents = new aiVector3D[subMesh.vertexCount];
					pAiAnimMesh->mBitangents = new aiVector3D[subMesh.vertexCount];
				}
				unsigned int firstVertex = mesh.m_Shapes.shapes[j].firstVertex + subMesh.firstVertex;
				for (unsigned int k = 0; k < subMesh.vertexCount; k++)
				{
					BlendShapeVertex &curInVertex = mesh.m_Shapes.vertices[firstVertex + k];
					//TODO: Check if the SubMesh's localAABB has to be applied!
					pAiAnimMesh->mVertices[k] = aiVector3D(-curInVertex.vertex.x, curInVertex.vertex.y, curInVertex.vertex.z);
					if (mesh.m_Shapes.shapes[j].hasNormals)
						pAiAnimMesh->mNormals[k] = aiVector3D(-curInVertex.normal.x, curInVertex.normal.y, curInVertex.normal.z);
					if (mesh.m_Shapes.shapes[j].hasTangents)
					{
						pAiAnimMesh->mTangents[k] = aiVector3D(-curInVertex.tangent.x, curInVertex.tangent.y, curInVertex.tangent.z);
						aiVector3D &tan = pAiAnimMesh->mTangents[k];
						aiVector3D norm;
						if (mesh.m_Shapes.shapes[j].hasNormals)
							norm = pAiAnimMesh->mNormals[k];
						else if (pAiMesh->mNormals)
							norm = pAiMesh->mNormals[k];
						pAiAnimMesh->mBitangents[k] = aiVector3D(tan.y * norm.z - tan.z * norm.y, tan.z * norm.x - tan.x * norm.z, tan.x * norm.y - tan.y * norm.x);
					}
				}

				pAiAnimMesh->mWeight = mesh.m_Shapes.fullWeights[j] / 100.0f; //Seems to be a percentage for some reason

				char nameHashStr[24];
				sprintf_s(nameHashStr, "%u_", mesh.m_Shapes.channels[j].nameHash);
				pAiAnimMesh->mName = nameHashStr;
				pAiAnimMesh->mName.Append(mesh.m_Shapes.channels[j].name);

				pAiMesh->mAnimMeshes[j] = pAiAnimMesh;
			}
		}

		if (mesh.m_BindPose.size() > 0 
			//&& (mesh.m_BindPose.size() == mesh.m_BoneNameHashes.size())
			&& mesh.m_Skin.size() >= ((size_t)subMesh.firstVertex + subMesh.vertexCount))
		{
			std::vector<unsigned int> boneWeightCount = std::vector<unsigned int>(mesh.m_BindPose.size());
			bool hasAnyBones = false;
			for (size_t i = subMesh.firstVertex; i < (subMesh.firstVertex + subMesh.vertexCount); i++)
			{
				BoneInfluence &curVertexSkin = mesh.m_Skin[i];
				for (int j = 0; j < 4; j++)
				{
					if (curVertexSkin.weight[j] > 0.0f && curVertexSkin.boneIndex[j] < mesh.m_BindPose.size())
					{
						boneWeightCount[curVertexSkin.boneIndex[j]]++;
						hasAnyBones = true;
					}
				}
			}
			if (hasAnyBones)
			{
				unsigned int curBoneCount = 0;
				std::vector<unsigned int> boneIndexMap = std::vector<unsigned int>(mesh.m_BindPose.size(), (unsigned int)-1);
				for (size_t i = 0; i < mesh.m_BindPose.size(); i++)
					if (boneWeightCount[i])
						boneIndexMap[i] = curBoneCount++;

				pAiMesh->mNumBones = curBoneCount;
				pAiMesh->mBones = new aiBone*[curBoneCount];
				unsigned int curBoneIndex = 0;
				for (size_t i = 0; i < mesh.m_BindPose.size(); i++)
				{
					if (boneWeightCount[i])
					{
						aiBone *pAiBone = new aiBone();
						pAiBone->mWeights = new aiVertexWeight[boneWeightCount[i]]();
						Matrix4x4f &m = mesh.m_BindPose[i];
						//Let the transformation matrix be ((a1 a2 a3 a4) (b1 ...) ... (d1 d2 d3 d4)).
						//Since the vertex positions are flipped around the y-z-plane (x *= -1), 
						//we need to 1) unflip it
						//   (i.e. negate transform.a1, b1, c1, which will be multiplied with the negated x coordinate => transformed as if x was not flipped)
						//and 2) flip it again after the transformation
						//   (i.e. multiply the transformation matrix with ((-1 0 0 0) (0 1 0 0) (0 0 1 0) (0 0 0 1)), which causes the resulting x coordinate to be flipped).
						//Fused together : a2, a3, a4, b1, c1 have to be negated in order to account for the negated x vertex coordinate.
						pAiBone->mOffsetMatrix = aiMatrix4x4
							(m.e[0][0], -m.e[0][1], -m.e[0][2], -m.e[0][3],
							-m.e[1][0], m.e[1][1], m.e[1][2], m.e[1][3],
							-m.e[2][0], m.e[2][1], m.e[2][2], m.e[2][3],
							m.e[3][0], m.e[3][1], m.e[3][2], m.e[3][3]);
						if (boneNames.size() > i)
							pAiBone->mName = boneNames[i];
						else
						{
							char boneNameTemp[16];
							if (mesh.m_BoneNameHashes.size() > i)
								sprintf_s(boneNameTemp, "%u", mesh.m_BoneNameHashes[i]);
							else
								sprintf_s(boneNameTemp, "i%u", (unsigned int)i);
							pAiBone->mName = boneNameTemp;
						}
						pAiMesh->mBones[boneIndexMap[i]] = pAiBone;
					}
				}
				for (size_t i = subMesh.firstVertex; i < ((size_t)subMesh.firstVertex + subMesh.vertexCount); i++)
				{
					BoneInfluence &curVertexSkin = mesh.m_Skin[i];
					for (int j = 0; j < 4; j++)
					{
						unsigned int targetIndex;
						if (curVertexSkin.weight[j] > 0 && curVertexSkin.boneIndex[j] < mesh.m_BindPose.size()
							&& (targetIndex = boneIndexMap[curVertexSkin.boneIndex[j]]) != (unsigned int)-1)
						{
							aiBone *pAiTargetBone = pAiMesh->mBones[targetIndex];
							aiVertexWeight &vertexWeight = pAiTargetBone->mWeights[pAiTargetBone->mNumWeights++];
							vertexWeight.mVertexId = (unsigned int)(i - subMesh.firstVertex);
							vertexWeight.mWeight = curVertexSkin.weight[j];
						}
					}
				}
			}
		}

		aiMaterial *pAiMat = new aiMaterial();
		pAiMesh->mMaterialIndex = scene.mNumMaterials;
		scene.mMaterials[scene.mNumMaterials++] = pAiMat;

		scene.mMeshes[scene.mNumMeshes++] = pAiMesh;
	}

	aiNode *pMeshNode = new aiNode(mesh.m_Name);
	/*aiMatrix4x4 translation; aiMatrix4x4 scaling;
	aiMatrix4x4::Translation(aiVector3D(mesh.m_LocalAABB.m_Center.x, mesh.m_LocalAABB.m_Center.y, mesh.m_LocalAABB.m_Center.z), translation);
	aiMatrix4x4::Scaling(aiVector3D(mesh.m_LocalAABB.m_Extent.x, mesh.m_LocalAABB.m_Extent.y, mesh.m_LocalAABB.m_Extent.z), scaling);
	pMeshNode->mTransformation = translation * scaling;*/

	pMeshNode->mNumMeshes = (unsigned int)mesh.m_SubMeshes.size();
	pMeshNode->mMeshes = new unsigned int[pMeshNode->mNumMeshes];
	for (unsigned int i = 0; i < pMeshNode->mNumMeshes; i++)
		pMeshNode->mMeshes[i] = scene.mNumMeshes - pMeshNode->mNumMeshes + i;

	scene.mRootNode->addChildren(1, &pMeshNode);

	return true;
}

bool WriteScene(aiScene &scene, IAssetsWriter *pWriter)
{
	Assimp::ColladaExporter exporter(&scene, NULL, std::string(), std::string());
	char *outputBuffer = new char[4096]();
	
	exporter.mOutput.seekp(0, std::ios::end);
	size_t fileSize = exporter.mOutput.tellp();
	exporter.mOutput.seekg(0, std::ios::beg);
	while (fileSize >= 4096)
	{
		exporter.mOutput.read(outputBuffer, 4096);
		if (pWriter->Write(4096, outputBuffer) != 4096)
		{
			delete[] outputBuffer;
			return false;
		}
		fileSize -= 4096;
	}
	bool ret = true;
	if (fileSize > 0)
	{
		exporter.mOutput.read(outputBuffer, fileSize);
		ret = pWriter->Write(fileSize, outputBuffer) == fileSize;
	}
	delete[] outputBuffer;
	return ret;
}


void ComposeMatrix(Quaternionf &rotation, Vector3f &position, Vector3f &scale, aiMatrix4x4 &out)
{
	out = aiMatrix4x4(aiVector3D(scale.x, scale.y, scale.z), aiQuaternion(rotation.w, rotation.x, rotation.y, rotation.z), aiVector3D(position.x, position.y, position.z));
}