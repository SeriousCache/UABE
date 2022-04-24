#pragma once

#include "../UABE_Generic/AssetPluginUtil.h"
#include "../AssetsTools/AssetsReplacer.h"

#include <vector>
#include <cstdint>
struct HalfFloat
{
	unsigned short half;
	//http://stackoverflow.com/questions/6162651/half-precision-floating-point-in-java/6162687#6162687
	float toFloat();
};
struct Vector2f
{
	float x;
	float y;
};
struct Vector2hf
{
	HalfFloat x;
	HalfFloat y;
	void ToVector2f(Vector2f &out);
};

struct Vector3f
{
	bool Read(AssetTypeValueField *pField);
	float x;
	float y;
	float z;
};
struct Vector3hf
{
	HalfFloat x;
	HalfFloat y;
	HalfFloat z;
	void ToVector3f(Vector3f &out);
};
struct AABB
{
	bool Read(AssetTypeValueField *pField);
	Vector3f m_Center;
	Vector3f m_Extent;
};
struct MinMaxAABB
{
	bool Read(AssetTypeValueField *pField);
	Vector3f m_Min;
	Vector3f m_Max;
};

struct Quaternionf
{
	bool Read(AssetTypeValueField *pField);
	float x;
	float y;
	float z;
	float w;
};

struct Matrix4x4f
{
	bool Read(AssetTypeValueField *pField);
	float e[4][4]; //e[line][column]
};

struct SubMesh
{
	bool Read(AssetTypeValueField *pField);
	unsigned int firstByte; //index buffer offset
	unsigned int indexCount;
	int topology; //unknown
	unsigned int baseVertex; //since 2017.3; offset for indices, used especially for 16bit index buffers with >65535 vertices.
	unsigned int firstVertex; //vertex index
	unsigned int vertexCount;
	AABB localAABB;
};
struct ChannelInfo
{
	bool Read(AssetTypeValueField *pField);
	unsigned char stream; 
	unsigned char offset; //byte offset
	unsigned char format; //0 : float32, 1 : half, 2 : byte(?), 11 : int32
	unsigned char dimension; //amount of values; if 0 : channel doesn't exist
	unsigned char dimension_flags;
};
struct StreamInfo
{
	bool Read(AssetTypeValueField *pField);
	unsigned int channelMask;
	unsigned int offset;
	unsigned char stride;
	unsigned char dividerOp;
	unsigned short frequency;
};
class VertexData
{
public:
	bool wasAbleToRead;
	VertexData();
	inline VertexData(const VertexData &src) { (*this) = src; }
	inline VertexData(VertexData &&src) { (*this) = std::move(src); }
	VertexData &operator=(const VertexData &src);
	VertexData &operator=(VertexData &&src);
	//pMeshContextInfo: File that contains the streamInfo reference, used for path resolving.
	VertexData(AssetTypeValueField *pField,
		AppContext &appContext, class StreamingInfo &streamInfo, AssetIdentifier& meshAsset,
		std::vector<struct BoneInfluence> &boneWeights, bool unity5OrNewer);
	~VertexData();

	//Allows float32/float16/unorm8/snorm8/unorm16/snorm16
	static bool ConvertChannelFloat(const void *inData, uint8_t inFormat, uint8_t dimension, float *outData, bool unity5OrNewer);
	//Only allows int8/int16/int32.
	static bool ConvertChannelInt32(const void *inData, uint8_t inFormat, uint8_t dimension, int *outData, bool unity5OrNewer);
	//Interprets int8/int16/int32 as uint8/uint16/uint32.
	static bool ConvertChannelUInt32(const void *inData, uint8_t inFormat, uint8_t dimension, unsigned int *outData, bool unity5OrNewer);
	static inline uint8_t ChannelElementSize(uint8_t format, bool unity5OrNewer)
	{
		//Unity 2018.2
		static const uint8_t channelFormatSizeNew[16] = {
			4, 2, 1, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4, 0, 0, 0
		};
		/*
		0  : Float32; 1  : Float16; 2  : UNorm8; 3  : UNorm8; 4  : SNorm8; 5  : UNorm16; 6  : SNorm16;
		7  : UInt8;   8  : SInt8;   9  : UInt16; 10 : SInt16; 11 : UInt32; 12 : SInt32;
		*/

		//Unity 4 : kVertexChannelFormatSizes;
		static const uint8_t channelFormatSizeOld[4] = {
			4, 2, 4, 1
		};
		//0 : Float32; 1  : Float16; 2 : UInt32; 3 : SNorm8;

		if (format > (unity5OrNewer ? 12 : 3))
			return 0;
		return unity5OrNewer ? channelFormatSizeNew[format] : channelFormatSizeOld[format];
	}
	static inline bool IsFloatFormat(uint8_t format, bool unity5OrNewer)
	{
		return unity5OrNewer ? (format < 7) : (format != 2);
	}
	static inline bool IsIntFormat(uint8_t format, bool unity5OrNewer)
	{
		return unity5OrNewer ? (format >= 8 && format <= 12 && ((format - 7) & 1)) : false;
	}
	//Int or UInt
	static inline bool IsUIntFormat(uint8_t format, bool unity5OrNewer)
	{
		return unity5OrNewer ? (format >= 7 && format <= 12) : (format == 2);
	}

