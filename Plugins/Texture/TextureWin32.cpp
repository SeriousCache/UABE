#include "resource.h"
#include "defines.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WindowsX.h>
#include <tchar.h>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <chrono>
#include "../libStringConverter/convert.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/CreateEmptyValueField.h"
#include "../UABE_Win32/Win32PluginManager.h"
#include "../UABE_Win32/Win32AppContext.h"
#include "../UABE_Win32/Win32BatchImportDesc.h"
#include "../UABE_Win32/BatchImportDialog.h"
#include "../UABE_Win32/FileDialog.h"
#include "Texture.h"

#define IsPowerOfTwo(n) ((n!=0)&&!(n&(n-1)))

HMODULE g_hModule;

INT_PTR CALLBACK CompressQualityDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
		TextureImportParam* pParam = (TextureImportParam*)lParam;

		HWND hCbQuality = GetDlgItem(hDlg, IDC_CBQUALITY);
		std::vector<const wchar_t*> presetNames;
		size_t defaultPresetIdx = GetQualityPresetList(pParam->batchInfo.newTextureFormat, presetNames);
		if (defaultPresetIdx >= presetNames.size() || presetNames.size() >= 0x7FFFFFFF)
		{
			EndDialog(hDlg, 0);
		}
		else
		{
			pParam->qualitySelection = GetQualityFromPresetListIdx(pParam->batchInfo.newTextureFormat, defaultPresetIdx);
			for (size_t i = 0; i < presetNames.size(); i++)
			{
				ComboBox_AddString(hCbQuality, presetNames[i]);
			}
			ComboBox_SetCurSel(hCbQuality, (int)defaultPresetIdx);
		}
		return (INT_PTR)TRUE;
	}
	case WM_CLOSE:
	case WM_DESTROY:
	{
		TextureImportParam* pParam = (TextureImportParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

		int selection = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_CBQUALITY));
		if (selection < 0) selection = 0;

		ECompressorQuality newQuality = GetQualityFromPresetListIdx(pParam->batchInfo.newTextureFormat, (size_t)selection);
		if (pParam->qualitySelection != newQuality)
		{
			pParam->qualitySelected = true;
			pParam->qualitySelection = newQuality;
		}
	}
	break;
	case WM_COMMAND:
	{
		WORD wmId = LOWORD(wParam);
		WORD wmEvent = HIWORD(wParam);
		switch (wmId)
		{
		case IDOK:
		case IDCANCEL:
		{
			EndDialog(hDlg, (INT_PTR)lParam);
			return (INT_PTR)TRUE;
		}
		break;
		}
	}
	}
	return (INT_PTR)FALSE;
}
INT_PTR CALLBACK ImportDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	TextureImportParam* pParam = (TextureImportParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	TCHAR tPrint[128];
	int wmId, wmEvent;
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_DESTROY:
	break;
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
		TextureImportParam* pParam = (TextureImportParam*)lParam;
		if (pParam->importTextureName.size() > 0)
		{
			auto tc_importTextureName = unique_MultiByteToTCHAR(pParam->importTextureName.c_str());
			Edit_SetText(GetDlgItem(hDlg, IDC_ENAME), tc_importTextureName.get());
		}
		HWND hCBTexFmt = GetDlgItem(hDlg, IDC_CBTEXFMT);

		HWND hCKMipMap = GetDlgItem(hDlg, IDC_CKMIPMAP);

		HWND hCKReadable = GetDlgItem(hDlg, IDC_CKREADABLE);

		HWND hCKReadAllowed = GetDlgItem(hDlg, IDC_CKREADALLOWED);

		HWND hCBFilterMode = GetDlgItem(hDlg, IDC_CBFILTERMODE);
		ComboBox_AddString(hCBFilterMode, TEXT("Point"));
		ComboBox_AddString(hCBFilterMode, TEXT("Bilinear"));
		ComboBox_AddString(hCBFilterMode, TEXT("Trilinear"));

		HWND hEAniso = GetDlgItem(hDlg, IDC_EANISO);

		HWND hEMipBias = GetDlgItem(hDlg, IDC_EMIPBIAS);

		HWND hCBWrapMode = GetDlgItem(hDlg, IDC_CBWRAPMODE);
		ComboBox_AddString(hCBWrapMode, TEXT("Repeat"));
		ComboBox_AddString(hCBWrapMode, TEXT("Clamp"));

		HWND hCBWrapModeU = GetDlgItem(hDlg, IDC_CBWRAPMODEU);
		ComboBox_AddString(hCBWrapModeU, TEXT("Repeat"));
		ComboBox_AddString(hCBWrapModeU, TEXT("Clamp"));
		ComboBox_AddString(hCBWrapModeU, TEXT("Mirror")); //2017.1
		ComboBox_AddString(hCBWrapModeU, TEXT("MirrorOnce")); //2017.1

		HWND hCBWrapModeV = GetDlgItem(hDlg, IDC_CBWRAPMODEV);
		ComboBox_AddString(hCBWrapModeV, TEXT("Repeat"));
		ComboBox_AddString(hCBWrapModeV, TEXT("Clamp"));
		ComboBox_AddString(hCBWrapModeV, TEXT("Mirror"));
		ComboBox_AddString(hCBWrapModeV, TEXT("MirrorOnce"));

		HWND hELightMapFmt = GetDlgItem(hDlg, IDC_ELIGHTMAPFMT);

		HWND hCBClSpace = GetDlgItem(hDlg, IDC_CBCLSPACE);
		ComboBox_AddString(hCBClSpace, TEXT("Gamma"));
		ComboBox_AddString(hCBClSpace, TEXT("Linear"));

		if (!pParam->assetWasTextureFile)
		{
			pParam->importTextureInfo.m_ForcedFallbackFormat = 0;
			pParam->importTextureInfo.m_DownscaleFallback = false;
			pParam->importTextureInfo.m_TextureFormat = TexFmt_DXT5;
			pParam->importTextureInfo.m_MipMap = true;
			pParam->importTextureInfo.m_IsReadable = false;
			pParam->importTextureInfo.m_ReadAllowed = true;
			pParam->importTextureInfo.m_StreamingMipmaps = false;
			pParam->importTextureInfo.m_StreamingMipmapsPriority = 0;
			pParam->importTextureInfo.m_TextureSettings.m_FilterMode = 2;
			pParam->importTextureInfo.m_TextureSettings.m_Aniso = 2;
			pParam->importTextureInfo.m_TextureSettings.m_MipBias = 0.0F;
			pParam->importTextureInfo.m_TextureSettings.m_WrapMode = 1;
			pParam->importTextureInfo.m_TextureSettings.m_WrapU = 1;
			pParam->importTextureInfo.m_TextureSettings.m_WrapV = 1;
			pParam->importTextureInfo.m_TextureSettings.m_WrapW = 1;
			pParam->importTextureInfo.m_LightmapFormat = 0;
			pParam->importTextureInfo.m_ColorSpace = 1;

			pParam->importTextureInfo.m_MipCount = 1;
			pParam->importTextureInfo.m_StreamData.offset = 0;
			pParam->importTextureInfo.m_StreamData.size = 0;
			pParam->importTextureInfo.m_StreamData.path.clear();

			//Retrieve texture format version.
			SupportsTextureFormat(pParam->asset.pFile->getAssetsFileContext()->getAssetsFile(),
				TexFmt_ARGB32,
				pParam->importTextureInfo.extra.textureFormatVersion);
		}
		pParam->importTextureInfo.m_TextureDimension = 2;
		pParam->importTextureInfo.m_ImageCount = 1;

		{
			size_t textureTypeIndex;
			if (pParam->batchInfo.newTextureFormat == TexFmt_BGRA32Old)
				textureTypeIndex = GetTextureNameIDPair(TexFmt_BGRA32New, pParam->importTextureInfo.extra.textureFormatVersion);
			else
				textureTypeIndex = GetTextureNameIDPair(pParam->batchInfo.newTextureFormat, pParam->importTextureInfo.extra.textureFormatVersion);
			if (textureTypeIndex == (size_t)-1) textureTypeIndex = 0;
			int cbTargetSel = 0;
			for (size_t i = 0, cbIdx = 0; i < SupportedTextureNames_size; i++)
			{
				int texVersion = -1;
				if (SupportsTextureFormat(pParam->asset.pFile->getAssetsFileContext()->getAssetsFile(),
						SupportedTextureNames[i].textureType,
						texVersion)
					&& IsTextureNameIDPairInRange(&SupportedTextureNames[i], texVersion))
				{
					ComboBox_AddString(hCBTexFmt, SupportedTextureNames[i].name);
					ComboBox_SetItemData(hCBTexFmt, cbIdx, i);
					if (textureTypeIndex == i)
						cbTargetSel = (int)cbIdx;
					cbIdx++;
				}
			}
			ComboBox_SetCurSel(hCBTexFmt, (int)cbTargetSel);

			if (pParam->assetWasTextureFile && IsPowerOfTwo(pParam->importTextureInfo.m_Width) && IsPowerOfTwo(pParam->importTextureInfo.m_Height))
			{
				Button_SetCheck(hCKMipMap, pParam->batchInfo.newMipMap);
			}
			else
			{
				Button_SetCheck(hCKMipMap, FALSE);
				Button_Enable(hCKMipMap, FALSE);
			}
			Button_SetCheck(hCKReadable, pParam->batchInfo.newReadable);
			Button_SetCheck(hCKReadAllowed, pParam->batchInfo.newReadAllowed);

			if (pParam->importTextureInfo.m_TextureSettings.m_FilterMode > 2)
				ComboBox_SetCurSel(hCBFilterMode, 2);
			else
				ComboBox_SetCurSel(hCBFilterMode, pParam->batchInfo.newFilterMode);

			_stprintf_s(tPrint, TEXT("%u"), pParam->batchInfo.newAnisoLevel);
			Edit_SetText(hEAniso, tPrint);

			//Print an exact representation, using scientific notation if it's shorter.
			_stprintf_s(tPrint, TEXT("%.9g"), pParam->batchInfo.newMipBias);
			Edit_SetText(hEMipBias, tPrint);

			if (pParam->batchInfo.newWrapMode > 1)
				ComboBox_SetCurSel(hCBWrapMode, 1);
			else
				ComboBox_SetCurSel(hCBWrapMode, pParam->batchInfo.newWrapMode);

			if (pParam->batchInfo.newWrapModeU > 3)
				ComboBox_SetCurSel(hCBWrapModeU, 3);
			else
				ComboBox_SetCurSel(hCBWrapModeU, pParam->batchInfo.newWrapModeU);

			if (pParam->batchInfo.newWrapModeV > 3)
				ComboBox_SetCurSel(hCBWrapModeV, 3);
			else
				ComboBox_SetCurSel(hCBWrapModeV, pParam->batchInfo.newWrapModeV);

			_stprintf_s(tPrint, TEXT("0x%02X"), pParam->batchInfo.newLightmapFmt);
			Edit_SetText(hELightMapFmt, tPrint);

			if (pParam->batchInfo.newColorSpace > 1)
				ComboBox_SetCurSel(hCBClSpace, 1);
			else
				ComboBox_SetCurSel(hCBClSpace, pParam->batchInfo.newColorSpace);
		}

		InactivateDialogPairsByIdx(hDlg, pParam->hideDialogElementsList);
		return (INT_PTR)TRUE;
	}
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
		case IDC_BLOAD:
		{
			TextureImportParam* pParam = (TextureImportParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			wchar_t* newTextureFilePathW = nullptr;
			if (SUCCEEDED(ShowFileOpenDialog(hDlg, &newTextureFilePathW, L"*.tga;*.png;*.bmp|Image file:*.*|All types:",
				nullptr, nullptr, nullptr, UABE_FILEDIALOG_EXPIMPASSET_GUID)))
			{
				auto newTextureFilePath = unique_WideToMultiByte(newTextureFilePathW);
				FreeCOMFilePathBuf(&newTextureFilePathW);
				if (pParam->batchInfo.isBatchEntry)
				{
					IAssetsReader* pImageFileReader = Create_AssetsReaderFromFile(newTextureFilePath.get(), true, RWOpenFlags_Immediately);
					if (pImageFileReader != NULL)
					{
						pParam->batchInfo.batchFilenameOverride.assign(newTextureFilePath.get());
						Free_AssetsReader(pImageFileReader);
					}
					else
						MessageBox(hDlg, TEXT("Unable to open the file!"), TEXT("ERROR"), 16);
				}
				else
				{
					std::vector<uint8_t> newTextureData;
					if (LoadTextureFromFile(newTextureFilePath.get(),
						newTextureData,
						pParam->importTextureInfo.m_Width,
						pParam->importTextureInfo.m_Height))
					{
						pParam->importTextureData = std::move(newTextureData);
						pParam->textureDataModified = true;
						HWND hCKMipMap = GetDlgItem(hDlg, IDC_CKMIPMAP);
						if (IsPowerOfTwo(pParam->importTextureInfo.m_Width) &&
							IsPowerOfTwo(pParam->importTextureInfo.m_Height))
							Button_Enable(hCKMipMap, TRUE);
						else
						{
							Button_SetCheck(hCKMipMap, FALSE);
							Button_Enable(hCKMipMap, FALSE);
						}
					}
					else
						MessageBox(hDlg, TEXT("Unable to open or process the file!"), TEXT("ERROR"), 16);
				}
			}
		}
		break;
		case IDOK:
		{
			TextureImportParam* pParam = (TextureImportParam*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			HWND hEName = GetDlgItem(hDlg, IDC_ENAME);
			HWND hCBTexFmt = GetDlgItem(hDlg, IDC_CBTEXFMT);
			HWND hCKMipMap = GetDlgItem(hDlg, IDC_CKMIPMAP);
			HWND hCKReadable = GetDlgItem(hDlg, IDC_CKREADABLE);
			HWND hCKReadAllowed = GetDlgItem(hDlg, IDC_CKREADALLOWED);
			HWND hCBFilterMode = GetDlgItem(hDlg, IDC_CBFILTERMODE);
			HWND hEAniso = GetDlgItem(hDlg, IDC_EANISO);
			HWND hEMipBias = GetDlgItem(hDlg, IDC_EMIPBIAS);
			HWND hCBWrapMode = GetDlgItem(hDlg, IDC_CBWRAPMODE);
			HWND hCBWrapModeU = GetDlgItem(hDlg, IDC_CBWRAPMODEU);
			HWND hCBWrapModeV = GetDlgItem(hDlg, IDC_CBWRAPMODEV);
			HWND hELightMapFmt = GetDlgItem(hDlg, IDC_ELIGHTMAPFMT);
			HWND hCBClSpace = GetDlgItem(hDlg, IDC_CBCLSPACE);


			Edit_GetText(hEName, tPrint, 128);
			{
				auto cNameBuffer = unique_TCHARToMultiByte(tPrint);
				pParam->batchInfo.newName.assign(cNameBuffer.get());
			}

			size_t selection = (unsigned int)ComboBox_GetCurSel(hCBTexFmt);
			if (selection >= SupportedTextureNames_size || selection >= ComboBox_GetCount(hCBTexFmt)) selection = 0;
			else selection = (size_t)ComboBox_GetItemData(hCBTexFmt, selection);
			const TextureNameIDPair* pTexFormatInfo = &SupportedTextureNames[selection];

			pParam->batchInfo.newTextureFormat = pTexFormatInfo->textureType;
			//Special case : TexFmt_BGRA32Old and TexFmt_BGRA32New.
			if ((pTexFormatInfo->textureType == TexFmt_BGRA32New
				  && pParam->asset.pFile->getAssetsFileContext()->getAssetsFile()->header.format < 0x0E))
				pParam->batchInfo.newTextureFormat = TexFmt_BGRA32Old;

			pParam->batchInfo.newMipMap = Button_GetCheck(hCKMipMap) ? true : false;
			pParam->batchInfo.newReadable = Button_GetCheck(hCKReadable) ? true : false;
			pParam->batchInfo.newReadAllowed = Button_GetCheck(hCKReadAllowed) ? true : false;

			TCHAR* endPtr = NULL;

			pParam->batchInfo.newFilterMode = ComboBox_GetCurSel(hCBFilterMode);

			Edit_GetText(hEAniso, tPrint, 128);
			int anisoLevel = (int)_tcstol(tPrint, &endPtr, 0);
			if (endPtr != tPrint)
				pParam->batchInfo.newAnisoLevel = anisoLevel;

			Edit_GetText(hEMipBias, tPrint, 128);
			float mipBias = (float)_tcstod(tPrint, &endPtr);
			if (endPtr != tPrint)
				pParam->batchInfo.newMipBias = mipBias;

			pParam->batchInfo.newWrapMode = ComboBox_GetCurSel(hCBWrapMode);
			pParam->batchInfo.newWrapModeU = ComboBox_GetCurSel(hCBWrapModeU);
			pParam->batchInfo.newWrapModeV = ComboBox_GetCurSel(hCBWrapModeV);

			Edit_GetText(hELightMapFmt, tPrint, 128);
			int lightMapFormat = (int)_tcstol(tPrint, &endPtr, 0);
			if (endPtr != tPrint)
				pParam->batchInfo.newLightmapFmt = lightMapFormat;

			pParam->batchInfo.newColorSpace = ComboBox_GetCurSel(hCBClSpace);

			if (pParam->batchInfo.isBatchEntry)
			{
				if (pParam->batchInfo.newName.compare(pParam->importTextureName)
					|| pParam->batchInfo.newTextureFormat != pParam->importTextureInfo.m_TextureFormat
					|| pParam->batchInfo.newMipMap != (pParam->importTextureInfo.m_MipMap || (pParam->importTextureInfo.m_MipCount > 1))
					|| pParam->batchInfo.newReadable != pParam->importTextureInfo.m_IsReadable
					|| pParam->batchInfo.newReadAllowed != pParam->importTextureInfo.m_ReadAllowed
					|| pParam->batchInfo.newFilterMode != pParam->importTextureInfo.m_TextureSettings.m_FilterMode
					|| pParam->batchInfo.newAnisoLevel != pParam->importTextureInfo.m_TextureSettings.m_Aniso
					|| pParam->batchInfo.newMipBias != pParam->importTextureInfo.m_TextureSettings.m_MipBias
					|| pParam->batchInfo.newWrapMode != pParam->importTextureInfo.m_TextureSettings.m_WrapMode
					|| pParam->batchInfo.newWrapModeU != pParam->importTextureInfo.m_TextureSettings.m_WrapU
					|| pParam->batchInfo.newWrapModeV != pParam->importTextureInfo.m_TextureSettings.m_WrapV
					|| pParam->batchInfo.newLightmapFmt != pParam->importTextureInfo.m_LightmapFormat
					|| pParam->batchInfo.newColorSpace != pParam->importTextureInfo.m_ColorSpace)
				{
					pParam->batchInfo.hasNewSettings = true;
				}
			}
			/*if (pParam->batchInfo.newTextureFormat != pParam->importTextureInfo.m_TextureFormat
				|| pParam->batchInfo.batchFilenameOverride.size() > 0)*/
			{
				DialogBoxParam(g_hModule, MAKEINTRESOURCE(IDD_COMPQUALITY), hDlg, CompressQualityDlg, (LPARAM)pParam);
			}
		}
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void GetHideDialogElementsList(AssetTypeTemplateField* pTemplateBase, std::vector<size_t>& elementIndices)
{
	if (!pTemplateBase->SearchChild("m_ReadAllowed"))
		elementIndices.push_back(GetImportDialogPairByID(IDC_SREADALLOWED));
	if (!pTemplateBase->SearchChild("m_ColorSpace"))
		elementIndices.push_back(GetImportDialogPairByID(IDC_SCLSPACE));
	AssetTypeTemplateField* pTextureSettings;
	if ((pTextureSettings = pTemplateBase->SearchChild("m_TextureSettings")))
	{
		if (!pTextureSettings->SearchChild("m_WrapMode"))
			elementIndices.push_back(GetImportDialogPairByID(IDC_SWRAPMODE));
		if (!pTextureSettings->SearchChild("m_WrapU"))
			elementIndices.push_back(GetImportDialogPairByID(IDC_SWRAPMODEU));
		if (!pTextureSettings->SearchChild("m_WrapV"))
			elementIndices.push_back(GetImportDialogPairByID(IDC_SWRAPMODEV));
	}
}
void InitializeImportParam(TextureImportParam* pParam, AppContext& appContext)
{
	pParam->batchInfo.hasNewSettings = false;
	AssetIdentifier& asset = pParam->asset;

	IAssetsReader_ptr pAssetReader = asset.makeReader(appContext);
	if (pAssetReader == nullptr)
		return;
	QWORD assetSize = 0;
	if (!pAssetReader->Seek(AssetsSeek_End, 0) || !pAssetReader->Tell(assetSize) || !pAssetReader->Seek(AssetsSeek_Begin, 0))
		return;

	AssetTypeTemplateField templateBase;
	if (!asset.pFile->MakeTemplateField(&templateBase, appContext, asset.getClassID(), asset.getMonoScriptID(), &asset))
		return;
	AssetTypeTemplateField* pTemplateBase = &templateBase;

	AssetTypeInstance assetInstance(1, &pTemplateBase, assetSize, pAssetReader.get(), asset.isBigEndian());
	AssetTypeValueField* pBaseField = assetInstance.GetBaseField();
	if (pBaseField == nullptr || pBaseField->IsDummy())
		return;
	pAssetReader.reset();

	GetHideDialogElementsList(pTemplateBase, pParam->hideDialogElementsList);

	TextureFile textureFile;
	if (!ReadTextureFile(&textureFile, pBaseField))
		return;

	//Retrieve texture format version.
	SupportsTextureFormat(asset.pFile->getAssetsFileContext()->getAssetsFile(),
		(TextureFormat)textureFile.m_TextureFormat,
		textureFile.extra.textureFormatVersion);

	pParam->importTextureName.assign(textureFile.m_Name);
	pParam->importTextureInfo = textureFile;
	//pParam->importTextureInfo.m_Name.clear();

	pParam->assignCompressedTextureData(std::vector<uint8_t>(pParam->importTextureInfo._pictureDataSize));
	memcpy(pParam->importTextureInfo.pPictureData, textureFile.pPictureData, pParam->importTextureInfo._pictureDataSize);

	pParam->assetWasTextureFile = true;
	pParam->batchInfo.newName.assign(pParam->importTextureName);
	pParam->batchInfo.newTextureFormat = pParam->importTextureInfo.m_TextureFormat;
	pParam->batchInfo.newMipMap = pParam->importTextureInfo.m_MipMap || (pParam->importTextureInfo.m_MipCount > 1);
	pParam->batchInfo.newReadable = pParam->importTextureInfo.m_IsReadable;
	pParam->batchInfo.newReadAllowed = pParam->importTextureInfo.m_ReadAllowed;
	pParam->batchInfo.newFilterMode = pParam->importTextureInfo.m_TextureSettings.m_FilterMode;
	pParam->batchInfo.newAnisoLevel = pParam->importTextureInfo.m_TextureSettings.m_Aniso;
	pParam->batchInfo.newMipBias = pParam->importTextureInfo.m_TextureSettings.m_MipBias;
	pParam->batchInfo.newWrapMode = pParam->importTextureInfo.m_TextureSettings.m_WrapMode;
	pParam->batchInfo.newWrapModeU = pParam->importTextureInfo.m_TextureSettings.m_WrapU;
	pParam->batchInfo.newWrapModeV = pParam->importTextureInfo.m_TextureSettings.m_WrapV;
	pParam->batchInfo.newLightmapFmt = pParam->importTextureInfo.m_LightmapFormat;
	pParam->batchInfo.newColorSpace = pParam->importTextureInfo.m_ColorSpace;
}
void OpenImportDialog(HWND hParentWnd, TextureImportParam* pParam)
{
	DialogBoxParam(g_hModule, MAKEINTRESOURCE(IDD_IMPORT), hParentWnd, ImportDlg, (LPARAM)pParam);
}

class TextureBatchImportDialogDesc : public CGenericBatchImportDialogDesc, public IWin32AssetBatchImportDesc
{
	Win32AppContext& appContext;
public:
	std::vector<std::unique_ptr<TextureImportParam>> importParameters;
public:
	inline TextureBatchImportDialogDesc(Win32AppContext& appContext, std::vector<AssetUtilDesc> _elements, const std::string& extensionRegex)
		: CGenericBatchImportDialogDesc(std::move(_elements), extensionRegex),
		appContext(appContext)
	{
		importParameters.resize(getElements().size());
	}
	bool hasAnyChanges()
	{
		return std::any_of(importParameters.begin(), importParameters.end(), [](const auto& x) {return x != nullptr; })
			|| std::any_of(importFilePathOverrides.begin(), importFilePathOverrides.end(), [](const auto& x) {return !x.empty(); });
	}

	bool ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow)
	{
		if (matchIndex == (size_t)-1)
			return true;
		if (matchIndex >= importParameters.size() || matchIndex >= getElements().size() || matchIndex >= importFilePathOverrides.size())
			return false;
		std::unique_ptr<TextureImportParam>& pImportParam = importParameters[matchIndex];
		if (pImportParam == nullptr)
		{
			pImportParam.reset(new TextureImportParam(getElements()[matchIndex].asset, false, true));
			InitializeImportParam(pImportParam.get(), appContext);
		}

		pImportParam->batchInfo.batchFilenameOverride.clear();

		OpenImportDialog(hParentWindow, pImportParam.get());

		importFilePathOverrides[matchIndex].assign(pImportParam->batchInfo.batchFilenameOverride);
		if (!pImportParam->batchInfo.hasNewSettings && !pImportParam->textureDataModified && !pImportParam->qualitySelected)
			pImportParam.reset();
		return true;
		return false; //TODO
	}
};

class TextureEditTask : public ITask
{
	AppContext& appContext;
	std::unique_ptr<TextureBatchImportDialogDesc> pDialogDesc;
	std::string taskName;
	TypeTemplateCache templateCache;
public:
	TextureEditTask(AppContext& appContext, std::unique_ptr<TextureBatchImportDialogDesc> _pDialogDesc)
		: appContext(appContext), pDialogDesc(std::move(_pDialogDesc)), taskName("Edit Textures")
	{}
	const std::string& getName()
	{
		return taskName;
	}
	TaskResult execute(TaskProgressManager& progressManager)
	{
		unsigned int progressRange = static_cast<unsigned int>(std::min<size_t>(pDialogDesc->importParameters.size(), 10000));
		size_t assetsPerProgressStep = pDialogDesc->importParameters.size() / progressRange;
		constexpr size_t assetsPerDescUpdate = 8;
		progressManager.setCancelable();
		progressManager.setProgress(0, progressRange);
		auto lastDescTime = std::chrono::high_resolution_clock::now();
		bool encounteredErrors = false;
		for (size_t i = 0; i < pDialogDesc->importParameters.size(); ++i)
		{
			if (progressManager.isCanceled())
				return TaskResult_Canceled;
			if ((i % assetsPerProgressStep) == 0)
				progressManager.setProgress((unsigned int)(i / assetsPerProgressStep), progressRange);
			auto curTime = std::chrono::high_resolution_clock::now();
			if (i == 0 || std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastDescTime).count() >= 500)
			{
				progressManager.setProgressDesc(std::format("Importing {}/{}", (i + 1), pDialogDesc->importParameters.size()));
				lastDescTime = curTime;
			}
			const AssetIdentifier& asset = pDialogDesc->getElements()[i].asset;
			std::unique_ptr<TextureImportParam> &pImportParam = pDialogDesc->importParameters[i];
			if (pImportParam == nullptr)
			{
				pImportParam.reset(new TextureImportParam(asset, false, true));
				InitializeImportParam(pImportParam.get(), appContext);
			}

			pImportParam->batchInfo.batchFilenameOverride = pDialogDesc->getImportFilePath(i);
			try {
				FinalizeTextureEdit(pImportParam.get(), progressManager);
			}
			catch (AssetUtilError err) {
				progressManager.logMessage(std::format(
					"Error importing an asset (File ID {0}, Path ID {1}): {2}",
					asset.fileID, (int64_t)asset.pathID, err.what()));
				encounteredErrors = true;
			}
			pImportParam.reset();
		}
		return (TaskResult)(encounteredErrors ? -2 : 0);

	}

	//Set pParam->batchInfo.batchFilenameOverride to the import file name.
	void FinalizeTextureEdit(TextureImportParam* pParam, TaskProgressManager &progressManager)
	{
		size_t textureTypeIndex;
		if (pParam->batchInfo.newTextureFormat == TexFmt_BGRA32Old)
			textureTypeIndex = GetTextureNameIDPair(TexFmt_BGRA32New, pParam->importTextureInfo.extra.textureFormatVersion);
		else
			textureTypeIndex = GetTextureNameIDPair(pParam->batchInfo.newTextureFormat, pParam->importTextureInfo.extra.textureFormatVersion);
		if (textureTypeIndex == (size_t)-1)
			throw AssetUtilError("Unable to determine the texture format.");

		const TextureNameIDPair* pTexFormatInfo = &SupportedTextureNames[textureTypeIndex];
		unsigned int newWidth = pParam->importTextureInfo.m_Width;
		unsigned int newHeight = pParam->importTextureInfo.m_Height;
		std::vector<uint8_t> &newTextureData = pParam->importTextureData;
		if (!pParam->batchInfo.batchFilenameOverride.empty())
		{
			//Load the given texture file.
			LoadTextureFromFile(pParam->batchInfo.batchFilenameOverride.c_str(), newTextureData, newWidth, newHeight);
			pParam->importTextureInfo.pPictureData = NULL;
			pParam->textureDataModified = true;
		}

		bool oldMipMap = pParam->importTextureInfo.m_MipMap || (pParam->importTextureInfo.m_MipCount > 1);
		bool newMipMap = pParam->batchInfo.newMipMap &&
			IsPowerOfTwo(newWidth) &&
			IsPowerOfTwo(newHeight);

		bool generateTextureData = pParam->textureDataModified
			|| (pParam->importTextureInfo.m_TextureFormat != pParam->batchInfo.newTextureFormat);
		//|| (newMipMap != oldMipMap));
		if (newMipMap && !oldMipMap)
			generateTextureData = true;

		size_t newTextureDataSize = 0;
		unsigned int mipCount = pParam->importTextureInfo.m_MipCount;
		if (generateTextureData)
		{
			unsigned int curWidth = newWidth;
			unsigned int curHeight = newHeight;

			mipCount = 0;
			do
			{
				size_t curTextureDataSize = GetTextureDataSize(pTexFormatInfo, curWidth, curHeight);

				newTextureDataSize += curTextureDataSize;
				mipCount++;
				if ((curWidth >>= 1) >= 1 || (curHeight >>= 1) >= 1)
				{
					if (curWidth == 0)
						curWidth = 1;
					else if (curHeight == 0)
						curHeight = 1;
				}
				else
					break;
			} while (newMipMap);

			//Decode the current texture data if necessary.
			if (newTextureDataSize > 0 && newTextureDataSize <= 0xFFFFFFFFULL
				&& (pParam->importTextureInfo.pPictureData != NULL
					|| (pParam->importTextureData.empty()
						&& pParam->importTextureInfo._pictureDataSize == 0
						&& pParam->importTextureInfo.m_StreamData.size > 0)))
			{
				if (pParam->importTextureInfo.pPictureData != pParam->importTextureData.data()
					|| pParam->importTextureInfo._pictureDataSize != pParam->importTextureData.size())
					throw std::runtime_error("FinalizeTextureEdit: Unexpected texture data reference.");
				std::vector<uint8_t> compressedTextureData;
				if (pParam->importTextureInfo._pictureDataSize == 0
					&& (pParam->importTextureInfo.m_Width * pParam->importTextureInfo.m_Height) > 0 &&
					pParam->importTextureInfo.m_StreamData.size > 0)
				{
					//Load the texture from the referred resource.
					pParam->importTextureInfo.pPictureData = NULL;
					auto pResourcesFile = FindResourcesFile(appContext, pParam->importTextureInfo.m_StreamData.path, pParam->asset, progressManager);
					//Non-null guaranteed by FindResourcesFile (AssetUtilError thrown otherwise).

					std::shared_ptr<IAssetsReader> pStreamReader = pResourcesFile->getResource(pResourcesFile,
						pParam->importTextureInfo.m_StreamData.offset,
						pParam->importTextureInfo.m_StreamData.size);
					if (pStreamReader == nullptr)
						throw AssetUtilError("Unable to locate the texture resource.");

					compressedTextureData.resize(pParam->importTextureInfo.m_StreamData.size);
					if (pStreamReader->Read(0, pParam->importTextureInfo.m_StreamData.size, compressedTextureData.data())
						!= pParam->importTextureInfo.m_StreamData.size)
						throw AssetUtilError("Unable to read data from the texture resource.");
				}
				else
					compressedTextureData = pParam->importTextureData;
				if (!compressedTextureData.empty())
				{
					//Decode the texture data.
					newTextureData.resize((size_t)pParam->importTextureInfo.m_Width * pParam->importTextureInfo.m_Height * 4);

					pParam->importTextureInfo.pPictureData = compressedTextureData.data();
					pParam->importTextureInfo._pictureDataSize = pParam->importTextureInfo.m_StreamData.size;

					//Retrieve the texture format version.
					SupportsTextureFormat(pParam->asset.pFile->getAssetsFileContext()->getAssetsFile(),
						(TextureFormat)0,
						pParam->importTextureInfo.extra.textureFormatVersion);
					if (!GetTextureData(&pParam->importTextureInfo, newTextureData.data()))
						throw AssetUtilError("Unable to decode the texture.");
				}
				else
					throw AssetUtilError("Unable to find the compressed texture data.");
				pParam->importTextureInfo.pPictureData = NULL;
				pParam->importTextureInfo.m_StreamData.offset = pParam->importTextureInfo.m_StreamData.size = 0;
				pParam->importTextureInfo.m_StreamData.path.clear();
			}
		}

		
		std::unique_ptr<IAssetsWriterToMemory> pWriter(Create_AssetsWriterToMemory());
		AssetTypeTemplateField &templateBase = templateCache.getTemplateField(appContext, pParam->asset);

		pParam->importTextureName = pParam->batchInfo.newName;
		pParam->importTextureInfo.m_TextureFormat = pParam->batchInfo.newTextureFormat;
		pParam->importTextureInfo.m_MipMap = pParam->batchInfo.newMipMap;
		pParam->importTextureInfo.m_MipCount = mipCount;
		pParam->importTextureInfo.m_IsReadable = pParam->batchInfo.newReadable;
		pParam->importTextureInfo.m_ReadAllowed = pParam->batchInfo.newReadAllowed;
		pParam->importTextureInfo.m_TextureSettings.m_FilterMode = pParam->batchInfo.newFilterMode;
		pParam->importTextureInfo.m_TextureSettings.m_Aniso = pParam->batchInfo.newAnisoLevel;
		pParam->importTextureInfo.m_TextureSettings.m_MipBias = pParam->batchInfo.newMipBias;
		pParam->importTextureInfo.m_TextureSettings.m_WrapMode = pParam->batchInfo.newWrapMode;
		pParam->importTextureInfo.m_TextureSettings.m_WrapU = pParam->batchInfo.newWrapModeU;
		pParam->importTextureInfo.m_TextureSettings.m_WrapV = pParam->batchInfo.newWrapModeV;
		pParam->importTextureInfo.m_LightmapFormat = pParam->batchInfo.newLightmapFmt;
		pParam->importTextureInfo.m_ColorSpace = pParam->batchInfo.newColorSpace;

		pParam->importTextureInfo.m_Width = newWidth;
		pParam->importTextureInfo.m_Height = newHeight;

		std::vector<uint8_t> recompressedTextureDataBuf;
		bool dataFormatValid = true;
		if (generateTextureData && !newTextureData.empty())
		{
			//Compress the texture data.
			recompressedTextureDataBuf.resize((newTextureDataSize + 3) & (~3), 0);
			pParam->importTextureInfo.pPictureData = recompressedTextureDataBuf.data();
			pParam->importTextureInfo._pictureDataSize = (newTextureDataSize + 3) & (~3);
			pParam->importTextureInfo.m_StreamData.path.clear();
			pParam->importTextureInfo.m_StreamData.offset = 0;
			pParam->importTextureInfo.m_StreamData.size = 0;
			if (!pParam->qualitySelected)
			{
				pParam->qualitySelection = ePVRTCFastest;
				std::vector<const wchar_t*> presetNames;
				size_t defaultPresetIdx = GetQualityPresetList(pParam->importTextureInfo.m_TextureFormat, presetNames);
				if (defaultPresetIdx < presetNames.size() && presetNames.size() < 0x7FFFFFFF)
				{
					pParam->qualitySelection = GetQualityFromPresetListIdx(pParam->importTextureInfo.m_TextureFormat, defaultPresetIdx);
				}
			}
			//update the texture format version
			SupportsTextureFormat(pParam->asset.pFile->getAssetsFileContext()->getAssetsFile(),
				(TextureFormat)0,
				pParam->importTextureInfo.extra.textureFormatVersion);
			dataFormatValid = MakeTextureData(&pParam->importTextureInfo, newTextureData.data(), pParam->qualitySelection);
		}

		if (!dataFormatValid
			|| (pParam->importTextureInfo.pPictureData == NULL && pParam->importTextureInfo._pictureDataSize > 0))
			throw AssetUtilError("Unable to convert the texture data.");
		//Replace the texture.
		pParam->importTextureInfo.m_Name = pParam->importTextureName;

		{
			std::vector<std::unique_ptr<uint8_t[]>> textureValueFieldMemory;
			AssetTypeValueField* pTextureBase = CreateEmptyValueFieldFromTemplate(&templateBase, textureValueFieldMemory);
			if (!WriteTextureFile(&pParam->importTextureInfo, pTextureBase, textureValueFieldMemory))
				throw AssetUtilError("Unable to serialize the texture file. Is the class database invalid?");

			QWORD newTextureFileSize = pTextureBase->Write(pWriter.get(), 0, pParam->asset.isBigEndian());
			if (newTextureFileSize == 0)
				throw AssetUtilError("Unable to write the texture file. Is the class database invalid?");
		}

		QWORD writerPos = 0; size_t writerSize = 0;
		void* pWriterBuf = NULL;
		pWriter->GetBuffer(pWriterBuf, writerSize);
		pWriter->Tell(writerPos);

		std::shared_ptr<AssetsEntryReplacer> pReplacer(MakeAssetModifierFromMemory(0, pParam->asset.pathID,
			pParam->asset.getClassID(), pParam->asset.getMonoScriptID(),
			pWriterBuf, writerSize, Free_AssetsWriterToMemory_DynBuf));
		if (pReplacer == nullptr)
			throw AssetUtilError("Unexpected runtime error.");
		pWriter->SetFreeBuffer(false);
		pParam->asset.pFile->addReplacer(pReplacer, appContext);
	}
};

