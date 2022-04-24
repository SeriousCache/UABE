#pragma once
#include "defines.h"
#include <string>
#include <cstdlib>
#include <cstdint>
//Represents the two 'major' Unity release numbers,
// as these are usually the relevant ones for version comparisons.
struct EngineVersion
{
	unsigned int year=0; //3,4,5,2017,...
	unsigned int release=0; //Usually numbered 1,2,...
	ASSETSTOOLS_API static EngineVersion parse(std::string versionString);
};
