// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>

#include <assert.h>

static void __cdecl do_nothing(wchar_t const*, wchar_t const*, wchar_t const*, unsigned int, uintptr_t)
{
}

//------------------------------------------------------------------------------
// The C runtime _popen() function has a bug (issue clink#727) where it tries
// to double-close a file handle upon failure.  But also many Lua APIs pass
// input parameters directly through to various C runtime functions, expecting
// the C runtime to do parameter validation and return a failure message which
// Lua can then print to the terminal.  But the default behavior for the C
// runtime changed and now forces a Watson report (crashes the process).
// That's not an appropriate response in Clink or other programs which host
// the Lua engine, so we need to set an invalid parameter handle that does
// nothing, so that the C runtime continues gracefully (returning an error)
// instead of forcibly terminating the process.
void install_crt_invalid_parameter_handler()
{
    _set_invalid_parameter_handler(do_nothing);
}
