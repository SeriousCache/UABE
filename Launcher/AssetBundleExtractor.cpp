#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <tchar.h>
#include "../libStringConverter/convert.h"

#include <string>
#include <cstring>
#include <vector>

static std::string getModuleBaseDir(HINSTANCE hInstance)
{
	std::string baseDir;
	std::vector<TCHAR> baseDirT;
	size_t ownPathLen = MAX_PATH;
	while (true)
	{
		baseDirT.resize(ownPathLen + 1, 0);
		SetLastError(0);
		DWORD result = GetModuleFileName(hInstance, baseDirT.data(), (DWORD)ownPathLen);
		if (result == 0)
		{
			baseDirT.clear();
			break;
		}
		else if (result == ownPathLen && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			ownPathLen += MAX_PATH;
		else
			break;
	}
	size_t ownPathStrlen = _tcslen(baseDirT.data());
	for (size_t i = ownPathStrlen-1; i > 0; i--)
	{
		if (baseDirT[i] == TEXT('\\'))
		{
			baseDirT.resize(i + 1);
			baseDirT[i] = 0;
			ownPathStrlen = i;
			break;
		}
	}

	size_t outLen = 0;
	char *baseDirA = _TCHARToMultiByte(baseDirT.data(), outLen);
	baseDir.assign(baseDirA);
	_FreeCHAR(baseDirA);
	return baseDir;
}

#include "../UABE_Win32/Win32AppContext.h"
int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	std::string baseDir = getModuleBaseDir(hInstance);
	std::vector<char> argvBuf8;
	size_t totalArgvLen = 0;
	char **argv8 = new char*[__argc+1];
	for (int i = 0; i < __argc; i++)
	{
		size_t len16 = wcslen(__wargv[i]);
		if (len16 > INT_MAX) len16 = INT_MAX;
		size_t len8 = (size_t)WideCharToMultiByte(CP_UTF8, 0, __wargv[i], (int)len16, NULL, 0, NULL, NULL);
		size_t argvBufOffset = argvBuf8.size();
		argvBuf8.resize(argvBuf8.size() + len8 + 1);
		WideCharToMultiByte(CP_UTF8, 0, __wargv[i], (int)len16, &argvBuf8[argvBufOffset], (int)len8, NULL, NULL);
		argvBuf8[argvBufOffset + len8] = 0;
		argv8[i] = (char*)argvBufOffset;
	}
	for (int i = 0; i < __argc; i++)
	{
		argv8[i] = argvBuf8.data() + (size_t)argv8[i];
	}
	argv8[__argc] = nullptr;
	int ret;
	if (HMODULE hUABEWin32 = GetModuleHandle(TEXT("UABE_Win32.dll")))
	{
		Win32AppContext appContext(hUABEWin32, baseDir);
		ret = appContext.Run(__argc, argv8);
	}
	delete[] argv8;
	return ret;
}
