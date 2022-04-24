#include "convert.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

const char *_ConvertCPP_EmptyCHAR = "";
const wchar_t *_ConvertCPP_EmptyWCHAR = L"";
char *_WideToMultiByte(const wchar_t *wi, size_t &len)
{
	if (!wi)
		return nullptr;
	int wcLen = (int)(wcslen(wi) & 0x7FFFFFFF);
	int mbLen = WideCharToMultiByte(CP_UTF8, 0, wi, wcLen, NULL, 0, NULL, NULL);
	char *ret = new char[mbLen + 1];
	WideCharToMultiByte(CP_UTF8, 0, wi, wcLen, ret, mbLen, NULL, NULL);
	ret[mbLen] = 0;
	len = (size_t)mbLen;
	return ret;
}
wchar_t *_MultiByteToWide(const char *mb, size_t &len)
{
	if (!mb)
		return nullptr;
	int mbLen = (int)(strlen(mb) & 0x7FFFFFFF);
	int wcLen = MultiByteToWideChar(CP_UTF8, 0, mb, mbLen, NULL, 0);
	wchar_t *ret = new wchar_t[wcLen + 1];
	MultiByteToWideChar(CP_UTF8, 0, mb, mbLen, ret, wcLen);
	ret[wcLen] = 0;
	len = (size_t)wcLen;
	return ret;
}
wchar_t *_WideToWide(const wchar_t *wi, size_t &len)
{
	if (!wi)
		return nullptr;
	len = wcslen(wi);
	wchar_t *ret = new wchar_t[len + 1];
	wcscpy(ret, wi);
	return ret;
}
char *_MultiByteToMultiByte(const char *mb, size_t &len)
{
	if (!mb)
		return nullptr;
	len = strlen(mb);
	char *ret = new char[len + 1];
	strcpy(ret, mb);
	return ret;
}
void _FreeCHAR(char *c)
{
	if (c != nullptr)
		delete[] c;
}
void _FreeWCHAR(WCHAR *c)
{
	if (c != nullptr)
		delete[] c;
}
