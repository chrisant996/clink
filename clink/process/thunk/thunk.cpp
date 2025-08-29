// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>

#include "thunk.h"

//------------------------------------------------------------------------------
extern "C" DWORD WINAPI zzz_stdcall_thunk(zzz_thunk_data& data)
{
    data.out = data.func(data.in);
    return 0;
}
