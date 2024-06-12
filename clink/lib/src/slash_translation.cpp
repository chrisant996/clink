// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "slash_translation.h"

#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static int32 s_slash_translation = slash_translation::off;

//------------------------------------------------------------------------------
void set_slash_translation(int32 mode)
{
    s_slash_translation = mode;
}

//------------------------------------------------------------------------------
int32 get_slash_translation()
{
    return s_slash_translation;
}

//------------------------------------------------------------------------------
void do_slash_translation(str_base& in_out, const char* _sep)
{
    assert(s_slash_translation > slash_translation::off);

    uint8 sep;

    switch (s_slash_translation)
    {
    default:
        sep = 0;
        break;

    case slash_translation::slash:
        sep = '/';
        break;

    case slash_translation::backslash:
        sep = '\\';
        break;

    case slash_translation::automatic:
        // Use whatever sep was passed in by the caller.
        // Or if the caller passed nullptr, use the system path separator.
        sep = _sep ? *_sep : 0;
        break;
    }

    assert(sep == 0 || sep == '/' || sep == '\\');

    path::normalise_separators(in_out, sep);
}

