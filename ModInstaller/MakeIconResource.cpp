#include "stdafx.h"
#include "MakeIconResource.h"

typedef struct
{
	uint16_t idReserved; // Reserved (must be 0)
	uint16_t idType; // Resource type (1 for icons)
	uint16_t idCount; // How many images?
} GRPICONDIR, *LPGRPICONDIR;
#define SIZEOF_GRPICONDIR 6
typedef struct
{
	uint8_t bWidth; // Width, in pixels, of the image
	uint8_t bHeight; // Height, in pixels, of the image
	uint8_t bColorCount; // Number of colors in image (0 if >=8bpp)
	uint8_t bReserved; // Reserved
	uint16_t wPlanes; // Color Planes
	uint16_t wBitCount; // Bits per pixel
	DWORD dwBytesInRes; // how many bytes in this resource?
	uint16_t nID; // the ID
} GRPICONDIRENTRY, *LPGRPICONDIRENTRY; 
#define SIZEOF_GRPICONDIRENTRY 14
typedef struct
{
	uint8_t bWidth; // Width, in pixels, of the image
	uint8_t bHeight; // Height, in pixels, of the image
	uint8_t bColorCount; // Number of colors in image (0 if >=8bpp)
	uint8_t bReserved; // Reserved
	uint16_t wPlanes; // Color Planes
	uint16_t wBitCount; // Bits per pixel
	DWORD dwBytesInRes; // how many bytes in this resource?
	DWORD dwOffset; //byte offset from .ico file base
} GRPICONENTRY, *LPGRPICONENTRY; 
#define SIZEOF_GRPICONENTRY 16
static bool MakeIconResource(const std::vector<uint8_t> &icoData, std::vector<uint8_t> &iconGroup, std::vector<std::vector<uint8_t>> &iconData)
{
	const LPGRPICONDIR pIconHeader = (const LPGRPICONDIR)icoData.data();
	if ((icoData.size() < SIZEOF_GRPICONDIR) || (icoData.size() < (SIZEOF_GRPICONDIR + pIconHeader->idCount * SIZEOF_GRPICONENTRY)))
		return false;
	for (uint16_t i = 0; i < pIconHeader->idCount; i++)
	{
		LPGRPICONENTRY pCurEntry = (LPGRPICONENTRY)&(icoData[SIZEOF_GRPICONDIR + i * SIZEOF_GRPICONENTRY]);
		if (icoData.size() < (pCurEntry->dwOffset + pCurEntry->dwBytesInRes))
			return false;
	}
	iconGroup.resize(SIZEOF_GRPICONDIR + pIconHeader->idCount * SIZEOF_GRPICONDIRENTRY);
	memcpy(iconGroup.data(), pIconHeader, SIZEOF_GRPICONDIR);
	iconData.resize(pIconHeader->idCount);
	for (uint16_t i = 0; i < pIconHeader->idCount; i++)
	{
		LPGRPICONENTRY pCurEntry = (LPGRPICONENTRY)&(icoData[SIZEOF_GRPICONDIR + i * SIZEOF_GRPICONENTRY]);
		LPGRPICONDIRENTRY pCurDirEntry = (LPGRPICONDIRENTRY)&(iconGroup[SIZEOF_GRPICONDIR + i * SIZEOF_GRPICONDIRENTRY]);
		memcpy(pCurDirEntry, pCurEntry, SIZEOF_GRPICONDIRENTRY - 2);
		pCurDirEntry->nID = i + 1;
		iconData[i].resize(pCurDirEntry->dwBytesInRes);
		memcpy(iconData[i].data(), &(icoData[pCurEntry->dwOffset]), pCurEntry->dwBytesInRes);
	}
	return true;
}
static bool MakeIconResource(std::vector<HICON> hIcons, std::vector<uint8_t> &iconGroup, std::vector<std::vector<uint8_t>>&iconData)
{
	//http://www.codeproject.com/Articles/4945/UpdateResource?msg=2766314#xx2766314xx

	GRPICONDIR dirBase = {};
	dirBase.idReserved = 0; dirBase.idType = 1;
	dirBase.idCount = std::min<uint16_t>((uint16_t)hIcons.size(),0xFFFE);
	std::vector<GRPICONDIRENTRY> dirEntries;
	dirEntries.resize(std::min<uint16_t>((uint16_t)hIcons.size(),0xFFFE));
	iconData.resize(std::min<uint16_t>((uint16_t)hIcons.size(),0xFFFE));
	for (size_t i = 0; i < hIcons.size() && i < 0xFFFF; i++)
	{
		ICONINFO info = {};
		GetIconInfo(hIcons[i], &info);
		BITMAP bitmapColorInfo = {};
		BITMAP bitmapMaskInfo = {};
		if (!GetObject(info.hbmColor, sizeof(bitmapColorInfo), &bitmapColorInfo))
		{
			DeleteObject(info.hbmColor);
			DeleteObject(info.hbmMask);
			return false;
		}
		else
		{
			bool hasAlpha = false;
			bool hasMask = GetObject(info.hbmMask, sizeof(bitmapMaskInfo), &bitmapMaskInfo) != 0 && !hasAlpha;
			if (hasMask && bitmapMaskInfo.bmBitsPixel != 1)
				return false;

			GRPICONDIRENTRY &entry = dirEntries[i];
			entry.bWidth = (uint8_t)bitmapColorInfo.bmWidth;
			entry.bHeight = (uint8_t)bitmapColorInfo.bmHeight;
			entry.bColorCount = 0;//(bitmapColorInfo.bmBitsPixel >= 8) ? 0 : (1 << bitmapColorInfo.bmBitsPixel); Size of palette
			entry.bReserved = 0;
			entry.wPlanes = 1;//bitmapColorInfo.bmPlanes;
			entry.wBitCount = 32;//(hasMask || hasAlpha) ? 32 : 24;//bitmapColorInfo.bmBitsPixel;
			entry.nID = 1 + (uint16_t)i;
			DWORD xorStride = (((entry.bWidth * entry.wBitCount) + 31) & (~31)) >> 3;
			DWORD andStride = (((entry.bWidth * 1) + 31) & (~31)) >> 3; //There MUST be a mask in the icon, if hasMask == false, we fill this with 1
			entry.dwBytesInRes = sizeof(BITMAPINFOHEADER) + entry.bHeight * xorStride + entry.bHeight * andStride;//(DWORD)iconDataSize;
			//only width, height and color format info is stored in bitmapInfo
			std::vector<COLORREF> colorBitsTmp(xorStride * entry.bHeight);
		
			HDC hDC = CreateCompatibleDC(NULL);
			BITMAPINFO bmpInfo = {};
			bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmpInfo.bmiHeader.biBitCount = 0;
			bmpInfo.bmiHeader.biWidth = entry.bWidth;
			bmpInfo.bmiHeader.biHeight = entry.bHeight;
			bmpInfo.bmiHeader.biPlanes = 1;
			bmpInfo.bmiHeader.biBitCount = 32;//bitmapColorInfo.bmBitsPixel;
			bmpInfo.bmiHeader.biCompression = BI_RGB;
			bmpInfo.bmiHeader.biSizeImage = 0;
			SelectObject(hDC, info.hbmColor);
			int ret = GetDIBits(hDC, info.hbmColor, 0, entry.bHeight, colorBitsTmp.data(), &bmpInfo, DIB_RGB_COLORS);
			if (ret != entry.bHeight)
			{
				DeleteObject(info.hbmColor);
				if (hasMask)
					DeleteObject(info.hbmMask);
				return false;
			}
			std::vector<uint8_t> outIconData(entry.dwBytesInRes);
			BITMAPINFOHEADER *rawIconData_Header = (BITMAPINFOHEADER*)outIconData.data();
			void *rawIconData_XOR = (void*)&(outIconData[sizeof(BITMAPINFOHEADER)]);
			void *rawIconData_AND = (void*)&(outIconData[sizeof(BITMAPINFOHEADER) + entry.bHeight * xorStride]);
			if (hasMask)
			{
				memcpy(rawIconData_XOR, colorBitsTmp.data(), (entry.bWidth * entry.bHeight) * 4); //always 32bit per pixel
				std::vector<uint8_t> bmpInfoBuf(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
				BITMAPINFO *_bmpInfo = reinterpret_cast<BITMAPINFO*>(bmpInfoBuf.data());
				_bmpInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				_bmpInfo->bmiHeader.biWidth = entry.bWidth;
				_bmpInfo->bmiHeader.biHeight = entry.bHeight;
				_bmpInfo->bmiHeader.biPlanes = 1;
				_bmpInfo->bmiHeader.biBitCount = 1;//bitmapColorInfo.bmBitsPixel;
				_bmpInfo->bmiHeader.biCompression = BI_RGB;
				_bmpInfo->bmiHeader.biSizeImage = 0;
				SelectObject(hDC, info.hbmMask);
				if (GetDIBits(hDC, info.hbmMask, 0, entry.bHeight, colorBitsTmp.data(), _bmpInfo, DIB_RGB_COLORS) != entry.bHeight)
				{
					DeleteObject(info.hbmColor);
					if (hasMask)
						DeleteObject(info.hbmMask);
					return false;
				}
				memcpy(rawIconData_AND, colorBitsTmp.data(), entry.bHeight * andStride);
			}
			else
			{
				memcpy(rawIconData_XOR, colorBitsTmp.data(), (entry.bWidth * entry.bHeight) * 4); //always 32bit per pixel
				memset(rawIconData_AND, 1, andStride * entry.bHeight); //always 32bit per pixel
			}
			rawIconData_Header->biSize = sizeof(BITMAPINFOHEADER);
			rawIconData_Header->biWidth = entry.bWidth;
			rawIconData_Header->biHeight = entry.bHeight * 2; //once intended (1 bit per color times) to easily determine the size
			rawIconData_Header->biPlanes = entry.wPlanes;
			rawIconData_Header->biBitCount = entry.wBitCount;
			rawIconData_Header->biCompression = 0;
			rawIconData_Header->biSizeImage = 0;
			rawIconData_Header->biXPelsPerMeter = 0;
			rawIconData_Header->biYPelsPerMeter = 0;
			rawIconData_Header->biClrUsed = 0;
			rawIconData_Header->biClrImportant = 0;

			DeleteObject(info.hbmColor);
			if (hasMask)
				DeleteObject(info.hbmMask);

			iconData[i] = std::move(outIconData);
		}
	}
	iconGroup.resize(SIZEOF_GRPICONDIR + dirEntries.size() * SIZEOF_GRPICONDIRENTRY);
	memcpy(iconGroup.data(), &dirBase, SIZEOF_GRPICONDIR);
	for (size_t i = 0; i < dirEntries.size(); i++)
		memcpy(&(iconGroup.data()[SIZEOF_GRPICONDIR + i * SIZEOF_GRPICONDIRENTRY]), &dirEntries[i], SIZEOF_GRPICONDIRENTRY);
	return true;
}

bool SetProgramIconResource(const TCHAR *filePath, const std::vector<uint8_t> &iconFileData)
{
	std::vector<uint8_t> iconGroup;
	std::vector<std::vector<uint8_t>> iconDataList;
	if (MakeIconResource(iconFileData, iconGroup, iconDataList))
	{
		HANDLE hUpdate = BeginUpdateResource(filePath, FALSE);
		if (hUpdate)
		{
			#define IDI_MODINSTALLER 102
			//UpdateResource(hUpdate, RT_ICON, MAKEINTRESOURCE(IDI_MODINSTALLER), 
			//	MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), userData->iconData, userData->iconDataSize);
														
			UpdateResource(hUpdate, RT_GROUP_ICON, MAKEINTRESOURCE(IDI_MODINSTALLER), 
				MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), iconGroup.data(), (DWORD)iconGroup.size());
			for (size_t i = 0; i < iconDataList.size(); i++)
			{
				UpdateResource(hUpdate, RT_ICON, MAKEINTRESOURCE(1+i), 
					MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), iconDataList[i].data(), (DWORD)iconDataList[i].size());
			}
			//UpdateResource(hUpdate, RT_ICON, MAKEINTRESOURCE(1), 
			//	MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), pIconData, iconDataLen);
			EndUpdateResource(hUpdate, FALSE);
			return true;
		}
	}
	return false;
}
