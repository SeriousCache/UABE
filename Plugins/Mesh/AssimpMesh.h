#pragma once
#include "Mesh.h"
#include <assimp/scene.h>
#include <assimp/matrix4x4.h>
#include <vector>

//Adds a mesh to an aiScene. boneNames (optional) overrides the bone names (default: name hashes).
//Bone names can be retrieved through a SkinnedMeshRenderer, which has a bone list of Transforms (=> name of GameObject)
bool AddMeshToScene(aiScene &scene, Mesh &mesh, std::vector<std::string> &boneNames, bool unity5OrNewer);

//Writes an aiScene.
bool WriteScene(aiScene &scene, IAssetsWriter *pWriter);

void ComposeMatrix(Quaternionf &rotation, Vector3f &position, Vector3f &scale, aiMatrix4x4 &out);