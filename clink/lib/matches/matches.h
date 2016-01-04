// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class match_handler;
class str_base;

//------------------------------------------------------------------------------
class matches
{
public:
                        matches();
                        ~matches();
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;
    void                add_match(const char* match);
    void                reset();
    match_handler&      get_handler() const;
    void                set_handler(match_handler* handler);

private:
    std::vector<char*>  m_matches;
    match_handler*      m_handler;

private:
                        matches(const matches&) = delete;
                        matches(matches&&) = delete;
    void                operator = (const matches&) = delete;
    void                operator = (matches&& rhs) = delete;
};

//------------------------------------------------------------------------------
inline unsigned int matches::get_match_count() const
{
    return (unsigned int)m_matches.size();
}

//------------------------------------------------------------------------------
inline const char* matches::get_match(unsigned int index) const
{
    return (index < get_match_count()) ? m_matches[index] : nullptr;
}

//------------------------------------------------------------------------------
inline void matches::reset()
{
    set_handler(nullptr);
    m_matches.clear();
}
