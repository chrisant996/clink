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
    virtual int     compare(const char* word, const char* match) override;
    virtual void    get_displayable(const char* match, str_base& out) override;
};

//------------------------------------------------------------------------------
int generic_match_handler::compare(const char* word, const char* match)
{
    return str_compare(word, match);
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
    virtual int     compare(const char* word, const char* match) override;
    virtual void    get_displayable(const char* match, str_base& out) override;
};

//------------------------------------------------------------------------------
int file_match_handler::compare(const char* word, const char* match)
{
    int offset = 0;
    int j;
    while (1)
    {
        j = str_compare(word + offset, match + offset);
        if (j < 0 || (match[j] != '\\' && match[j] != '/'))
            break;

        offset = j + 1;
    }

    return j += offset;
}

//------------------------------------------------------------------------------
void file_match_handler::get_displayable(const char* match, str_base& out)
{
    path::get_name(match, out);
}

//------------------------------------------------------------------------------
match_handler* get_file_match_handler()
{
    static file_match_handler handler;
    return &handler;
}
