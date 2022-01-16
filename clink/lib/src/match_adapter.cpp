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

//------------------------------------------------------------------------------
extern "C" char* printable_part(char* text);

//------------------------------------------------------------------------------
match_adapter::~match_adapter()
{
    free_filtered();
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
    m_has_descriptions = -1;
}

//------------------------------------------------------------------------------
void match_adapter::set_regen_matches(const matches* matches)
{
    free_filtered();
    clear_alt();
    m_matches = matches ? matches : m_real_matches;
}

//------------------------------------------------------------------------------
void match_adapter::set_alt_matches(char** matches)
{
    clear_alt();

    m_alt_matches = matches;

    // Skip first alt match when counting.
    if (matches && matches[1])
    {
        assert(has_matches_lookaside(matches));

        unsigned int count = 0;
        while (*(++matches))
            count++;
        m_alt_count = count;
    }
}

//------------------------------------------------------------------------------
void match_adapter::set_filtered_matches(match_display_filter_entry** filtered_matches)
{
    free_filtered();

    m_filtered_matches = filtered_matches;
    m_filtered_has_descriptions = false;

    // Skip first filtered match; it's fake, to satisfy Readline's expectation
    // that matches start at [1].
    if (filtered_matches && filtered_matches[0])
    {
        m_filtered_has_descriptions = (filtered_matches[0]->visible_display < 0);

        unsigned int count = 0;
        while (*(++filtered_matches))
            count++;
        m_filtered_count = count;
    }
}

//------------------------------------------------------------------------------
void match_adapter::init_has_descriptions()
{
    m_has_descriptions = -1;
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
        for (unsigned int i = 0; i < m_filtered_count; i++)
        {
            const char* match = m_filtered_matches[i + 1]->match;
            if (!i)
            {
                out = match;
            }
            else
            {
                int matching = str_compare<char, true/*compute_lcd*/>(out.c_str(), match);
                out.truncate(matching);
            }
        }
    }
    else if (m_alt_matches)
    {
        out = m_alt_matches[0];
    }
    else if (m_matches)
    {
        m_matches->get_lcd(out);
    }
    else
    {
        out.clear();
    }
}

//------------------------------------------------------------------------------
unsigned int match_adapter::get_match_count() const
{
    if (m_filtered_matches)
        return m_filtered_count;
    if (m_alt_matches)
        return m_alt_count;
    if (m_matches)
        return m_matches->get_match_count();
    return 0;
}

//------------------------------------------------------------------------------
const char* match_adapter::get_match(unsigned int index) const
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
const char* match_adapter::get_match_display(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->display;

    const char* display;
    if (m_alt_matches)
        display = lookup_match(m_alt_matches[index + 1]).get_display();
    else if (m_matches)
        display = m_matches->get_match_display(index);
    else
        return nullptr;

    // Don't use printable_part(), because append_filename() needs to know
    // both the raw match and the printable part.
    if (display && *display)
        return display;
    return get_match(index);
}

//------------------------------------------------------------------------------
unsigned int match_adapter::get_match_visible_display(unsigned int index) const
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
const char* match_adapter::get_match_description(unsigned int index) const
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
unsigned int match_adapter::get_match_visible_description(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->visible_description;

    const char* description = get_match_description(index);
    return description ? cell_count(description) : 0;
}

//------------------------------------------------------------------------------
match_type match_adapter::get_match_type(unsigned int index) const
{
    if (m_filtered_matches)
        return static_cast<match_type>(m_filtered_matches[index + 1]->type);
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_type();
    if (m_matches)
        return m_matches->get_match_type(index);
    return match_type::none;
}

//------------------------------------------------------------------------------
char match_adapter::get_match_append_char(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->append_char;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_append_char();
    if (m_matches)
        return m_matches->get_append_character();
    return 0;
}

//------------------------------------------------------------------------------
unsigned char match_adapter::get_match_flags(unsigned int index) const
{
    if (m_filtered_matches)
        return m_filtered_matches[index + 1]->flags;
    if (m_alt_matches)
        return lookup_match(m_alt_matches[index + 1]).get_flags();
    if (m_matches)
    {
        unsigned char flags = 0;
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
bool match_adapter::is_custom_display(unsigned int index) const
{
    if (m_filtered_matches)
    {
        if (!m_filtered_matches[index + 1]->match[0])
            return true;
        const char* temp = printable_part(const_cast<char*>(m_filtered_matches[index + 1]->match));
        if (strcmp(temp, m_filtered_matches[index + 1]->display) != 0)
            return true;
    }
    // TODO: m_alt_matches?
    return false;
}

//------------------------------------------------------------------------------
bool match_adapter::is_append_display(unsigned int index) const
{
    return !!(get_match_flags(index) & MATCH_FLAG_APPEND_DISPLAY);
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
        return m_filtered_has_descriptions > 0;
    }

    if (m_alt_matches)
    {
        if (m_alt_has_descriptions < 0)
        {
            m_alt_has_descriptions = false;
            for (char** matches = m_alt_matches; *(++matches);)
            {
                match_details details = lookup_match(*matches);
                if (details)
                {
                    const char* desc = details.get_description();
                    if (desc && *desc)
                    {
                        m_alt_has_descriptions = true;
                        break;
                    }
                }
            }
        }
        return m_alt_has_descriptions > 0;
    }

    if (m_matches)
    {
        if (m_has_descriptions < 0)
        {
            m_has_descriptions = false;
            for (unsigned int i = m_matches ? m_matches->get_match_count() : 0; i--;)
            {
                if (m_matches->get_match_description(i))
                {
                    m_has_descriptions = true;
                    break;
                }
            }
        }
        return m_has_descriptions > 0;
    }
    return false;
}

//------------------------------------------------------------------------------
void match_adapter::free_filtered()
{
    free_filtered_matches(m_filtered_matches);
    m_filtered_matches = nullptr;
    m_filtered_count = 0;
    m_filtered_has_descriptions = -1;
}

//------------------------------------------------------------------------------
void match_adapter::clear_alt()
{
    m_alt_matches = nullptr;
    m_alt_count = 0;
    m_alt_has_descriptions = -1;
}
