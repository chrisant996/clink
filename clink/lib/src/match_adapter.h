// Copyright (c) 2021-2022 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

class matches;
class matches_iter;
enum class match_type : unsigned short;

//------------------------------------------------------------------------------
class match_adapter
{
public:
                    ~match_adapter();
    const matches*  get_matches() const;
    void            set_matches(const matches* matches);
    void            set_regen_matches(const matches* matches);
    void            set_alt_matches(char** matches, bool own);
    void            set_alt_matches(matches* matches, bool own);
    void            set_filtered_matches(matches* filtered_matches, bool own);
    void            init_has_descriptions();
    void            reset();

    void            filter_matches();

    matches_iter    get_iter();
    void            get_lcd(str_base& out) const;
    uint32          get_match_count() const;
    const char*     get_match(uint32 index) const;
    match_type      get_match_type(uint32 index) const;
    const char*     get_match_display(uint32 index) const;
    const char*     get_match_display_raw(uint32 index) const;
    uint32          get_match_visible_display(uint32 index) const;
    const char*     get_match_description(uint32 index) const;
    uint32          get_match_visible_description(uint32 index) const;
    char            get_match_append_char(uint32 index) const;
    uint8           get_match_flags(uint32 index) const;
    bool            is_append_display(uint32 index) const;
    bool            use_display(uint32 index, match_type type, bool append) const;

    bool            is_fully_qualify() const;
    bool            is_display_filtered() const;
    bool            is_alt_matches() const;
    bool            is_initialized() const;
    bool            has_descriptions() const;

private:
    const char*     get_match_display_internal(uint32 index) const;
    bool            get_match_custom_display(uint32 index) const;
    void            free_filtered();
    void            clear_alt();

private:
    struct cached_info
    {
                        cached_info() { clear(); }
        void            clear();
        uint32          m_count;
        str<32>         m_lcd;
        char            m_has_descriptions;
        bool            m_has_lcd;
    };

    const matches*  m_matches = nullptr;
    const matches*  m_real_matches = nullptr;
    char**          m_alt_matches = nullptr;
    bool            m_alt_own = false;
    matches*        m_filtered_matches = nullptr;
    bool            m_filtered_own = false;
    bool            m_is_display_filtered = false;
    mutable cached_info m_cached;
    mutable cached_info m_alt_cached;
    mutable cached_info m_filtered_cached;
};
