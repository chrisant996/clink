// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
class matches
{
public:
    virtual unsigned int    get_match_count() const = 0;
    virtual const char*     get_match(unsigned int index) const = 0;
    virtual unsigned int    get_visible_chars(unsigned int index) const = 0;
    virtual bool            has_quoteable() const = 0;
    virtual int             get_first_quoteable(unsigned int index) const = 0;
    virtual void            get_match_lcd(str_base& out) const = 0;
};



//------------------------------------------------------------------------------
class match_builder
{
public:
                            match_builder(matches& matches);
    bool                    add_match(const char* match);
    
private:
    matches&                m_matches;
};
