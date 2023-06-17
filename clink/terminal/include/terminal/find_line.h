// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum find_line_mode : int32
{
    none                    = 0x00,
    use_regex               = 0x01,
    ignore_case             = 0x02,
};
DEFINE_ENUM_FLAG_OPERATORS(find_line_mode);

//------------------------------------------------------------------------------
int32 find_line(HANDLE h, const CONSOLE_SCREEN_BUFFER_INFO& csbi,
              wchar_t* chars_buffer, int32 chars_capacity,
              WORD* attrs_buffer, int32 attrs_capacity,
              int32 starting_line, int32 distance,
              const char* text, find_line_mode mode,
              const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff);
