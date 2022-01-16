// Copyright (c) 2021-2022 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

struct match_display_filter_entry;
class matches;
class matches_iter;
enum class match_type : unsigned char;

//------------------------------------------------------------------------------
class match_adapter
{
public:
                    ~match_adapter();
    const matches*  get_matches() const;
    void            set_matches(const matches* matches);
    void            set_regen_matches(const matches* matches);
    void            set_alt_matches(char** matches);
    void            set_filtered_matches(match_display_filter_entry** filtered_matches);
    void            init_has_descriptions();

    matches_iter    get_iter();
    void            get_lcd(str_base& out) const;
    unsigned int    get_match_count() const;
    const char*     get_match(unsigned int index) const;
    match_type      get_match_type(unsigned int index) const;
    const char*     get_match_display(unsigned int index) const;
    unsigned int    get_match_visible_display(unsigned int index) const;
    const char*     get_match_description(unsigned int index) const;
    unsigned int    get_match_visible_description(unsigned int index) const;
    char            get_match_append_char(unsigned int index) const;
    unsigned char   get_match_flags(unsigned int index) const;
    bool            is_custom_display(unsigned int index) const;
    bool            is_append_display(unsigned int index) const;

    bool            is_display_filtered() const;
    bool            has_descriptions() const;

private:
    void            free_filtered();
    void            clear_alt();

private:
    const matches*  m_matches = nullptr;
    const matches*  m_real_matches = nullptr;
    char**          m_alt_matches = nullptr;
    unsigned int    m_alt_count = 0;
    match_display_filter_entry** m_filtered_matches = nullptr;
    unsigned int    m_filtered_count = 0;
    mutable char    m_has_descriptions = -1;
    mutable char    m_alt_has_descriptions = -1;
    mutable char    m_filtered_has_descriptions = -1;
};
