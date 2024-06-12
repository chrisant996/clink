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