class TextureEditModifyDialog : public AssetModifyDialog
{
	CBatchImportDialog batchImportDialog;
	std::unique_ptr<TextureBatchImportDialogDesc> pDialogDesc;
	Win32AppContext &appContext;
	AssetListDialog &listDialog;
public:
	TextureEditModifyDialog(std::unique_ptr<TextureBatchImportDialogDesc> _pDialogDesc, std::string _basePath,
		Win32AppContext &appContext, AssetListDialog& listDialog)
		: batchImportDialog(appContext.getMainWindow().getHInstance(), _pDialogDesc.get(), _pDialogDesc.get(), std::move(_basePath)),
		  pDialogDesc(std::move(_pDialogDesc)),
		  appContext(appContext), listDialog(listDialog)
	{
		batchImportDialog.SetCloseCallback([this,&listDialog,&appContext](bool apply)
			{
				if (apply && pDialogDesc != nullptr)
				{
					auto pTask = std::make_shared<TextureEditTask>(appContext, std::move(pDialogDesc));
					appContext.taskManager.enqueue(pTask);
				}
				batchImportDialog.SetCloseCallback(nullptr);
				listDialog.removeModifyDialog(this);
			}
		);
	}
	virtual ~TextureEditModifyDialog() {}
	//Called when the user requests to close the tab.
	//Returns true if there are unsaved changes, false otherwise.
	//If the function will return true and applyable is not null,
	// *applyable will be set to true iff applyNow() is assumed to succeed without further interaction
	// (e.g. all fields in the dialog have a valid value, ...).
	//The caller uses this info to decide whether and how it should display a confirmation dialog before proceeding.
	virtual bool hasUnappliedChanges(bool* applyable)
	{
		if (applyable) *applyable = false;
		return true;
	}
	//Called when the user requests to apply the changes (e.g. selecting Apply, Save or Save All in the menu).
	//Returns whether the changes have been applied;
	// if true, the caller may continue closing the AssetModifyDialog.
	// if false, the caller shall stop closing the AssetModifyDialog.
	//Note: applyChanges() is expected to notify the user about errors (e.g. via MessageBox).
	virtual bool applyChanges()
	{
		return false;
	}
	virtual std::string getTabName()
	{
		return "Edit textures";
	}
	virtual HWND getWindowHandle()
	{
		return batchImportDialog.getWindowHandle();
	}
	//Called for unhandled WM_COMMAND messages. Returns true if this dialog has handled the request, false otherwise.
	virtual bool onCommand(WPARAM wParam, LPARAM lParam)
	{
		return false;
	}
	//message : currently only WM_KEYDOWN; keyCode : VK_F3 for instance
	virtual void onHotkey(ULONG message, DWORD keyCode)
	{
		return;
	}
	//Called when the dialog is to be shown. The parent window will not change before the next onHide call.
	virtual void onShow(HWND hParentWnd)
	{
		batchImportDialog.ShowModeless(hParentWnd);
	}
	//Called when the dialog is to be hidden, either because of a tab switch or while closing the tab.
	virtual void onHide()
	{
		batchImportDialog.Hide();
	}
	//Called when the tab is about to be destroyed.
	//Once this function is called, AssetListDialog::removeModifyDialog must not be used for this dialog.
	virtual void onDestroy()
	{
		batchImportDialog.SetCloseCallback(nullptr);
	}
};
class Win32TextureEditProvider : public IAssetListTabOptionProvider
{
public:
	class Runner : public IOptionRunner
	{
		Win32AppContext& appContext;
		AssetListDialog& listDialog;
		std::vector<AssetUtilDesc> selection;
	public:
		Runner(Win32AppContext& appContext, AssetListDialog& listDialog, std::vector<AssetUtilDesc> _selection)
			: appContext(appContext), listDialog(listDialog), selection(std::move(_selection))
		{}
		void operator()()
		{
			auto pDialogDesc = std::make_unique<TextureBatchImportDialogDesc>(appContext, std::move(selection), "\\.(?:tga|png)");
			if (pDialogDesc->getElements().size() > 1)
			{
				WCHAR* folderPathW = nullptr;
				if (!ShowFolderSelectDialog(appContext.getMainWindow().getWindow(), &folderPathW, L"Select an input directory", UABE_FILEDIALOG_EXPIMPASSET_GUID))
					return;
				auto folderPath8 = unique_WideToMultiByte(folderPathW);
				FreeCOMFilePathBuf(&folderPathW);

				auto pModifyDialog = std::make_shared<TextureEditModifyDialog>(std::move(pDialogDesc), folderPath8.get(), appContext, listDialog);
				listDialog.addModifyDialog(pModifyDialog);
				
			}
			else
			{
				if (pDialogDesc->ShowAssetSettings(0, appContext.getMainWindow().getWindow())
					&& pDialogDesc->hasAnyChanges())
				{
					auto pTask = std::make_shared<TextureEditTask>(appContext, std::move(pDialogDesc));
					appContext.taskManager.enqueue(pTask);
				}
			}
		}
	};
	EAssetOptionType getType()
	{
		return EAssetOptionType::Import;
	}
	std::unique_ptr<IOptionRunner> prepareForSelection(
		Win32AppContext& appContext, AssetListDialog& listDialog,
		std::vector<AssetUtilDesc> selection,
		std::string& optionName)
	{
		if (!PluginSupportsElements(selection))
			return nullptr;
		optionName = "Edit";
		return std::make_unique<Runner>(appContext, listDialog, std::move(selection));
	}
};

class Win32TexturePluginDesc : public GenericTexturePluginDesc
{
public:
	Win32TexturePluginDesc()
		: GenericTexturePluginDesc()
	{
		pProviders.push_back(std::make_shared<Win32TextureEditProvider>());
	}
};

IPluginDesc* GetUABEPluginDesc1(size_t sizeof_AppContext, size_t sizeof_BundleFileContextInfo)
{
	if (sizeof_AppContext != sizeof(AppContext) || sizeof_BundleFileContextInfo != sizeof(BundleFileContextInfo))
	{
		assert(false);
		return nullptr;
	}
	return new Win32TexturePluginDesc();
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		g_hModule = hModule;
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
