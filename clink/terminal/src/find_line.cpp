// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "find_line.h"
#include "terminal_out.h" // for find_line_mode

#include <core/base.h>
#include <core/str.h>
#include <core/str_transform.h>

#include <assert.h>

#include <regex>

//------------------------------------------------------------------------------
int32 find_line(HANDLE h, const CONSOLE_SCREEN_BUFFER_INFO& csbi,
              wchar_t* chars_buffer, int32 chars_capacity,
              WORD* attrs_buffer, int32 attrs_capacity,
              int32 starting_line, int32 distance,
              const char* text, find_line_mode mode,
              const BYTE* attrs, int32 num_attrs, BYTE mask)
{
    assert(!text || chars_capacity >= csbi.dwSize.X);
    if (!(!text || chars_capacity >= csbi.dwSize.X))
        return -2;

    assert(!attrs || num_attrs <= 0 || attrs_capacity >= csbi.dwSize.X);
    if (!(!attrs || num_attrs <= 0 || attrs_capacity >= csbi.dwSize.X))
        return -2;

    wstr_moveable find;
    wstr_moveable tmp;
    std::unique_ptr<std::wregex> regex;
    if (text && *text)
    {
        find = text;

        if (mode & find_line_mode::use_regex)
        {
            std::regex_constants::syntax_option_type syntax = std::regex_constants::ECMAScript;
            if (mode & find_line_mode::ignore_case)
                syntax |= std::regex_constants::icase;

            try
            {
                regex = std::make_unique<std::wregex>(find.c_str(), syntax);
            }
            catch (std::regex_error ex)
            {
                return -1;
            }
        }
        else
        {
            find = text;
            if (mode & find_line_mode::ignore_case)
            {
                str_transform(find.c_str(), find.length(), tmp, transform_mode::lower);
                find = std::move(tmp);
            }
        }
    }

    int32 start_found = 0;
    int32 len_found = csbi.dwSize.X;

    while (distance != 0)
    {
        if (starting_line < 0 || starting_line >= csbi.dwSize.Y)
            return 0;

        bool found_text = true;
        if (text)
        {
            COORD coord = { 0, SHORT(starting_line) };
            DWORD len = 0;
            if (!ReadConsoleOutputCharacterW(h, chars_buffer, csbi.dwSize.X, coord, &len))
                return -1;
            if (len != csbi.dwSize.X)
                return -1;

            while (len > 0 && iswspace(chars_buffer[len - 1]))
                len--;
            chars_buffer[len] = '\0';

            const wchar_t* line_text = chars_buffer;
            if (!regex && (mode & find_line_mode::ignore_case))
            {
                str_transform(chars_buffer, len, tmp, transform_mode::lower);
                line_text = tmp.c_str();
                len = tmp.length();
            }

            if (regex)
            {
                std::wcmatch matches;
                try
                {
                    std::regex_search(line_text, line_text + len, matches, *regex, std::regex_constants::match_default);
                }
                catch (std::regex_error ex)
                {
                    return -2;
                }

                found_text = matches.size() > 0;
                if (found_text)
                {
                    start_found = int32(matches.position(0));
                    len_found = int32(matches.length(0));
                }
            }
            else
            {
                // Presume that str_transform preserved the alignment between
                // text and attributes.
                const wchar_t* found = wcsstr(chars_buffer, find.c_str());
                found_text = !!found;
                start_found = int32(found - chars_buffer);
                len_found = find.length();
            }
        }

        bool found_attr = true;
        if (attrs && num_attrs)
        {
            found_attr = false;

            COORD coord = { SHORT(start_found), SHORT(starting_line) };
            DWORD len = 0;
            if (!ReadConsoleOutputAttribute(h, attrs_buffer, len_found, coord, &len))
                return -2;
            if (len != len_found)
                return -2;

            const BYTE* end_attrs = attrs + num_attrs;
            for (const WORD* attr = attrs_buffer; len--; attr++)
            {
                for (const BYTE* find_attr = attrs; find_attr < end_attrs; find_attr++)
                    if ((BYTE(*attr) & mask) == (*find_attr & mask))
                    {
                        found_attr = true;
                        goto break_break;
                    }
            }

break_break:
            ;
        }

        if (found_text && found_attr)
            return starting_line;

        if (distance > 0)
        {
            starting_line++;
            distance--;
        }
        else
        {
            starting_line--;
            distance++;
        }
    }

    return -1;
}
