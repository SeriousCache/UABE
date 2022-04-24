#pragma once
#ifdef UABE_Generic_EXPORTS
#define UABE_Generic_API __declspec(dllexport)
#else
#define UABE_Generic_API __declspec(dllimport)
#endif
