#include "EngineVersion.h"

EngineVersion EngineVersion::parse(std::string versionString)
{
	unsigned int unityYear = 0, unityRelease = 0;
	{
		char toIntTemp[7] = {};
		if (versionString.size() > 6 && versionString[4] == '.' && versionString[6] == '.')
		{
			*(uint32_t*)&toIntTemp[0] = *(uint32_t*)&versionString[0];
			toIntTemp[5] = versionString[5];
		}
		else if (versionString.size() > 3 && versionString[1] == '.' && versionString[3] == '.')
		{
			toIntTemp[0] = versionString[0];
			toIntTemp[5] = versionString[2];
		}
		unityYear = (unsigned int)strtol(toIntTemp, NULL, 10);
		unityRelease = (unsigned int)strtol(&toIntTemp[5], NULL, 10);
	}
	return EngineVersion{ unityYear, unityRelease };
}
