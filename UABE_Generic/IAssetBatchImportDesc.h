#pragma once
#include <stdint.h>
#include <vector>
#include <string>

#ifndef OUT
#define OUT
#define IN
#endif

//Class implemented by plugins that use the AppContext::ShowAssetBatchImportDialog function. 
//All const char* and std strings are UTF-8. const char* strings returned by the plugin must not be freed before the dialog has closed.
class IAssetBatchImportDesc
{
public:
	class AssetDesc
	{
	public:
		const char *description;
		const char* assetsFileName;
		long long int pathID;
	};
	//Returns a list of asset descriptions to show to the user. The indices into this list will be used for matchIndex.
	virtual bool GetImportableAssetDescs(OUT std::vector<AssetDesc>& descList) = 0;

	//Returns one or multiple regex string(s) that match(es) any potentially importable file in a directory, including those not to be imported.
	//The batch import dialog implementation uses std::regex (ECMAScript) and matches the full name. See https://www.regular-expressions.info/stdregex.html
	virtual bool GetFilenameMatchStrings(OUT std::vector<const char*>& regexList, OUT bool& checkSubDirs) = 0;

	//The return value specifies whether the file name matches any of the assets to import. If a match is found, its index is returned through matchIndex.
	//capturingGroups contains the contents of the regex capturing groups matched in filename.
	virtual bool GetFilenameMatchInfo(IN const char *filename, IN std::vector<const char*>& capturingGroups, OUT size_t& matchIndex) = 0;

	//Sets the full file path for an asset. filepath is NULL for assets where no matching file was found.
	virtual void SetInputFilepath(IN size_t matchIndex, IN const char* filepath) = 0;

	//Retrieves a potential file name override, returning true only if an override exists. Called by the dialog handler after ShowAssetSettings.
	virtual bool HasFilenameOverride(IN size_t matchIndex, OUT std::string& filenameOverride, OUT bool& relativeToBasePath) = 0;
};