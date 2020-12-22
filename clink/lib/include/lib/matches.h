// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str_iter.h>
#include <assert.h>

class str_base;

//------------------------------------------------------------------------------
enum class match_type : unsigned char
{
    do_not_use,     // complete.c relies on the type never being 0, so it can use savestring().
    none,           // Behaves like dir if match ends with path sep, otherwise like file.
    word,           // Matches and displays the whole word even if it contains slashes.
    arg,            // Prevents appending a space if the match ends with a colon or equal sign.
    cmd,            // Displays match using the cmd color.
    alias,          // Displays match using the alias color.
    file,           // Displays match using the file color and only displays the last path component.
    dir,            // Displays match using the directory color, only displays the last path component, and adds a trailing path separator.
    link,           // Displays match using the symlink color and only displays the last path component.
    mask = 0x0f,
    hidden = 0x40,  // Displays file/dir/link matches using the hidden color.
    readonly = 0x80, // Displays file/dir/link matches using the readonly color.
};

DEFINE_ENUM_FLAG_OPERATORS(match_type);

//------------------------------------------------------------------------------
inline bool is_pathish(match_type type)
{
    type &= match_type::mask;
    return type == match_type::file || type == match_type::dir || type == match_type::link;
}

//------------------------------------------------------------------------------
inline bool is_match_type(match_type type, match_type test)
{
    assert((int(test) & ~int(match_type::mask)) == 0);
    type &= match_type::mask;
    return type == test;
}



//------------------------------------------------------------------------------
class shadow_bool
{
public:
                            shadow_bool(bool default_value) : m_default(default_value) { reset(); }
                            shadow_bool(const shadow_bool& o)
                                : m_has_explicit(o.m_has_explicit)
                                , m_explicit(o.m_explicit)
                                , m_implicit(o.m_implicit)
                                , m_default(o.m_default)
                            {}

    operator                bool() const { return get(); }
    bool                    operator=(bool) = delete;

    void                    reset() { m_has_explicit = false; m_explicit = false; m_implicit = m_default; }
    void                    set_explicit(bool value) { m_explicit = value; m_has_explicit = true; }
    void                    set_implicit(bool value) { m_implicit = value; }
    bool                    get() const { return m_has_explicit ? m_explicit : m_implicit; }
    bool                    is_explicit() const { return m_has_explicit; }

private:
    bool                    m_has_explicit : 1;
    bool                    m_explicit : 1;
    bool                    m_implicit : 1;
    bool                    m_default : 1;
};



//------------------------------------------------------------------------------
class matches;

//------------------------------------------------------------------------------
class matches_iter
{
public:
                            matches_iter(const matches& matches, const char* pattern = nullptr);
                            ~matches_iter();
    bool                    next();
    const char*             get_match() const;
    match_type              get_match_type() const;
    shadow_bool             is_filename_completion_desired() const;
    shadow_bool             is_filename_display_desired() const;

private:
    bool                    has_match() const { return m_index < m_next; }
    const matches&          m_matches;
    char*                   m_expanded_pattern;
    str_iter                m_pattern;
    bool                    m_has_pattern = false;
    unsigned int            m_index = 0;
    unsigned int            m_next = 0;

    mutable shadow_bool     m_filename_completion_desired;
    mutable shadow_bool     m_filename_display_desired;
    mutable bool            m_any_pathish = false;
    mutable bool            m_all_pathish = true;
};

//------------------------------------------------------------------------------
struct match_display_filter_entry;

//------------------------------------------------------------------------------
class matches
{
public:
    virtual matches_iter    get_iter(const char* pattern = nullptr) const = 0;
    virtual unsigned int    get_match_count() const = 0;
    virtual bool            is_suppress_append() const = 0;
    virtual shadow_bool     is_filename_completion_desired() const = 0;
    virtual shadow_bool     is_filename_display_desired() const = 0;
    virtual char            get_append_character() const = 0;
    virtual int             get_suppress_quoting() const = 0;
    virtual int             get_word_break_adjustment() const = 0;
    virtual bool            match_display_filter(char** matches, match_display_filter_entry*** filtered_matches, bool popup) const = 0;

private:
    friend class matches_iter;
    virtual const char*     get_match(unsigned int index) const = 0;
    virtual match_type      get_match_type(unsigned int index) const = 0;
    virtual const char*     get_unfiltered_match(unsigned int index) const { return nullptr; }
    virtual match_type      get_unfiltered_match_type(unsigned int index) const { return match_type::none; }
};



//------------------------------------------------------------------------------
match_type to_match_type(int mode, int attr);
match_type to_match_type(const char* type_name);
void match_type_to_string(match_type type, str_base& out);

//------------------------------------------------------------------------------
struct match_desc
{
    const char*             match;          // Match text.
    match_type              type;           // Match type.
};

//------------------------------------------------------------------------------
class match_builder
{
public:
                            match_builder(matches& matches);
    bool                    add_match(const char* match, match_type type);
    bool                    add_match(const match_desc& desc);
    void                    set_append_character(char append);
    void                    set_suppress_append(bool suppress=true);
    void                    set_suppress_quoting(int suppress=1); //0=no, 1=yes, 2=suppress end quote

    void                    set_matches_are_files(bool files=true);

private:
    matches&                m_matches;
};
