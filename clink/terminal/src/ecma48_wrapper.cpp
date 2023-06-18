// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_wrapper.h"
#include "wcwidth.h"

//------------------------------------------------------------------------------
ecma48_wrapper::ecma48_wrapper(const char* in, uint32 wrap)
{
    // WARNING:  This assumes `in` contains no TAB or CR or LF characters!

    while (*in == ' ')
        in++;

    ecma48_state state;
    ecma48_iter iter(in, state);

    const char* s = in;
    const char* end_fits = s;
    const char* end_word = s;
    const char* next_word = s;
    uint32 cells = 0;

    while (true)
    {
        const ecma48_code& code = iter.next();
        if (!code)
            break;
        if (code.get_type() == ecma48_code::type_chars)
        {
            wcwidth_iter inner_iter(code.get_pointer(), code.get_length());
            while (true)
            {
                const int32 c = inner_iter.next();

                if (!c || c == ' ')
                {
                    end_fits = end_word;
                    next_word = inner_iter.get_pointer();
                }
                if (!c)
                    break;

                const int32 w = inner_iter.character_wcwidth_onectrl();
                if (wrap && cells + w > wrap)
                {
                    if (end_fits <= s) // Must fit at least one segment!
                    {
                        end_fits = end_word;
                        next_word = end_word;
                    }

                    assert(end_fits > s);
                    m_lines.emplace_back(s, int32(end_fits - s));

                    s = next_word;
                    while (*s == ' ')
                        s++;
                    inner_iter.reset_pointer(s);
                    end_fits = s;
                    end_word = s;
// REVIEW: next_word is invalid now; is there any code path that can attempt to use it while it's invalid?
                    cells = 0;
                    continue;
                }

                cells += w;

                if (c != ' ')
                    end_word = inner_iter.get_pointer();
            }
        }
        else
        {
            end_fits += code.get_length();
        }
    }

    if (end_fits > s)
        m_lines.emplace_back(s, int32(end_fits - s));

    if (m_lines.empty())
        m_lines.emplace_back(in, 0);
}

//------------------------------------------------------------------------------
bool ecma48_wrapper::next(str_base& out)
{
    if (m_next >= m_lines.size())
        return false;
    out.clear();
    out.concat(m_lines[m_next].get_pointer(), m_lines[m_next].length());
    out << "\n";
    m_next++;
    return true;
}
