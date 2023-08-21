// Copyright (c) 2021-2022 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "match_adapter.h"
#include "matches.h"
#include "matches_lookaside.h"
#include "display_matches.h"

#include <core/str_compare.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
};

//------------------------------------------------------------------------------
extern "C" char* __printable_part(char* text);

//------------------------------------------------------------------------------
void match_adapter::cached_info::clear()
{
    m_count = 0;
    m_lcd.clear();
    m_has_descriptions = -1;
    m_has_lcd = false;
}

//------------------------------------------------------------------------------
match_adapter::~match_adapter()
{
    free_filtered();
    clear_alt();
}

//------------------------------------------------------------------------------
const matches* match_adapter::get_matches() const
{
    return m_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_matches(const matches* matches)
{
    free_filtered();
    clear_alt();
    m_real_matches = matches;
    m_matches = m_real_matches;
    m_cached.clear();
}

//------------------------------------------------------------------------------
void match_adapter::set_regen_matches(const matches* matches)
{
    free_filtered();
    clear_alt();
    m_matches = matches ? matches : m_real_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_alt_matches(char** matches, bool own)
{
    free_filtered();
    clear_alt();

    m_alt_matches = matches;
    m_alt_own = own;

    // Skip first alt match when counting.
    if (matches && matches[1])
    {
        assert(has_matches_lookaside(matches));

        uint32 count = 0;
        while (*(++matches))
            count++;
        m_alt_cached.m_count = count;
    }
}

//------------------------------------------------------------------------------
void match_adapter::set_filtered_matches(match_display_filter_entry** filtered_matches)
{
    free_filtered();

    m_filtered_matches = filtered_matches;
    m_filtered_cached.m_has_descriptions = false;

    // Skip first filtered match; it's fake, to satisfy Readline's expectation
    // that matches start at [1].
    if (filtered_matches && filtered_matches[0])
    {
        m_filtered_cached.m_has_descriptions = (filtered_matches[0]->visible_display < 0);

        uint32 count = 0;
        while (*(++filtered_matches))
            count++;
        m_filtered_cached.m_count = count;
    }
}

//------------------------------------------------------------------------------
void match_adapter::init_has_descriptions()
{
    m_cached.clear();
}

//------------------------------------------------------------------------------
void match_adapter::reset()
{
    free_filtered();
    clear_alt();
}

//------------------------------------------------------------------------------
matches_iter match_adapter::get_iter()
{
    assert(m_matches);
    free_filtered();
    clear_alt();
    return m_matches->get_iter();
}

//------------------------------------------------------------------------------
void match_adapter::get_lcd(str_base& out) const
{
    if (m_filtered_matches)
    {
        if (!m_filtered_cached.m_has_lcd)
        {
            uint32 i = 1; // 1 based indexing.
            m_filtered_cached.m_has_lcd = true;
            m_filtered_cached.m_lcd.clear();
            if (i <= m_filtered_cached.m_count)
                m_filtered_cached.m_lcd = m_filtered_matches[i++]->match;
            while (i <= m_filtered_cached.m_count)
            {
                const char* match = m_filtered_matches[i++]->match;
                int32 matching = str_compare<char, true/*compute_lcd*/>(m_filtered_cached.m_lcd.c_str(), match);
                m_filtered_cached.m_lcd.truncate(matching);
            }
        }

        out = m_filtered_cached.m_lcd.c_str();
    }
    else if (m_alt_matches)
    {
        if (!m_alt_cached.m_has_lcd)
        {
            uint32 i = 1; // 1 based indexing.
            m_alt_cached.m_has_lcd = true;
            m_alt_cached.m_lcd.clear();
            if (i <= m_alt_cached.m_count)
                m_alt_cached.m_lcd = m_alt_matches[i++];
            while (i <= m_alt_cached.m_count)
            {
                const char *match = m_alt_matches[i++];
                int32 matching = str_compare<char, true/*compute_lcd*/>(m_alt_cached.m_lcd.c_str(), match);
                m_alt_cached.m_lcd.truncate(matching);
            }
        }

        out = m_alt_cached.m_lcd.c_str();
    }
    else if (m_matches)
    {
        if (!m_cached.m_has_lcd)
        {
            m_cached.m_has_lcd = true;
            m_matches->get_lcd(m_cached.m_lcd);
        }

        out = m_cached.m_lcd.c_str();
    }
    else
    {
        out.clear();
    }
}

//------------------------------------------------------------------------------
uint32 match_adapter::get_match_count() const
{
    if (m_filtered_matches)
        return m_filtered_cached.m_count;
    if (m_alt_matches)
        return m_alt_cached.m_count;
    if (m_matches)
        return m_matches->get_match_count();
    return 0;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->match;
    if (m_alt_matches)
        return m_alt_matches[index + 1];
    if (m_matches)
        return m_matches->get_match(index);
    return nullptr;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_display_internal(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->display;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_display();
    else if (m_matches)
        return m_matches->get_match_display(index);
    return nullptr;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_display(uint32 index) const
{
    const char* const display = get_match_display_internal(index);

    // Don't use __printable_part(), because append_filename() needs to know
    // both the raw match and the printable part.
    if (display && *display)
        return display;

    return get_match(index);
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_display_raw(uint32 index) const
{
    const char* const display = get_match_display_internal(index);

    // Don't use __printable_part(), because append_filename() needs to know
    // both the raw match and the printable part.
    if (display && *display)
        return display;

    return nullptr;
}

//------------------------------------------------------------------------------
uint32 match_adapter::get_match_visible_display(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->visible_display;

    const char* display;
    if (m_alt_matches)
        display = lookup_match(m_alt_matches[index + 1]).get_display();
    else if (m_matches)
        display = m_matches->get_match_display(index);
    else
        return 0;

    if (display && *display)
        return cell_count(display);
    const char* match = get_match(index);
    match_type type = get_match_type(index);
    return printable_len(match, type);
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match_description(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->description;

    const char *description;
    if (m_alt_matches)
        description = lookup_match(m_alt_matches[index + 1]).get_description();
    else if (m_matches)
        description = m_matches->get_match_description(index);
    else
        return nullptr;
    return description && *description ? description : nullptr;
}

//------------------------------------------------------------------------------
uint32 match_adapter::get_match_visible_description(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->visible_description;

    const char* description = get_match_description(index);
    return description ? cell_count(description) : 0;
}

//------------------------------------------------------------------------------
match_type match_adapter::get_match_type(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->type;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_type();
    if (m_matches)
        return m_matches->get_match_type(index);
    return match_type::none;
}

//------------------------------------------------------------------------------
char match_adapter::get_match_append_char(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->append_char;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_append_char();
    if (m_matches)
        return m_matches->get_match_append_char(index);
    return 0;
}

//------------------------------------------------------------------------------
uint8 match_adapter::get_match_flags(uint32 index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->flags;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_flags();
    if (m_matches)
    {
        uint8 flags = 0;
        shadow_bool suppress = m_matches->get_match_suppress_append(index);
        if (suppress.is_explicit())
        {
            flags |= MATCH_FLAG_HAS_SUPPRESS_APPEND;
            if (suppress.get())
                flags |= MATCH_FLAG_SUPPRESS_APPEND;
        }
        if (m_matches->get_match_append_display(index))
            flags |= MATCH_FLAG_APPEND_DISPLAY;
        return flags;
    }
    return 0;
}

//------------------------------------------------------------------------------
bool match_adapter::is_custom_display(uint32 index) const
{
    if (m_filtered_matches)
    {
        if (!m_filtered_matches[index + 1]->match[0])
            return true;
        const char* temp = __printable_part(const_cast<char*>(m_filtered_matches[index + 1]->match));
        if (strcmp(temp, m_filtered_matches[index + 1]->display) != 0)
            return true;
    }

    const char* display = get_match_display(index);
    if (!display || !*display)
        return false;

    const char* match = get_match(index);
    assert(match && *match);
    if (strcmp(match, display) != 0)
        return true;

    return false;
}

//------------------------------------------------------------------------------
bool match_adapter::is_append_display(uint32 index) const
{
    return !!(get_match_flags(index) & MATCH_FLAG_APPEND_DISPLAY);
}

//------------------------------------------------------------------------------
bool match_adapter::use_display(uint32 index, match_type type, bool append) const
{
    return ((append) ||
            (is_display_filtered() && is_match_type(type, match_type::none)) ||
            (is_custom_display(index)));
}

//------------------------------------------------------------------------------
bool match_adapter::is_fully_qualify() const
{
    return m_matches && m_matches->is_fully_qualify();
}

//------------------------------------------------------------------------------
bool match_adapter::is_display_filtered() const
{
    return !!m_filtered_matches;
}

//------------------------------------------------------------------------------
bool match_adapter::has_descriptions() const
{
    if (m_filtered_matches)
    {
        return m_filtered_cached.m_has_descriptions > 0;
    }

    if (m_alt_matches)
    {
        if (m_alt_cached.m_has_descriptions < 0)
        {
            m_alt_cached.m_has_descriptions = false;
            for (char** matches = m_alt_matches; *(++matches);)
            {
                match_details details = lookup_match(*matches);
                if (details)
                {
                    const char* desc = details.get_description();
                    if (desc && *desc)
                    {
                        m_alt_cached.m_has_descriptions = true;
                        break;
                    }
                }
            }
        }
        return m_alt_cached.m_has_descriptions > 0;
    }

    if (m_matches)
    {
        if (m_cached.m_has_descriptions < 0)
        {
            m_cached.m_has_descriptions = false;
            for (uint32 i = m_matches ? m_matches->get_match_count() : 0; i--;)
            {
                if (m_matches->get_match_description(i))
                {
                    m_cached.m_has_descriptions = true;
                    break;
                }
            }
        }
        return m_cached.m_has_descriptions > 0;
    }
    return false;
}

//------------------------------------------------------------------------------
void match_adapter::free_filtered()
{
    if (m_filtered_matches)
    {
        free_filtered_matches(m_filtered_matches);
        m_filtered_matches = nullptr;
        m_filtered_cached.clear();
    }
}

//------------------------------------------------------------------------------
void match_adapter::clear_alt()
{
    if (m_alt_own)
    {
        _rl_free_match_list(m_alt_matches);
        m_alt_own = false;
    }
    m_alt_matches = nullptr;
    m_alt_cached.clear();
}
