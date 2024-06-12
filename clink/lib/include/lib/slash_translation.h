// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
namespace slash_translation {

enum
{
    // IMPORTANT:  Do not rearrange these values!  They are documented and are
    // persisted in the clink_settings file and referenced by numeric value in
    // Lua scripts.

    // IMPORTANT:  Keep these in sync with the match.translate_slashes setting.

    off,
    system,
    slash,
    backslash,
    automatic,      // Can't name it 'auto' because that's a reserved keyword.
    max
};

}

//------------------------------------------------------------------------------
class str_base;

//------------------------------------------------------------------------------
void set_slash_translation(int32 mode);
int32 get_slash_translation();
void do_slash_translation(str_base& in_out, const char* sep=nullptr);
