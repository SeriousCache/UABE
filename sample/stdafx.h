#pragma once

#include "targetver.h"

#define _ITERATOR_DEBUG_LEVEL 0 //Otherwise causes issues with the AssetsTools compiled with Release.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//Defined here for convenience
#ifndef __checkoutofmemory
#define __assert(exp,msg) if(!(exp)){MessageBoxA(NULL,msg,"Error",16);ExitProcess(E_OUTOFMEMORY);}
#define __checkoutofmemory(exp) __assert(!(exp),"Out of memory!")
#endif