// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_result.h"
#include "core/base.h"
#include "core/str.h"
#include "core/str_compare.h"

#include <algorithm>

//------------------------------------------------------------------------------
match_result::match_result()
{
}

//------------------------------------------------------------------------------
match_result::match_result(match_result&& rhs)
: match_result()
{
    swap(rhs);
}

//------------------------------------------------------------------------------
match_result::~match_result()
{
    for (int i = 0, e = int(m_matches.size()); i < e; ++i)
        delete m_matches[i];
}

//------------------------------------------------------------------------------
void match_result::operator = (match_result&& rhs)
{
    swap(rhs);
}

//------------------------------------------------------------------------------
void match_result::swap(match_result& rhs)
{
    std::swap(m_matches, rhs.m_matches);
}

//------------------------------------------------------------------------------
void match_result::reserve(unsigned int count)
{
    m_matches.reserve(count);
}

//------------------------------------------------------------------------------
void match_result::add_match(const char* match)
{
    if (match == nullptr || match[0] == '\0')
        return;

    int len = int(strlen(match)) + 1;
    char* out = new char[len];
    str_base(out, len).copy(match);
    m_matches.push_back(out);
}

//------------------------------------------------------------------------------
void match_result::get_match_lcd(str_base& out) const
{
    int match_count = get_match_count();

    if (match_count <= 0)
        return;

    if (match_count == 1)
    {
        out = m_matches[0];
        return;
    }

    out = get_match(0);
    int lcd_length = out.length();

    for (int i = 1, n = get_match_count(); i < n; ++i)
    {
        const char* match = get_match(i);
        int d = str_compare(match, out.c_str());
        if (d >= 0)
            lcd_length = min(d, lcd_length);
    }

    out.truncate(lcd_length);
}
