#include "Mesh.h"
#undef max
#undef min
#include "../inc/half.hpp"

float HalfFloat::toFloat()
{
	return half_float::detail::half2float(half);
}
void Vector2hf::ToVector2f(Vector2f &out)
{
	out.x = x.toFloat();
	out.y = y.toFloat();
}
void Vector3hf::ToVector3f(Vector3f &out)
{
	out.x = x.toFloat();
	out.y = y.toFloat();
	out.z = z.toFloat();
}
bool Vector3f::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pXField = (*pField)["x"];
	AssetTypeValueField *pYField = (*pField)["y"];
	AssetTypeValueField *pZField = (*pField)["z"];
	if (!pXField->IsDummy() && !pYField->IsDummy() && !pZField->IsDummy())
	{
		x = pXField->GetValue()->AsFloat();
		y = pYField->GetValue()->AsFloat();
		z = pZField->GetValue()->AsFloat();
		return true;
	}
	return false;
}
bool AABB::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pCenterField = (*pField)["m_Center"];
	AssetTypeValueField *pExtentField = (*pField)["m_Extent"];
	if (!pCenterField->IsDummy() && !pExtentField->IsDummy())
	{
		return m_Center.Read(pCenterField) && m_Extent.Read(pExtentField);
	}
	return false;
}
bool MinMaxAABB::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pMinField = (*pField)["m_Min"];
	AssetTypeValueField *pMaxField = (*pField)["m_Max"];
	if (!pMinField->IsDummy() && !pMaxField->IsDummy())
	{
		return m_Min.Read(pMinField) && m_Max.Read(pMaxField);
	}
	return false;
}
bool Quaternionf::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pXField = (*pField)["x"];
	AssetTypeValueField *pYField = (*pField)["y"];
	AssetTypeValueField *pZField = (*pField)["z"];
	AssetTypeValueField *pWField = (*pField)["w"];
	if (!pXField->IsDummy() && !pYField->IsDummy() && !pZField->IsDummy() && !pWField->IsDummy())
	{
		x = pXField->GetValue()->AsFloat();
		y = pYField->GetValue()->AsFloat();
		z = pZField->GetValue()->AsFloat();
		w = pWField->GetValue()->AsFloat();
		return true;
	}
	return false;
}
bool Matrix4x4f::Read(AssetTypeValueField *pField)
{
	if (pField->GetChildrenCount() == 4*4)
	{
		for (unsigned int i = 0; i < 16; i++)
		{
			AssetTypeValueField *pNumField = pField->Get(i);
			AssetTypeValue *pValue = pNumField->GetValue();
			if (pValue == NULL || pValue->GetType() != ValueType_Float)
				return false;
			e[i >> 2][i & 3]  = pValue->AsFloat();
		}
		return true;
	}
	return false;
}
bool SubMesh::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pFirstByteField = (*pField)["firstByte"];
	AssetTypeValueField *pIndexCountField = (*pField)["indexCount"];
	AssetTypeValueField *pTopologyField = (*pField)["topology"];
	AssetTypeValueField *pBaseVertexField = (*pField)["baseVertex"];
	AssetTypeValueField *pFirstVertexField = (*pField)["firstVertex"];
	AssetTypeValueField *pVertexCountField = (*pField)["vertexCount"];
	AssetTypeValueField *pAABBField = (*pField)["localAABB"];
	if (!pFirstByteField->IsDummy() && !pIndexCountField->IsDummy() && !pTopologyField->IsDummy() && !pFirstVertexField->IsDummy()
			&& !pVertexCountField->IsDummy() && !pAABBField->IsDummy())
	{
		firstByte = pFirstByteField->GetValue()->AsUInt();
		indexCount = pIndexCountField->GetValue()->AsUInt();
		topology = pTopologyField->GetValue()->AsInt();
		if (!pBaseVertexField->IsDummy())
			baseVertex = pBaseVertexField->GetValue()->AsUInt();
		else
			baseVertex = 0; 
		firstVertex = pFirstVertexField->GetValue()->AsUInt();
		vertexCount = pVertexCountField->GetValue()->AsUInt();
		return localAABB.Read(pAABBField);
	}
	return false;
}
bool ChannelInfo::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pStreamField = (*pField)["stream"];
	AssetTypeValueField *pOffsetField = (*pField)["offset"];
	AssetTypeValueField *pFormatField = (*pField)["format"];
	AssetTypeValueField *pDimensionField = (*pField)["dimension"];
	if (!pStreamField->IsDummy() && !pOffsetField->IsDummy() && !pFormatField->IsDummy() && !pDimensionField->IsDummy())
	{
		stream = (unsigned char)pStreamField->GetValue()->AsUInt();
		offset = (unsigned char)pOffsetField->GetValue()->AsUInt();
		format = (unsigned char)pFormatField->GetValue()->AsUInt();
		dimension = (unsigned char)pDimensionField->GetValue()->AsUInt();
		dimension_flags = (dimension & 0xF0) >> 4;
		dimension = dimension & 0x0F;
		return true;
	}
	return false;
}
bool StreamInfo::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pChannelMaskField = (*pField)["channelMask"];
	AssetTypeValueField *pOffsetField = (*pField)["offset"];
	AssetTypeValueField *pStrideField = (*pField)["stride"];
	AssetTypeValueField *pDividerOpField = (*pField)["dividerOp"];
	AssetTypeValueField *pFrequencyField = (*pField)["frequency"];
	if (!pChannelMaskField->IsDummy() && !pOffsetField->IsDummy() && !pStrideField->IsDummy()
		&& !pDividerOpField->IsDummy() && !pFrequencyField->IsDummy())
	{
		channelMask = pChannelMaskField->GetValue()->AsUInt();
		offset = pOffsetField->GetValue()->AsUInt();
		stride = (unsigned char)pStrideField->GetValue()->AsUInt();
		dividerOp = (unsigned char)pDividerOpField->GetValue()->AsUInt();
		frequency = (unsigned short)pFrequencyField->GetValue()->AsUInt();
		return true;
	}
	return false;
}
VertexData::VertexData()
{
	wasAbleToRead = false;
	_hasOwnData = false;
	m_CurrentChannels = -1;
	m_VertexCount = 0;
	m_Channels = std::vector<ChannelInfo>();
	m_Streams = std::vector<StreamInfo>();
	dataByteCount = 0;
	m_DataSize = NULL;
}
VertexData::~VertexData()
{
	if (_hasOwnData && m_DataSize != nullptr)
	{
		delete[] ((uint8_t*)m_DataSize);
		dataByteCount = 0;
		m_DataSize = nullptr;
		_hasOwnData = false;
	}
}
VertexData& VertexData::operator=(const VertexData& src)
{
	wasAbleToRead = src.wasAbleToRead;
	m_CurrentChannels = src.m_CurrentChannels;
	m_VertexCount = src.m_VertexCount;
	m_Channels.assign(src.m_Channels.begin(), src.m_Channels.end());
	m_Streams.assign(src.m_Streams.begin(), src.m_Streams.end());
	dataByteCount = src.dataByteCount;
	m_DataSize = src.m_DataSize;
	if (src._hasOwnData && m_DataSize != nullptr)
	{
		m_DataSize = new uint8_t[dataByteCount];
		memcpy(m_DataSize, src.m_DataSize, dataByteCount);
		_hasOwnData = true;
	}
	else
		_hasOwnData = false;
	return *this;
}
VertexData& VertexData::operator=(VertexData&& src)
{
	wasAbleToRead = src.wasAbleToRead;
	m_CurrentChannels = src.m_CurrentChannels;
	m_VertexCount = src.m_VertexCount;
	m_Channels = std::move(src.m_Channels);
	m_Streams = std::move(src.m_Streams);
	dataByteCount = src.dataByteCount;
	m_DataSize = src.m_DataSize;
	_hasOwnData = src._hasOwnData;
	src._hasOwnData = false;
	src.m_DataSize = nullptr;
	return *this;
}
VertexData::VertexData(AssetTypeValueField* pField,
	AppContext& appContext, class StreamingInfo& streamInfo, AssetIdentifier &meshAsset,
	std::vector<struct BoneInfluence>& boneWeights, bool unity5OrNewer)
{
	const uint8_t formatCount = unity5OrNewer ? 12 : 4;
	m_Channels = std::vector<ChannelInfo>();
	wasAbleToRead = false;
	_hasOwnData = false;
	AssetTypeValueField *pCurrentChannelsField = (*pField)["m_CurrentChannels"];
	AssetTypeValueField *pVertexCountField = (*pField)["m_VertexCount"];
	AssetTypeValueField *pChannelsField = (*(*pField)["m_Channels"])["Array"];
	AssetTypeValueField *pDataField = (*pField)["m_DataSize"];
	if (!pVertexCountField->IsDummy() && !pChannelsField->IsDummy() && !pDataField->IsDummy())
	{
		wasAbleToRead = true;
		dataByteCount = pDataField->GetValue()->AsByteArray()->size;
		m_DataSize = pDataField->GetValue()->AsByteArray()->data;
		if (!pCurrentChannelsField->IsDummy())
			m_CurrentChannels = pCurrentChannelsField->GetValue()->AsInt();
		else
			m_CurrentChannels = -1;
		m_VertexCount = pVertexCountField->GetValue()->AsUInt();
		int channelCount = pChannelsField->GetValue()->AsArray()->size;
		for (int i = 0; i < channelCount; i++)
		{
			ChannelInfo channel;
			if (!channel.Read((*pChannelsField)[i]))
				wasAbleToRead = false;
			else
				m_Channels.push_back(channel);
		}
		AssetTypeValueField *pStreamsField = (*(*pField)["m_Streams"])["Array"];
		if (!pStreamsField->IsDummy() && pStreamsField->GetValue())
		{
			uint32_t streamCount = pStreamsField->GetValue()->AsArray()->size;
			for (uint32_t i = 0; i < streamCount; i++)
			{
				StreamInfo curStream;
				if (!curStream.Read(pStreamsField->Get(i)))
					wasAbleToRead = false;
				else
					m_Streams.push_back(curStream);
			}
			{
				uint32_t maxStream = streamCount - 1;
				for (size_t i = 0; i < m_Channels.size(); i++)
					if (m_Channels[i].stream > maxStream)
						maxStream = m_Channels[i].stream;
				StreamInfo tmpStream = {};
				for (uint32_t i = streamCount; i <= maxStream; i++)
					m_Streams.push_back(tmpStream);
			}
		}
		else
		{
			uint32_t maxStream = 0;
			for (size_t i = 0; i < m_Channels.size(); i++)
				if (m_Channels[i].stream > maxStream)
					maxStream = m_Channels[i].stream;
			StreamInfo tmpStream = {};
			for (uint32_t i = 0; i <= maxStream; i++)
				m_Streams.push_back(tmpStream);
			for (size_t i = 0; i < m_Channels.size(); i++)
			{
				ChannelInfo &channel = m_Channels[i];
				m_Streams[channel.stream].channelMask |= (1 << (int)(uint8_t)i);
				unsigned int curSize = channel.offset + channel.dimension * ChannelElementSize(channel.format, unity5OrNewer);
				if (curSize > m_Streams[channel.stream].stride)
					m_Streams[channel.stream].stride = curSize;

				if (channel.stream >= m_Streams.size() && m_VertexCount > 0)
					wasAbleToRead = false;
				if (channel.format >= formatCount)
					wasAbleToRead = false;
			}
			unsigned int curOffset = 0;
			for (size_t i = 0; i < m_Streams.size(); i++)
			{
				//no idea if there is any alignment
				//m_Streams[i].stride = (m_Streams[i].stride + 3) & (~3)
				m_Streams[i].offset = curOffset;
				//m_Streams[i].offset = curOffset;
				curOffset += m_Streams[i].stride * m_VertexCount;
			}
			if (curOffset > dataByteCount && curOffset > streamInfo.size)
				wasAbleToRead = false;
			if (m_Streams.size() == 2) //sometimes there are additional 8 bytes between both streams
				m_Streams[1].offset = (dataByteCount ? dataByteCount : streamInfo.size) - (m_Streams[1].stride * m_VertexCount);
		}

		if (wasAbleToRead && (streamInfo.size > 0))
		{
			std::shared_ptr<ResourcesFileContextInfo> pResourcesFile = nullptr;
			std::shared_ptr<IAssetsReader> pStreamReader = nullptr;
			try {
				pResourcesFile = FindResourcesFile(appContext, streamInfo.path, meshAsset, {});
				pStreamReader = pResourcesFile->getResource(pResourcesFile,
					streamInfo.offset,
					streamInfo.size);
				if (pStreamReader == nullptr)
					throw AssetUtilError("Unable to locate the texture resource.");
			}
			catch (AssetUtilError e)
			{
				//TODO: Proper error reporting
				// Have to make all allocations RAII to prevent leaks with thrown exceptions.
				wasAbleToRead = false;
			}
			if (pStreamReader != nullptr)
			{
				m_DataSize = new uint32_t[streamInfo.size];
				dataByteCount = pStreamReader->Read(streamInfo.size, m_DataSize);

				if (dataByteCount != streamInfo.size)
				{
					wasAbleToRead = false;
					delete[] ((uint32_t*)m_DataSize);
					m_DataSize = nullptr;
					dataByteCount = 0;
				}
				_hasOwnData = true;
			}
			else
				wasAbleToRead = false;
		}

		if (wasAbleToRead && m_Channels.size() >= 14
			&& m_Channels[12].dimension == 4 && IsFloatFormat(m_Channels[12].format, unity5OrNewer)
			&& m_Channels[13].dimension == 4 && IsUIntFormat(m_Channels[13].format, unity5OrNewer))
		{
			ChannelInfo &weightChannel = m_Channels[12];
			ChannelInfo &indexChannel = m_Channels[13];
			StreamInfo &weightStream = m_Streams[weightChannel.stream];
			StreamInfo &indexStream = m_Streams[indexChannel.stream];
			uint32_t weightVertexSize = 4 * ChannelElementSize(weightChannel.format, unity5OrNewer);
			uint32_t indexVertexSize = 4 * ChannelElementSize(indexChannel.format, unity5OrNewer);
			boneWeights.resize(m_VertexCount);
			for (unsigned int i = 0; i < m_VertexCount; i++)
			{
				void* pCurWeightVertex = &((uint8_t*)m_DataSize)[weightStream.offset + i * weightStream.stride + weightChannel.offset];
				void* pCurIndexVertex = &((uint8_t*)m_DataSize)[indexStream.offset + i * indexStream.stride + indexChannel.offset];
				if ((weightStream.offset + i * weightStream.stride + weightChannel.offset + weightVertexSize) <= dataByteCount
					&& (indexStream.offset + i * indexStream.stride + indexChannel.offset + indexVertexSize) <= dataByteCount)
				{
					ConvertChannelFloat(pCurWeightVertex, weightChannel.format, 4, boneWeights[i].weight, unity5OrNewer);
					ConvertChannelUInt32(pCurIndexVertex, indexChannel.format, 4, boneWeights[i].boneIndex, unity5OrNewer);
				}
				else
				{
					boneWeights.clear();
					wasAbleToRead = false;
					break;
				}
			}
		}
	}
}

