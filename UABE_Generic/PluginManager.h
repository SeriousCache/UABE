#pragma once
#include "api.h"
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include <concepts>
#include <list>

//TODO: Incomplete (just a skeleton).

class IOptionProvider
{
public:
	 UABE_Generic_API virtual ~IOptionProvider();
};

class IPluginDesc
{
public:
	UABE_Generic_API virtual ~IPluginDesc();
	UABE_Generic_API virtual std::string getName() = 0;
	UABE_Generic_API virtual std::string getAuthor() = 0;
	//New line: \n
	UABE_Generic_API virtual std::string getDescriptionText() = 0;
	//The IPluginDesc object should keep a reference to the returned options, as the caller may keep only std::weak_ptrs.
	//Note: May be called early, e.g. before program UI initialization.
	UABE_Generic_API virtual std::vector<std::shared_ptr<IOptionProvider>> getPluginOptions(class AppContext& appContext) = 0;
};

class PluginMapping
{
public:
	std::list<std::weak_ptr<IOptionProvider>> options;
	std::list<std::unique_ptr<IPluginDesc>> descriptions;

	//Finds the next plugin option provider of a specific type.
	//curOption: Last returned iterator of a getNextOptionProvider, or options.cbegin().
	//pProvider: Output variable for the found provider. Set to nullptr if no provider was found.
	// -> The caller can (and probably should) stop iterating if pProvider == nullptr.
	//Returns an iterator in options that points to the next element to check, or options.cend().
	template<class OptionType>
	requires std::derived_from<OptionType, IOptionProvider>
	inline decltype(options)::const_iterator getNextOptionProvider(
		decltype(options)::const_iterator curOption,
		std::shared_ptr<OptionType>& pProvider) const
	{
		pProvider.reset();
		for (; curOption != options.end(); ++curOption)
		{
			pProvider = std::dynamic_pointer_cast<OptionType>(curOption->lock());
			if (pProvider != nullptr)
			{
				++curOption;
				break;
			}
		}
		return curOption;
	}
};

//Option runner base class for option providers.
class IOptionRunner
{
public:
	UABE_Generic_API virtual ~IOptionRunner();
	//Perform the operation.
	//Tasks blocking the caller should be avoided, as it would make the rest of the application unresponsive.
	// An exception to this are modal Win32 dialogs.
	UABE_Generic_API virtual void operator()() = 0;
};

#include <functional>
class OptionRunnerByFn : public IOptionRunner
{
	std::function<void(void)> fn;
public:
	inline OptionRunnerByFn(std::function<void(void)> _fn)
		: fn(std::move(_fn))
	{}
	inline void operator()() { fn(); }
};

#include "AppContext.h"
#include "AssetIterator.h"
#include "AssetPluginUtil.h"

enum class EAssetOptionType
{
	Import, //Import or edit an asset, may also export
	Export //Export an asset
};

//Provider for asset options: Apply an operation on a selection of assets.
class IAssetOptionProviderGeneric : public IOptionProvider
{
public:
	UABE_Generic_API IAssetOptionProviderGeneric();
	UABE_Generic_API virtual EAssetOptionType getType()=0;
	//Determines whether this option provider applies to a given selection.
	//Returns a runner object for the selection, or a null pointer otherwise.
	//No operation should be applied unless the runner object's operator() is invoked.
	//The plugin should set "optionName" to a short description on success.
	UABE_Generic_API virtual std::unique_ptr<IOptionRunner> prepareForSelection(
		class AppContext& appContext, 
		std::vector<struct AssetUtilDesc> selection, 
		std::string &optionName)=0;
};
