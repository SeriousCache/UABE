#include "../AssetsTools/TextureFileFormat.h"
#include <crnlib.h>
#include <crn_decomp.h>
#include <thread>
#include <vector>

//Recursive ## macro evaluation (required for WRAPSUFFIX) needs extra layers
//https://stackoverflow.com/a/1597129
#define _CONCAT(a,b) a ## b
#define _WN2(a, b) _CONCAT(a, b)
#define _WN(NAME) _WN2(NAME,WRAPSUFFIX)
#define API __declspec(dllexport)

static crn_bool CrunchProcessCallback(crn_uint32 phase_index, crn_uint32 total_phases, crn_uint32 subphase_index, crn_uint32 total_subphases, void* pUser_data_ptr)
{
	//int percentage_complete = (int)(.5f + (phase_index + float(subphase_index) / total_subphases) * 100.0f) / total_phases;
	//printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\bProcessing: %u%%", std::min(100, std::max(0, percentage_complete)));
	return true;
}
API bool _WN(CrunchTextureData_RGBA32_)(TextureFile* pTex, const void* pRGBA32Buf,
	void* pOutBuf, QWORD& outputSize, int compressQuality, unsigned int curWidth, unsigned int curHeight)
{
	crn_comp_params compPars; compPars.clear();
	compPars.m_width = curWidth;
	compPars.m_height = curHeight;
	compPars.m_levels = 1;//(mipMapOffsets.size() > cCRNMaxLevels) ? cCRNMaxLevels : (crn_uint32)mipMapOffsets.size();
	compPars.m_file_type = cCRNFileTypeCRN;
	switch (pTex->m_TextureFormat)
	{
	case TexFmt_DXT1Crunched:
		compPars.m_format = cCRNFmtDXT1;
		break;
	case TexFmt_DXT5Crunched:
		compPars.m_format = cCRNFmtDXT5;
		break;
	case TexFmt_ETC_RGB4Crunched:
		compPars.m_format = cCRNFmtETC1;
		pTex->extra.textureFormatVersion = 1;
		break;
#ifdef HAS_ETC2
	case TexFmt_ETC2_RGBA8Crunched:
		compPars.m_format = cCRNFmtETC2A;
		pTex->extra.textureFormatVersion = 1;
		break;
#endif
	default:
		return false;
	}
	compPars.m_pImages[0][0] = (crn_uint32*)pRGBA32Buf;
	//compPars.m_quality_level = 255;
	compPars.m_num_helper_threads = 0;//(compressQuality >= 1) ? (compressQuality - 1) : 1;
	compPars.m_pProgress_func = CrunchProcessCallback;

	uint32_t maxNumThreads = std::thread::hardware_concurrency();
	if (maxNumThreads == 0) maxNumThreads = 2;
	//16==cCRNMaxHelperThreads+1 for Unity, cCRNMaxHelperThreads for Legacy
	// (the legacy version constant was too high by one,
	//  since 'helper threads' mean all threads but the main compression thread).
	//Assuming we can have 16 threads total.
	if (maxNumThreads > 16) maxNumThreads = 16;
	bool mt = false;
	switch (compressQuality)
	{
	case 1: compPars.m_num_helper_threads = maxNumThreads - 1; //normal mt
	case 0: //normal
	default:
		compPars.m_quality_level = 128;
		break;
	case 3: compPars.m_num_helper_threads = maxNumThreads - 1; //very fast mt
	case 2: //very fast
		compPars.m_quality_level = 0; //cCRNMinQualityLevel
		break;
	case 5: compPars.m_num_helper_threads = maxNumThreads - 1; //slow mt
	case 4: //slow
		compPars.m_quality_level = 255; //cCRNMaxQualityLevel
		break;
	}
	compPars.m_userdata0 = CRNVERSION;

	crn_mipmap_params mipPars; mipPars.clear();
	mipPars.m_gamma_filtering = false;
	mipPars.m_max_levels = (pTex->m_MipCount > cCRNMaxLevels) ? cCRNMaxLevels : pTex->m_MipCount;
	if (mipPars.m_max_levels < 1) mipPars.m_max_levels = 1;
	mipPars.m_mode = (pTex->m_MipCount > 1) ? cCRNMipModeGenerateMips : cCRNMipModeNoMips;

	crn_uint32 crunchedQuality;
	float crunchedBitrate;
	crn_uint32 crunchedSize;

	void* crnCompressed = crn_compress(compPars, mipPars, crunchedSize, &crunchedQuality, &crunchedBitrate);
	if (crnCompressed)
	{
		if (outputSize < crunchedSize)
		{
			crn_free_block(crnCompressed);
			outputSize = 0;
			return false;
		}

		outputSize = crunchedSize;
		memcpy(pOutBuf, crnCompressed, crunchedSize);

		crnd::crn_texture_info info;
		if (crnd::crnd_get_texture_info(crnCompressed, crunchedSize, &info))
		{
			pTex->m_MipCount = info.m_levels;
		}

		crn_free_block(crnCompressed);
		return true;
	}
	else
	{
		outputSize = 0;
		return false;
	}
}
API bool _WN(DecrunchTextureData_)(TextureFile* pTex, std::vector<uint8_t>& decrunchBuf, TextureFormat &decrunchFormat)
{
	pTex->m_MipCount = 1;
	crnd::crn_texture_info tex_info;
	if (!crnd::crnd_get_texture_info(pTex->pPictureData, pTex->_pictureDataSize, &tex_info))
		return false;
	switch (pTex->m_TextureFormat)
	{
	case TexFmt_DXT1Crunched:
		if (tex_info.m_format != cCRNFmtDXT1)
			return false;
		decrunchFormat = TexFmt_DXT1;
		break;
	case TexFmt_DXT5Crunched:
		if (tex_info.m_format != cCRNFmtDXT5)
			return false;
		decrunchFormat = TexFmt_DXT5;
		break;
	case TexFmt_ETC_RGB4Crunched:
		if (tex_info.m_format != cCRNFmtETC1)
			return false;
		decrunchFormat = TexFmt_ETC_RGB4;
		break;
#ifdef HAS_ETC2
	case TexFmt_ETC2_RGBA8Crunched:
		if (tex_info.m_format != cCRNFmtETC2A)
			return false;
		decrunchFormat = TexFmt_ETC2_RGBA8;
		break;
#endif
	default:
		return false;
	}
	crn_uint32 blockCountX = (tex_info.m_width + 3) >> 2; if (!blockCountX) blockCountX = 1;
	crn_uint32 blockCountY = (tex_info.m_height + 3) >> 2; if (!blockCountY) blockCountY = 1;
	crn_uint32 row_pitch = blockCountX * crnd::crnd_get_bytes_per_dxt_block(tex_info.m_format);
	size_t dataSize = (size_t)row_pitch * blockCountY;
	decrunchBuf.resize(dataSize);

	crnd::crnd_unpack_context ctx = crnd::crnd_unpack_begin(pTex->pPictureData, pTex->_pictureDataSize);
	void* bufptr = decrunchBuf.data();
	if (!crnd::crnd_unpack_level(ctx, &bufptr, (unsigned int)decrunchBuf.size(), row_pitch, 0))
	{
		decrunchBuf.clear();
		return false;
	}
	return true;
}
