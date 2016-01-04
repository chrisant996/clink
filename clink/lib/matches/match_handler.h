// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
class match_handler
{
public:
    virtual int     compare(const char* word, const char* match) = 0;
    virtual void    get_displayable(const char* match, str_base& out) = 0;
};

//------------------------------------------------------------------------------
match_handler* get_generic_match_handler();
match_handler* get_file_match_handler();
