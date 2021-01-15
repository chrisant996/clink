// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum find_line_mode : int
{
    none                    = 0x00,
    use_regex               = 0x01,
    ignore_case             = 0x02,
};
DEFINE_ENUM_FLAG_OPERATORS(find_line_mode);

//------------------------------------------------------------------------------
int find_line(HANDLE h, const CONSOLE_SCREEN_BUFFER_INFO& csbi,
              wchar_t* chars_buffer, int chars_capacity,
              WORD* attrs_buffer, int attrs_capacity,
              int starting_line, int distance,
              const char* text, find_line_mode mode,
              const BYTE* attrs=nullptr, int num_attrs=0, BYTE mask=0xff);
