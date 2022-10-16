// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/bldopts.h>

#include <stdio.h>
#include <conio.h>
#include <io.h>
#include <locale.h>
#include <stdlib.h>
#ifdef __MINGW32__
#   include <stdint.h>
#endif

#include <Windows.h>
#include <Shellapi.h>
#include <TlHelp32.h>
// Some code
#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4091)
#endif
#include <Shlobj.h>
#ifndef __MINGW32__
#   include <DbgHelp.h>
#endif
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

#include <core/base.h>

#define CLINK_DLL    "clink_dll_" AS_STR(ARCHITECTURE) ".dll"
#define CLINK_EXE    "clink_" AS_STR(ARCHITECTURE) ".exe"
