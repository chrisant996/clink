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
    m_infos.reserve(64);
}

//------------------------------------------------------------------------------
matches::~matches()
{
    reset();
}

//------------------------------------------------------------------------------
unsigned int matches::get_match_count() const
{
    return (unsigned int)m_matches.size();
}

//------------------------------------------------------------------------------
const char* matches::get_match(unsigned int index) const
{
    return (index < get_match_count()) ? m_matches[index] : nullptr;
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
void matches::reset()
{
    for (int i = 0, e = int(m_matches.size()); i < e; ++i)
        delete m_matches[i];

    m_matches.clear();
    m_infos.clear();
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
    m_infos.push_back({ (unsigned short)(m_infos.size()) });
}
