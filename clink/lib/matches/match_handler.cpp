// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_handler.h"

#include <core/path.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
class generic_match_handler
    : public match_handler
{
public:
    virtual bool    compare(const char* word, const char* match) override;
    virtual void    get_displayable(const char* match, str_base& out) override;
};

//------------------------------------------------------------------------------
bool generic_match_handler::compare(const char* word, const char* match)
{
    int i = str_compare(word, match);
    if (i >= 0)
        return (word[i] != '\0');

    return true;
}

//------------------------------------------------------------------------------
void generic_match_handler::get_displayable(const char* match, str_base& out)
{
    out.copy(match);
}

//------------------------------------------------------------------------------
match_handler* get_generic_match_handler()
{
    static generic_match_handler handler;
    return &handler;
}



//------------------------------------------------------------------------------
class file_match_handler
    : public match_handler
{
public:
    virtual bool    compare(const char* word, const char* match) override;
    virtual void    get_displayable(const char* match, str_base& out) override;
};

//------------------------------------------------------------------------------
bool file_match_handler::compare(const char* word, const char* match)
{
    const char* word_name = path::get_name(word);
    const char* match_name = path::get_name(match);
    if (word_name - word < match_name - match)
        match_name = match + (word_name - word);

    int i = str_compare(word_name, match_name);
    if (i < 0)
        return true;

    if (word_name[i] == '\0')
        return true;

    if (path::is_separator(match_name[i]) && match_name[i + 1] == '\0')
        return true;

    return false;
}

//------------------------------------------------------------------------------
void file_match_handler::get_displayable(const char* match, str_base& out)
{
    // We don't use path::get_name() here as its result never contains a path
    // separator, and matches may be suffixed with a separator.

    for (const char* i = match; i != nullptr; i = path::next_element(i))
        if (i[0] != '\0')
            match = i;

    out << match;
}

//------------------------------------------------------------------------------
match_handler* get_file_match_handler()
{
    static file_match_handler handler;
    return &handler;
}
