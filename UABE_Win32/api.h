#pragma once
#ifdef UABE_Win32_EXPORTS
#define UABE_Win32_API __declspec(dllexport)
#else
#define UABE_Win32_API __declspec(dllimport)
#endif