	int m_CurrentChannels;
	unsigned int m_VertexCount; //amount of vertices (= dataByteCount / (lastChannel.offset + 4*lastChannel.dimension))
	std::vector<ChannelInfo> m_Channels;
	std::vector<StreamInfo> m_Streams;
	int dataByteCount;
	void* m_DataSize; //actual data of the mesh
protected:
	bool _hasOwnData; //if m_DataSize should be freed in the destructor.
};
class PackedBitVector
{
public:
	bool wasAbleToRead;
	bool hasRangeAndStart;
	PackedBitVector();
	bool Read(AssetTypeValueField *pField);
	unsigned char MakeByteMask(unsigned int bitOffset);
	bool ReadValue(unsigned int index, unsigned int &out);
	bool ReadValueFloat(unsigned int index, float &out);
	unsigned int m_NumItems;
	float m_Range;
	float m_Start;
	std::vector<unsigned char> m_Data;
	unsigned char m_BitSize;
	unsigned char bitSize; //m_BitSize may be null (according to https://github.com/Perfare/UnityStudio/blob/master/Unity%20Studio/Unity%20Classes/Mesh.cs)
};
class CompressedMesh
{
public:
	bool wasAbleToRead;
	CompressedMesh();
	bool Read(AssetTypeValueField *pField);
	PackedBitVector m_Vertices;
	PackedBitVector m_UV;
	PackedBitVector m_BindPoses;
	PackedBitVector m_Normals;
	PackedBitVector m_Tangents;
	PackedBitVector m_Weights;
	PackedBitVector m_NormalSigns;
	PackedBitVector m_TangentSigns;
	PackedBitVector m_FloatColors;
	PackedBitVector m_BoneIndices;
	PackedBitVector m_Triangles;
	PackedBitVector m_Colors;
	unsigned int m_UVInfo;
};
struct BlendShapeVertex
{
public:
	bool Read(AssetTypeValueField *pField);
	Vector3f vertex;
	Vector3f normal;
	Vector3f tangent;
	unsigned int index;
};
struct MeshBlendShape
{
public:
	bool Read(AssetTypeValueField *pField);
	unsigned int firstVertex;
	unsigned int vertexCount;
	bool hasNormals;
	bool hasTangents;
};
struct MeshBlendShapeChannel
{
public:
	bool Read(AssetTypeValueField *pField);
	const char *name;
	unsigned int nameHash;
	int frameIndex;
	int frameCount;
};
class BlendShapeData
{
public:
	bool wasAbleToRead;
	BlendShapeData();
	bool Read(AssetTypeValueField *pField);

	std::vector<BlendShapeVertex> vertices;
	std::vector<MeshBlendShape> shapes;
	std::vector<MeshBlendShapeChannel> channels;
	std::vector<float> fullWeights;
};
struct BoneInfluence
{
	bool Read(AssetTypeValueField *pField);
	float weight[4];
	unsigned int boneIndex[4];
};
class StreamingInfo
{
public:
	bool wasAbleToRead;
	StreamingInfo();
	bool Read(AssetTypeValueField *pField);
	uint64_t offset;
	unsigned int size;
	std::string path;
};
class VariableBoneCountWeights
{
public:
	VariableBoneCountWeights();
	bool Read(AssetTypeValueField *pField);
	std::vector<unsigned int> m_Data;
};
class Mesh
{
public:
	bool wasAbleToRead;
	
	Mesh(AssetTypeValueField *pField, bool unity5OrNewer, AppContext &appContext, AssetIdentifier &asset);
	const char *m_Name;
	std::vector<SubMesh> m_SubMeshes;
	BlendShapeData m_Shapes;
	std::vector<Matrix4x4f> m_BindPose;
	std::vector<unsigned int> m_BoneNameHashes;
	unsigned int m_RootBoneNameHash;
	std::vector<MinMaxAABB> m_BonesAABB;
	VariableBoneCountWeights m_VariableBoneCountWeights;
	unsigned char m_MeshCompression;
	int m_IndexFormat; //0 : UInt16, 1 : UInt32 (=> UnityEngine.CoreModule.dll UnityEngine.Rendering.IndexFormat)
	std::vector<unsigned int> m_IndexBuffer;
	std::vector<BoneInfluence> m_Skin; //TODO: removed with Unity 2018.2, replaced by vertex data channels 12/13
	VertexData m_VertexData;
	CompressedMesh m_CompressedMesh;
	AABB m_LocalAABB;
	//int m_MeshUsageFlags;
	//std::vector<unsigned char> m_BakedConvexCollisionMesh;
	//std::vector<unsigned char> m_BakedTriangleCollisionMesh;
	//float m_MeshMetrics[2];
	StreamingInfo m_StreamData;
};