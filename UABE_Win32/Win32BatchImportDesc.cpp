#include "stdafx.h"
#include "Win32BatchImportDesc.h"
#include "Win32AppContext.h"
#include "FileDialog.h"
#include "../libStringConverter/convert.h"

bool CWin32GenericBatchImportDialogDesc::ShowAssetSettings(IN size_t matchIndex, IN HWND hParentWindow)
{
	if (matchIndex == (size_t)-1)
		return true;
	if (matchIndex >= importFilePathOverrides.size())
		return false;

	importFilePathOverrides[matchIndex].clear();

	auto pFileTypeFilterW = unique_MultiByteToWide(fileTypeFilter.c_str());
	wchar_t* filePathBufW = nullptr;
	HRESULT hr = ShowFileOpenDialog(hParentWindow, &filePathBufW, pFileTypeFilterW.get(),
		nullptr, nullptr, nullptr,
		UABE_FILEDIALOG_EXPIMPASSET_GUID);
	if (SUCCEEDED(hr))
	{
		auto pFilePath8 = unique_WideToMultiByte(filePathBufW);
		importFilePathOverrides[matchIndex].assign(pFilePath8.get());
		FreeCOMFilePathBuf(&filePathBufW);
	}

	return true;
}