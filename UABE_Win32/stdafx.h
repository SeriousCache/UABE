//Precompiled header file (Visual Studio compiler)

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <shobjidl.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef __checkoutofmemory
#define __assert(exp,msg) if(!(exp)){MessageBoxA(NULL,msg,"Error",16);ExitProcess(E_OUTOFMEMORY);}
#define __checkoutofmemory(exp) __assert(!(exp),"Out of memory!")
#endif
