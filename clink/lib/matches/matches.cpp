// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
matches::matches()
{
    m_matches.reserve(64);
}

//------------------------------------------------------------------------------
matches::~matches()
{
    for (int i = 0, e = int(m_matches.size()); i < e; ++i)
        delete m_matches[i];
}

//------------------------------------------------------------------------------
void matches::add_match(const char* match)
{
    if (match == nullptr || match[0] == '\0')
        return;

    int len = int(strlen(match)) + 1;
    char* out = new char[len];
    str_base(out, len).copy(match);
    m_matches.push_back(out);
}

//------------------------------------------------------------------------------
void matches::get_match_lcd(str_base& out) const
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

    str_compare_scope _(str_compare_scope::caseless);

    for (int i = 1, n = get_match_count(); i < n; ++i)
    {
        const char* match = get_match(i);
        int d = str_compare(match, out.c_str());
        if (d >= 0)
            lcd_length = min(d, lcd_length);
    }

    out.truncate(lcd_length);
}



//------------------------------------------------------------------------------
matches_builder::matches_builder(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
matches_builder::~matches_builder()
{
}

//------------------------------------------------------------------------------
void matches_builder::add_match(const char* candidate)
{
    m_matches.add_match(candidate);
}
