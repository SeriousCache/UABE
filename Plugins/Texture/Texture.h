#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <malloc.h>
#include "../UABE_Generic/PluginManager.h"
#include "../UABE_Generic/FileContextInfo.h"
#include "../UABE_Generic/AppContext.h"
#include "../UABE_Generic/AssetIterator.h"

enum class EExportFormat
{
	PNG,
	TGA
};

bool LoadTextureFromFile(const char* filePath, std::vector<uint8_t> &newTextureData, unsigned int& newWidth, unsigned int& newHeight);
inline void FreeTextureFromFile(uint8_t* textureData)
{
	if (textureData) free(textureData);
}

bool PluginSupportsElements(std::vector<AssetUtilDesc> &elements);
class GenericTexturePluginDesc : public IPluginDesc
{
protected:
	std::vector<std::shared_ptr<IOptionProvider>> pProviders;
public:
	GenericTexturePluginDesc();
	std::string getName();
	std::string getAuthor();
	std::string getDescriptionText();
	//The IPluginDesc object should keep a reference to the returned options, as the caller may keep only std::weak_ptrs.
	//Note: May be called early, e.g. before program UI initialization.
	std::vector<std::shared_ptr<IOptionProvider>> getPluginOptions(class AppContext& appContext);
};
