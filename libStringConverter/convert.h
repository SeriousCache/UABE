#pragma once
#ifndef TCHAR
#define _TCHAR_CUSTOM
#ifdef _UNICODE
#define TCHAR wchar_t
#else
#define TCHAR char
#endif
#endif
#include <memory>

//UTF-8 <-> UTF-16 conversion utility functions.
//-> "Multi Byte" refers to UTF-8 here, even though Win32 defines it as the regional one-byte-per-character code page.
//-> TCHAR refers to either UTF-8 or UTF-16.
//   The latter is used if Win32 Unicode support is enabled (_UNICODE).

//len : Length in characters, excluding the null terminator.

char *_WideToMultiByte(const wchar_t *wi, size_t &len);
wchar_t *_MultiByteToWide(const char *mb, size_t &len);
wchar_t *_WideToWide(const wchar_t *wi, size_t &len);
char *_MultiByteToMultiByte(const char *mb, size_t &len);
void _FreeCHAR(char *c);
void _FreeWCHAR(wchar_t *c);

#ifdef _UNICODE
inline TCHAR *_WideToTCHAR(const wchar_t *wi, size_t &len)
{
	return _WideToWide(wi, len);
}
inline TCHAR *_MultiByteToTCHAR(const char *mb, size_t &len)
{
	return _MultiByteToWide(mb, len);
}
inline wchar_t *_TCHARToWide(const TCHAR *tc, size_t &len)
{
	return _WideToWide(tc, len);
}
inline char *_TCHARToMultiByte(const TCHAR *tc, size_t &len)
{
	return _WideToMultiByte(tc, len);
}
inline void _FreeTCHAR(TCHAR *tc)
{
	_FreeWCHAR(tc);
}
#else
inline TCHAR *_WideToTCHAR(const wchar_t *wi, size_t &len)
{
	return _WideToMultiByte(wi, len);
}
inline TCHAR *_MultiByteToTCHAR(const char *mb, size_t &len)
{
	return _MultiByteToMultiByte(mb, len);
}
inline wchar_t *_TCHARToWide(const TCHAR *tc, size_t &len)
{
	return _MultiByteToWide(tc, len);
}
inline char *_TCHARToMultiByte(const TCHAR *tc, size_t &len)
{
	return _MultiByteToMultiByte(tc, len);
}
inline void _FreeTCHAR(TCHAR *tc)
{
	_FreeCHAR(tc);
}
#endif

inline std::unique_ptr<TCHAR[], void(*)(TCHAR*)> unique_WideToTCHAR(const wchar_t *wi, size_t &len)
{
	return std::unique_ptr<TCHAR[], void(*)(TCHAR*)>(_WideToTCHAR(wi, len), _FreeTCHAR);
}
inline std::unique_ptr<TCHAR[], void(*)(TCHAR*)> unique_WideToTCHAR(const wchar_t *wi)
{
	size_t unused;
	return unique_WideToTCHAR(wi, unused);
}

inline std::unique_ptr<TCHAR[], void(*)(TCHAR*)> unique_MultiByteToTCHAR(const char *mb, size_t &len)
{
	return std::unique_ptr<TCHAR[], void(*)(TCHAR*)>(_MultiByteToTCHAR(mb, len), _FreeTCHAR);
}
inline std::unique_ptr<TCHAR[], void(*)(TCHAR*)> unique_MultiByteToTCHAR(const char *mb)
{
	size_t unused;
	return unique_MultiByteToTCHAR(mb, unused);
}

inline std::unique_ptr<char[], void(*)(char*)> unique_TCHARToMultiByte(const TCHAR *tc, size_t &len)
{
	return std::unique_ptr<char[], void(*)(char*)>(_TCHARToMultiByte(tc, len), _FreeCHAR);
}
inline std::unique_ptr<char[], void(*)(char*)> unique_TCHARToMultiByte(const TCHAR *tc)
{
	size_t unused;
	return unique_TCHARToMultiByte(tc, unused);
}


inline std::unique_ptr<char[], void(*)(char*)> unique_WideToMultiByte(const wchar_t *wc, size_t &len)
{
	return std::unique_ptr<char[], void(*)(char*)>(_WideToMultiByte(wc, len), _FreeCHAR);
}
inline std::unique_ptr<char[], void(*)(char*)> unique_WideToMultiByte(const wchar_t *wc)
{
	size_t unused;
	return unique_WideToMultiByte(wc, unused);
}

inline std::unique_ptr<wchar_t[], void(*)(wchar_t*)> unique_MultiByteToWide(const char *mb, size_t &len)
{
	return std::unique_ptr<wchar_t[], void(*)(wchar_t*)>(_MultiByteToWide(mb, len), _FreeWCHAR);
}
inline std::unique_ptr<wchar_t[], void(*)(wchar_t*)> unique_MultiByteToWide(const char *mb)
{
	size_t unused;
	return unique_MultiByteToWide(mb, unused);
}

inline std::unique_ptr<wchar_t[], void(*)(wchar_t*)> unique_WideToWide(const wchar_t *wc, size_t &len)
{
	return std::unique_ptr<wchar_t[], void(*)(wchar_t*)>(_WideToWide(wc, len), _FreeWCHAR);
}
inline std::unique_ptr<wchar_t[], void(*)(wchar_t*)> unique_WideToWide(const wchar_t *wc)
{
	size_t unused;
	return unique_WideToWide(wc, unused);
}

inline std::unique_ptr<char[], void(*)(char*)> unique_MultiByteToMultiByte(const char *mb, size_t &len)
{
	return std::unique_ptr<char[], void(*)(char*)>(_MultiByteToMultiByte(mb, len), _FreeCHAR);
}
inline std::unique_ptr<char[], void(*)(char*)> unique_MultiByteToMultiByte(const char *mb)
{
	size_t unused;
	return unique_MultiByteToMultiByte(mb, unused);
}

#ifdef _TCHAR_CUSTOM
#undef TCHAR
#undef _TCHAR_CUSTOM
#endif