bool VertexData::ConvertChannelFloat(const void *inData, uint8_t inFormat, uint8_t dimension, float *outData, bool unity5OrNewer)
{
	if (!unity5OrNewer)
	{
		if (inFormat == 2) //uint32
			return false;
		if (inFormat == 3) //snorm8
			inFormat = 4;
	}
	switch (inFormat)
	{
	case 0: //float32
		{
			const float *fInData = (const float*)inData;
			memcpy(outData, fInData, 4*dimension);
		}
		return true;
	case 1: //float16
		{
			const HalfFloat *hfInData = (const HalfFloat*)inData;
			for (uint8_t i = 0; i < dimension; i++)
			{
				HalfFloat curHF = {hfInData[i].half};
				outData[i] = curHF.toFloat();
			}
		}
		return true;
	case 2: //unorm8
	case 3: //unorm8
		{
			const unsigned __int8 *bInData = (const unsigned __int8*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (((float)bInData[i]) / 255.0);
		}
		return true;
	case 4: //snorm8
		{
			const __int8 *bInData = (const __int8*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (bInData[i] == -128) ? -1.0f : (((float)bInData[i]) / 127.0);
		}
		return true;
	case 5: //unorm16
		{
			const unsigned __int16 *bInData = (const unsigned __int16*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (((float)bInData[i]) / 65535.0);
		}
		return true;
	case 6: //snorm16
		{
			const __int16 *bInData = (const __int16*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (bInData[i] == -32768) ? -1.0f : (((float)bInData[i]) / 32767.0);
		}
		return true;
	default:
		return false;
	}
}
bool VertexData::ConvertChannelInt32(const void *inData, uint8_t inFormat, uint8_t dimension, int *outData, bool unity5OrNewer)
{
	if (!unity5OrNewer)
	{
		return false;
	}
	switch (inFormat)
	{
	case 8: //int8
		{
			const __int8 *bInData = (const __int8*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (int)bInData[i];
		}
		return true;
	case 10: //int16
		{
			const __int16 *sInData = (const __int16*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (int)sInData[i];
		}
		return true;
	case 12: //int32
		{
			const __int32 *iInData = (const __int32*)inData;
			memcpy(outData, iInData, 4 * dimension);
		}
		return true;
	default:
		return false;
	}
}
bool VertexData::ConvertChannelUInt32(const void *inData, uint8_t inFormat, uint8_t dimension, unsigned int *outData, bool unity5OrNewer)
{
	if (!unity5OrNewer)
	{
		if (inFormat == 2)
			inFormat = 11;
		else
			return false;
	}
	switch (inFormat)
	{
	case 7: //uint8
	case 8: //int8
		{
			const unsigned __int8 *bInData = (const unsigned __int8*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (unsigned int)bInData[i];
		}
		return true;
	case 9: //uint16
	case 10: //int16
		{
			const unsigned __int16 *sInData = (const unsigned __int16*)inData;
			for (uint8_t i = 0; i < dimension; i++)
				outData[i] = (unsigned int)sInData[i];
		}
		return true;
	case 11: //uint32
	case 12: //int32
		{
			const unsigned __int32 *iInData = (const unsigned __int32*)inData;
			memcpy(outData, iInData, 4 * dimension);
		}
		return true;
	default:
		return false;
	}
}

PackedBitVector::PackedBitVector()
{
	wasAbleToRead = false;
	hasRangeAndStart = false;
}
bool PackedBitVector::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pNumItems = (*pField)["m_NumItems"];
	AssetTypeValueField *pRange = (*pField)["m_Range"];
	AssetTypeValueField *pStart = (*pField)["m_Start"];
	AssetTypeValueField *pData = ((*pField)["m_Data"])->Get("Array");
	AssetTypeValueField *pBitSize = (*pField)["m_BitSize"];
	if ((!pNumItems->IsDummy() && pNumItems->GetValue() && pNumItems->GetValue()->GetType() == ValueType_UInt32) && 
		(!pData->IsDummy() && pData->GetValue() && pData->GetValue()->GetType() == ValueType_Array &&
			pData->GetTemplateField()->children.size() == 2 && pData->GetTemplateField()->children[1].valueType == ValueType_UInt8) &&
		(!pBitSize->IsDummy() && pBitSize->GetValue() && pBitSize->GetValue()->GetType() == ValueType_UInt8))
	{
		wasAbleToRead = true;
		m_NumItems = pNumItems->GetValue()->AsUInt();
		m_Data.clear();
		m_Data.reserve(pData->GetChildrenCount());
		for (unsigned int i = 0; i < pData->GetChildrenCount(); i++)
		{
			AssetTypeValueField *pCur = pData->Get(i);
			if (!pCur->GetValue())
			{
				wasAbleToRead = false;
				break;
			}
			m_Data.push_back((unsigned char)pCur->GetValue()->AsUInt());
		}
		m_BitSize = (unsigned char)pBitSize->GetValue()->AsUInt();
		if (!m_BitSize && m_NumItems)
			bitSize = (unsigned char)((m_Data.size() * 8) / m_NumItems);
		else
			bitSize = m_BitSize;
		if (bitSize > 32)
			wasAbleToRead = false;
		if ((!pRange->IsDummy() && pRange->GetValue() && pRange->GetValue()->GetType() == ValueType_Float) &&
			(!pStart->IsDummy() && pStart->GetValue() && pStart->GetValue()->GetType() == ValueType_Float))
		{
			hasRangeAndStart = true;
			m_Range = pRange->GetValue()->AsFloat();
			m_Start = pStart->GetValue()->AsFloat();
		}
		else
			hasRangeAndStart = false;
	}
	else
		wasAbleToRead = false;
	return wasAbleToRead;
}
unsigned char PackedBitVector::MakeByteMask(unsigned int bitOffset)
{
	return (unsigned char)(0xFF << bitOffset);
}
bool PackedBitVector::ReadValue(unsigned int index, unsigned int &out)
{
	out = 0;
	if (bitSize == 0)
		return true;
	if (index >= m_NumItems)
		return false;
	unsigned int bitIndex = index * bitSize; //let's just assume that one compressed buffer doesn't use more than 512 MB
	if (bitIndex >= (0UL - bitSize))
		return false;
	unsigned int curByteIndex = bitIndex >> 3;
	unsigned int curBitOffset = bitIndex & 7;
	//Bounds checks; The start check is just there to prevent integer overflows.
	if ((curByteIndex >= m_Data.size()) || (((bitIndex + bitSize) >> 3) > m_Data.size()))
		return false;
	//Copy the potentially not byte-aligned data first.
	out = ((m_Data[curByteIndex] & MakeByteMask(curBitOffset)) >> curBitOffset);
	unsigned int curOutBitOffset = 8 - curBitOffset;
	curBitOffset = 0;
	curByteIndex++;
	//Copy the byte-aligned data.
	while (curOutBitOffset < bitSize)
	{
		out |= m_Data[curByteIndex] << curOutBitOffset;
		curOutBitOffset += 8;
		curByteIndex++;
	}
	//Limit the data bits as the previous copies always read until the end of the current byte, while the actual value might end a couple of bits before.
	out &= (1 << bitSize) - 1; //Even works when bitSize == 32.
	return true;
}
bool PackedBitVector::ReadValueFloat(unsigned int index, float &out)
{
	if (!hasRangeAndStart)
		return false;
	unsigned int raw;
	if (!ReadValue(index, raw))
		return false;
	//Use double for the first step to reduce the overall error (there could be up to 32 bits representing a number between 0.0 and 1.0).
	out = (float)((((double)raw) / ((1 << bitSize) - 1)) * m_Range + m_Start);
	return true;
}
CompressedMesh::CompressedMesh()
{
	wasAbleToRead = false;
}
bool CompressedMesh::Read(AssetTypeValueField *pField)
{
	//If not stated otherwise, the following fields are present in U3.4 until (at least) U5.6.
	AssetTypeValueField *pVertices = (*pField)["m_Vertices"];
	AssetTypeValueField *pUV = (*pField)["m_UV"];
	AssetTypeValueField *pBindPoses = (*pField)["m_BindPoses"]; //Present starting with U3.5 until U4.7
	AssetTypeValueField *pNormals = (*pField)["m_Normals"];
	AssetTypeValueField *pTangents = (*pField)["m_Tangents"];
	AssetTypeValueField *pWeights = (*pField)["m_Weights"];
	AssetTypeValueField *pNormalSigns = (*pField)["m_NormalSigns"];
	AssetTypeValueField *pTangentSigns = (*pField)["m_TangentSigns"];
	AssetTypeValueField *pFloatColors = (*pField)["m_FloatColors"]; //Present starting with U5.0
	AssetTypeValueField *pBoneIndices = (*pField)["m_BoneIndices"];
	AssetTypeValueField *pTriangles = (*pField)["m_Triangles"];
	AssetTypeValueField *pColors = (*pField)["m_Colors"]; //Present starting with U3.5 until U4.7
	AssetTypeValueField *pUVInfo = (*pField)["m_UVInfo"]; //Present starting with U5.0
	if (!pVertices->IsDummy() && !pUV->IsDummy()/* && !pBindPoses->IsDummy()*/ && 
		!pNormals->IsDummy() && !pTangents->IsDummy() && !pWeights->IsDummy() && 
		!pNormalSigns->IsDummy() && !pTangentSigns->IsDummy() && !pBoneIndices->IsDummy() && 
		!pTriangles->IsDummy())
	{
		wasAbleToRead = true;
		wasAbleToRead &= m_Vertices.Read(pVertices);
		wasAbleToRead &= m_UV.Read(pUV);
		if (!pBindPoses->IsDummy())
			wasAbleToRead &= m_BindPoses.Read(pBindPoses);
		wasAbleToRead &= m_Normals.Read(pNormals);
		wasAbleToRead &= m_Tangents.Read(pTangents);
		wasAbleToRead &= m_Weights.Read(pWeights);
		wasAbleToRead &= m_NormalSigns.Read(pNormalSigns);
		wasAbleToRead &= m_TangentSigns.Read(pTangentSigns);
		if (!pFloatColors->IsDummy())
			wasAbleToRead &= m_FloatColors.Read(pFloatColors);
		wasAbleToRead &= m_BoneIndices.Read(pBoneIndices);
		wasAbleToRead &= m_Triangles.Read(pTriangles);
		if (!pColors->IsDummy())
			wasAbleToRead &= m_Colors.Read(pColors);
		if (!pUVInfo->IsDummy() && pUVInfo->GetValue() && pUVInfo->GetValue()->GetType() == ValueType_UInt32)
			m_UVInfo = pUVInfo->GetValue()->AsUInt();
		else
			m_UVInfo = 0;
	}
	else
		wasAbleToRead = false;
	return wasAbleToRead;
}
bool BlendShapeVertex::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pVertex = (*pField)["vertex"];
	AssetTypeValueField *pNormal = (*pField)["normal"];
	AssetTypeValueField *pTangent = (*pField)["tangent"];
	AssetTypeValueField *pIndex = (*pField)["index"];
	if (!pVertex->IsDummy() && !pNormal->IsDummy() && 
		!pTangent->IsDummy() && !pIndex->IsDummy())
	{
		if (!vertex.Read(pVertex))
			return false;
		if (!normal.Read(pNormal))
			return false;
		if (!tangent.Read(pTangent))
			return false;
		if (!pIndex->GetValue())
			return false;
		index = pIndex->GetValue()->AsUInt();
		return true;
	}
	return false;
}
bool MeshBlendShape::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pFirstVertex = (*pField)["firstVertex"];
	AssetTypeValueField *pVertexCount = (*pField)["vertexCount"];
	AssetTypeValueField *pHasNormals = (*pField)["hasNormals"];
	AssetTypeValueField *pHasTangents = (*pField)["hasTangents"];
	if (!pFirstVertex->IsDummy() && !pVertexCount->IsDummy() && 
		!pHasNormals->IsDummy() && !pHasTangents->IsDummy())
	{
		if (!pFirstVertex->GetValue())
			return false;
		firstVertex = pFirstVertex->GetValue()->AsUInt();
		if (!pVertexCount->GetValue())
			return false;
		vertexCount = pVertexCount->GetValue()->AsUInt();
		if (!pHasNormals->GetValue())
			return false;
		hasNormals = pHasNormals->GetValue()->AsBool();
		if (!pHasTangents->GetValue())
			return false;
		hasTangents = pHasTangents->GetValue()->AsBool();
		return true;
	}
	return false;
}
bool MeshBlendShapeChannel::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pName = (*pField)["name"];
	AssetTypeValueField *pNameHash = (*pField)["nameHash"];
	AssetTypeValueField *pFrameIndex = (*pField)["frameIndex"];
	AssetTypeValueField *pFrameCount = (*pField)["frameCount"];
	if (!pName->IsDummy() && !pNameHash->IsDummy() && 
		!pFrameIndex->IsDummy() && !pFrameCount->IsDummy())
	{
		if (!pName->GetValue())
			return false;
		name = pName->GetValue()->AsString();
		if (!pNameHash->GetValue())
			return false;
		nameHash = pNameHash->GetValue()->AsUInt();
		if (!pFrameIndex->GetValue())
			return false;
		frameIndex = pFrameIndex->GetValue()->AsInt();
		if (!pFrameCount->GetValue())
			return false;
		frameCount = pFrameCount->GetValue()->AsInt();
		return true;
	}
	return false;
}
BlendShapeData::BlendShapeData(){}
bool BlendShapeData::Read(AssetTypeValueField *pField)
{
	wasAbleToRead = false;
	AssetTypeValueField *pVertices = (*pField)["vertices"]->Get("Array");
	AssetTypeValueField *pShapes = (*pField)["shapes"]->Get("Array");
	AssetTypeValueField *pChannels = (*pField)["channels"]->Get("Array");
	AssetTypeValueField *pFullWeights = (*pField)["fullWeights"]->Get("Array");
	if (!pVertices->IsDummy() && !pShapes->IsDummy() && 
		!pChannels->IsDummy() && !pFullWeights->IsDummy())
	{
		vertices.resize(pVertices->GetChildrenCount());
		for (unsigned int i = 0; i < pVertices->GetChildrenCount(); i++)
		{
			if (!vertices[i].Read(pVertices->Get(i)))
			{
				vertices.clear();
				return false;
			}
		}
		
		shapes.resize(pShapes->GetChildrenCount());
		for (unsigned int i = 0; i < pShapes->GetChildrenCount(); i++)
		{
			if (!shapes[i].Read(pShapes->Get(i)))
			{
				vertices.clear();
				shapes.clear();
				return false;
			}
		}
		
		channels.resize(pChannels->GetChildrenCount());
		for (unsigned int i = 0; i < pChannels->GetChildrenCount(); i++)
		{
			if (!channels[i].Read(pChannels->Get(i)))
			{
				vertices.clear();
				shapes.clear();
				channels.clear();
				return false;
			}
		}
		
		fullWeights.resize(pFullWeights->GetChildrenCount());
		for (unsigned int i = 0; i < pFullWeights->GetChildrenCount(); i++)
		{
			AssetTypeValueField *pEntry = pFullWeights->Get(i);
			if (!pEntry->GetValue() || pEntry->GetValue()->GetType() != ValueType_Float)
			{
				vertices.clear();
				shapes.clear();
				channels.clear();
				fullWeights.clear();
				return false;
			}
			fullWeights[i] = pEntry->GetValue()->AsFloat();
		}
		wasAbleToRead = true;
		return true;
	}
	return false;
}

bool BoneInfluence::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pWeight[4] = {
		(*pField)["weight[0]"], (*pField)["weight[1]"], (*pField)["weight[2]"], (*pField)["weight[3]"]
	};
	AssetTypeValueField *pBoneIndex[4] = {
		(*pField)["boneIndex[0]"], (*pField)["boneIndex[1]"], (*pField)["boneIndex[2]"], (*pField)["boneIndex[3]"]
	};
	for (int i = 0; i < 4; i++)
	{
		if (pWeight[i]->GetValue() == NULL || pBoneIndex[i]->GetValue() == NULL || pWeight[i]->GetValue()->GetType() != ValueType_Float)
			return false;
		weight[i] = pWeight[i]->GetValue()->AsFloat();
		boneIndex[i] = pBoneIndex[i]->GetValue()->AsUInt();
	}
	return true;
}

VariableBoneCountWeights::VariableBoneCountWeights() {}
bool VariableBoneCountWeights::Read(AssetTypeValueField *pField)
{
	AssetTypeValueField *pDataArray = (*(*pField)["m_Data"])["Array"];
	if (!pDataArray->IsDummy())
	{
		m_Data.resize(pDataArray->GetChildrenCount());
		for (unsigned int i = 0; i < pDataArray->GetChildrenCount(); i++)
		{
			AssetTypeValue *pValue = pDataArray->Get(i)->GetValue();
			if (!pValue || pValue->GetType() != ValueType_UInt32)
				return false;
			m_Data[i] = pValue->AsUInt();
		}
		return true;
	}
	return false;
}

StreamingInfo::StreamingInfo()
{
	this->wasAbleToRead = false;
	this->size = 0;
	this->offset = 0;
}
bool StreamingInfo::Read(AssetTypeValueField *pField)
{
	this->wasAbleToRead = false;
	AssetTypeValueField *pOffset = (*pField)["offset"];
	AssetTypeValueField *pSize = (*pField)["size"];
	AssetTypeValueField *pPath = (*pField)["path"];
	if (pOffset->GetValue() && pOffset->GetValue()->GetType() == ValueType_UInt32 &&
		pSize->GetValue() && pSize->GetValue()->GetType() == ValueType_UInt32 &&
		pPath->GetValue() && pPath->GetValue()->GetType() == ValueType_String && pPath->GetValue()->AsString())
	{
		this->offset = pOffset->GetValue()->AsUInt64();
		this->size = pSize->GetValue()->AsUInt();
		this->path = std::string(pPath->GetValue()->AsString());
		this->wasAbleToRead = true;
	}
	return this->wasAbleToRead;
}

Mesh::Mesh(AssetTypeValueField *pField, bool unity5OrNewer, AppContext &appContext, AssetIdentifier &asset)
{
	m_SubMeshes = std::vector<SubMesh>();
	m_IndexBuffer = std::vector<unsigned int>();
	m_VertexData = VertexData();
	wasAbleToRead = false;
	AssetTypeValueField *pNameField = (*pField)["m_Name"];
	AssetTypeValueField *pSubMeshesField = (*(*pField)["m_SubMeshes"])["Array"];
	AssetTypeValueField *pShapesField = (*pField)["m_Shapes"];
	AssetTypeValueField *pBindPoseField = (*(*pField)["m_BindPose"])["Array"];
	AssetTypeValueField *pBoneNameHashesField = (*(*pField)["m_BoneNameHashes"])["Array"];
	AssetTypeValueField *pRootBoneNameHashField = (*pField)["m_RootBoneNameHash"];
	AssetTypeValueField *pBonesAABBField = (*(*pField)["m_BonesAABB"])["Array"];
	AssetTypeValueField *pVariableBoneCountWeightsField = (*pField)["m_VariableBoneCountWeights"];
	AssetTypeValueField *pMeshCompressionField = (*pField)["m_MeshCompression"];
	AssetTypeValueField *pIndexFormatField = (*pField)["m_IndexFormat"];
	AssetTypeValueField *pIndexBufferField = (*(*pField)["m_IndexBuffer"])["Array"];
	AssetTypeValueField *pSkinField = (*(*pField)["m_Skin"])["Array"];
	AssetTypeValueField *pVertexDataField = (*pField)["m_VertexData"];
	AssetTypeValueField *pCompressedMeshField = (*pField)["m_CompressedMesh"];
	AssetTypeValueField *pLocalAABBField = (*pField)["m_LocalAABB"];
	AssetTypeValueField *pStreamDataField = (*pField)["m_StreamData"];
	if (!pNameField->IsDummy() && !pSubMeshesField->IsDummy() && !pMeshCompressionField->IsDummy() && !pIndexBufferField->IsDummy()
			&& !pVertexDataField->IsDummy() && !pLocalAABBField->IsDummy())
	{
		wasAbleToRead = true;
		m_Name = pNameField->GetValue()->AsString();
		if (!m_Name)
			wasAbleToRead = false; //to prevent access violations
		uint32_t subMeshCount = pSubMeshesField->GetValue()->AsArray()->size;
		m_SubMeshes.reserve(subMeshCount);
		for (uint32_t i = 0; i < subMeshCount; i++)
		{
			SubMesh subMesh;
			if (!subMesh.Read((*pSubMeshesField)[i]))
				wasAbleToRead = false;
			else
				m_SubMeshes.push_back(subMesh);
		}

		if (!pShapesField->IsDummy())
			m_Shapes.Read(pShapesField);
		else
			m_Shapes.wasAbleToRead = false;
		if (!pBindPoseField->IsDummy())
		{
			m_BindPose.resize(pBindPoseField->GetChildrenCount());
			for (unsigned int i = 0; i < pBindPoseField->GetChildrenCount(); i++)
			{
				if (!m_BindPose[i].Read(pBindPoseField->Get(i)))
				{
					m_BindPose.clear();
					break;
				}
			}
		}
		if (!pBoneNameHashesField->IsDummy())
		{
			m_BoneNameHashes.resize(pBoneNameHashesField->GetChildrenCount());
			for (unsigned int i = 0; i < pBindPoseField->GetChildrenCount(); i++)
			{
				AssetTypeValue *pValue = pBoneNameHashesField->Get(i)->GetValue();
				if (!pValue)
				{
					m_BoneNameHashes.clear();
					break;
				}
				m_BoneNameHashes[i] = pValue->AsUInt();
			}
		}
		if (pRootBoneNameHashField->GetValue())
			m_RootBoneNameHash = pRootBoneNameHashField->GetValue()->AsUInt();
		else
			m_RootBoneNameHash = 0;

		if (!pBonesAABBField->IsDummy())
		{
			m_BonesAABB.resize(pBonesAABBField->GetChildrenCount());
			for (unsigned int i = 0; i < pBonesAABBField->GetChildrenCount(); i++)
			{
				if (!m_BonesAABB[i].Read(pBonesAABBField->Get(i)))
				{
					m_BonesAABB.clear();
					break;
				}
			}
		}
		if (!pVariableBoneCountWeightsField->IsDummy())
		{
			m_VariableBoneCountWeights.Read(pVariableBoneCountWeightsField);
		}

		m_MeshCompression = (unsigned char)pMeshCompressionField->GetValue()->AsUInt();

		if (!pIndexFormatField->IsDummy())
			m_IndexFormat = pIndexFormatField->GetValue()->AsInt();
		else
			m_IndexFormat = 0; //16 bit is default.

		int indexCount = pIndexBufferField->GetValue()->AsArray()->size;
		switch (m_IndexFormat)
		{
		case 0: //UInt16
			if (indexCount & 1)
				wasAbleToRead = false; //the indices obviously are no shorts
			else
			{
				m_IndexBuffer.reserve(indexCount/2); //index array is a byte array; the indices always(?) are shorts
				for (int i = 0; i < (indexCount-1); i+=2)
				{
					m_IndexBuffer.push_back((unsigned short)(*pIndexBufferField)[i]->GetValue()->AsInt() | ((unsigned short)(*pIndexBufferField)[i+1]->GetValue()->AsInt() << 8));
				}
			}
			break;
		case 1: //UInt32
			if (indexCount & 3)
				wasAbleToRead = false; //the indices obviously are no ints
			else
			{
				m_IndexBuffer.reserve(indexCount/4); //index array is a byte array; the indices always are ints
				for (int i = 0; i < (indexCount-3); i+=4)
				{
					m_IndexBuffer.push_back(
							(unsigned int)(*pIndexBufferField)[i]->GetValue()->AsInt() 
						| ((unsigned int)(*pIndexBufferField)[i+1]->GetValue()->AsInt() << 8)
						| ((unsigned int)(*pIndexBufferField)[i+2]->GetValue()->AsInt() << 16)
						| ((unsigned int)(*pIndexBufferField)[i+3]->GetValue()->AsInt() << 24)
						);
				}
			}
			break;
		default:
			wasAbleToRead = false;
			break;
		}
		
		if (!pSkinField->IsDummy())
		{
			m_Skin.resize(pSkinField->GetChildrenCount());
			for (unsigned int i = 0; i < pSkinField->GetChildrenCount(); i++)
			{
				if (!m_Skin[i].Read(pSkinField->Get(i)))
				{
					m_Skin.clear();
					break;
				}
			}
		}

		if (!pStreamDataField->IsDummy())
			m_StreamData.Read(pStreamDataField);

		m_VertexData = VertexData(pVertexDataField, appContext, m_StreamData, asset, m_Skin, unity5OrNewer);
		if (!m_VertexData.wasAbleToRead)
			wasAbleToRead = false;
		if (!pCompressedMeshField->IsDummy())
			m_CompressedMesh.Read(pCompressedMeshField);
		if (!m_LocalAABB.Read(pLocalAABBField))
			wasAbleToRead = false;
	}
}