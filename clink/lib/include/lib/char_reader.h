// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"
#include "char_reader.h"

#include <core/str.h>
#include <functional>

//------------------------------------------------------------------------------
struct char_reader_result
{
    bool success = false;
    str<16> utf8;       // Set only on success; contains exactly one codepoint.
};

//------------------------------------------------------------------------------
struct read_number_result
{
    bool success = false;
    int32 number = -1;  // -1 when no digits were entered.
};

//------------------------------------------------------------------------------
using char_reader_callback = std::function<void(char_reader_result)>;
using read_number_callback = std::function<void(read_number_result)>;
using read_number_change_callback = std::function<void(int32)>;

//------------------------------------------------------------------------------
// Activates a one-codepoint read without blocking the editor input loop.
// Pass the result from the current editor_module::on_input() invocation.
// The callback runs once on success, invalid UTF-8, or cancellation.  ASCII
// control bytes are accepted except Esc and Ctrl-G, which cancel the read.
bool read_one_codepoint(editor_module::result& result, char_reader_callback callback);

//------------------------------------------------------------------------------
// Reads decimal digits until Enter, Esc, or Ctrl+G.  Backspace removes a
// digit.  The change callback runs on activation and after each edit, with
// -1 for an empty number.  Other input dings.
bool read_number(editor_module::result& result, read_number_callback callback,
                 read_number_change_callback on_change, int32 max_number = 999999);
