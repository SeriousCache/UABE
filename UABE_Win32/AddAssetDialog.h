#pragma once
#include "Win32AppContext.h"
#include "AssetListDialog.h"


class AddAssetDialog
{
	struct ScriptIdxDescriptor
	{
		long long int monoScriptPathID;
		unsigned int monoScriptFileIDRel;
		uint16_t monoClassID; bool isNewClassID;
		Hash128 propertiesHash; //Assuming properties hash == 0 => need to calculate
		std::string scriptDescText;
		bool extendedTypeInfoPresent; //Set to true if the full type information for monoClassID is already present.
	};
	
	std::vector<struct ScriptIdxDescriptor> scriptDescriptors;
	Win32AppContext &appContext;
public:
	inline AddAssetDialog(Win32AppContext &appContext)
		: appContext(appContext)
	{}
	void open();
private:
	static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	static int GetTextExtent(HWND hComboBox, const TCHAR *text);

	unsigned int findRelFileID(AssetsFileContextInfo *pSourceFile, unsigned int targetFileID);
	void EnumScriptIndices_HandleMonoScript(AssetsFileContextInfo *pSourceFile, unsigned int scriptFileRefIdx,
		long long int scriptPathID, std::vector<ScriptIdxDescriptor> &descriptors);
	void EnumScriptIndices_HandleMonoBehaviour(AssetsFileContextInfo *pSourceFile,
		AssetIdentifier &behaviourAsset,
		AssetTypeTemplateField *pBehaviourBase, std::vector<ScriptIdxDescriptor> &descriptors);

	bool EnumScriptIndices(AssetsFileContextInfo *pFile, std::vector<ScriptIdxDescriptor> &descriptors);

	pathid_t getFreePathID(AssetsFileContextInfo *pFile);
};
