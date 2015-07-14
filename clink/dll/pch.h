// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <stdio.h>
#include <conio.h>
#include <io.h>
#include <locale.h>
#include <stdlib.h>
#ifdef __MINGW32__
#   include <stdint.h>
#endif

#include <Windows.h>
#include <Shlobj.h>
#include <TlHelp32.h>
#ifndef __MINGW32__
#   include <DbgHelp.h>
#endif

// Readline includes.
extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <compat/dirent.h>
}
