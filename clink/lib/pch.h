// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <conio.h>
#include <io.h>
#include <locale.h>
#include <stdlib.h>

#include <Windows.h>

extern "C" {

// Lua includes.
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

} // extern "C"
