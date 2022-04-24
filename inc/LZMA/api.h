#pragma once

#define LIBCOMPRESSION_API
#ifdef _LIBCOMPRESSION_EXPORT
//#define LIBCOMPRESSION_API __declspec(dllexport) 
#else
//#define LIBCOMPRESSION_API __declspec(dllimport) 
#endif