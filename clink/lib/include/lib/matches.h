// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
enum class match_type : unsigned char
{
    do_not_use,     // complete.c relies on the type never being 0, so it can use savestring().
    none,
    word,
    alias,
    file,
    dir,
    link,
    mask = 0x0f,
    hidden = 0x40,
    readonly = 0x80,
};

DEFINE_ENUM_FLAG_OPERATORS(match_type);

//------------------------------------------------------------------------------
inline bool is_pathish(match_type type)
{
    type &= match_type::mask;
    return type == match_type::file || type == match_type::dir || type == match_type::link;
}



//------------------------------------------------------------------------------
class matches
{
public:
    virtual unsigned int    get_match_count() const = 0;
    virtual const char*     get_match(unsigned int index) const = 0;
#ifdef NYI_MATCHES
    virtual const char*     get_displayable(unsigned int index) const = 0;
    virtual const char*     get_aux(unsigned int index) const = 0;
#endif
    virtual char            get_suffix(unsigned int index) const = 0;
    virtual match_type      get_match_type(unsigned int index) const = 0;
#ifdef NYI_MATCHES
    virtual unsigned int    get_cell_count(unsigned int index) const = 0;
    virtual bool            has_aux() const = 0;
#endif
    virtual void            get_match_lcd(str_base& out) const = 0;
};



//------------------------------------------------------------------------------
match_type to_match_type(int mode, int attr);
match_type to_match_type(const char* type_name);

//------------------------------------------------------------------------------
struct match_desc
{
    const char*             match;          // Match text.
#ifdef NYI_MATCHES
    const char*             displayable;
    const char*             aux;
#endif
    char                    suffix;         // Added after match, e.g. '%' for env vars.
    match_type              type;           // Match type.
};

//------------------------------------------------------------------------------
class match_builder
{
public:
                            match_builder(matches& matches);
    bool                    add_match(const char* match, match_type type);
    bool                    add_match(const match_desc& desc);
    void                    set_prefix_included(bool included=true);

private:
    matches&                m_matches;
};
