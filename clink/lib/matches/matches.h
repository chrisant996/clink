// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
class matches
{
    friend class matches_builder;
    friend class match_system;

public:
                        matches();
                        ~matches();
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;

private:
    void                add_match(const char* match);
    void                clear_matches();
    std::vector<char*>  m_matches;

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
inline void matches::clear_matches()
{
    m_matches.clear();
}



//------------------------------------------------------------------------------
class matches_builder
{
public:
                    matches_builder(matches& matches);
                    ~matches_builder();
    void            add_match(const char* match);
    void            clear_matches();
    const matches&  get_matches() const;

private:
    matches&        m_matches;
};

//------------------------------------------------------------------------------
inline void matches_builder::clear_matches()
{
    m_matches.clear_matches();
}

//------------------------------------------------------------------------------
inline const matches& matches_builder::get_matches() const
{
    return m_matches;
}
