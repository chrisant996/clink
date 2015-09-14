// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
class matches
{
public:
                        matches();
                        ~matches();
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                add_match(const char* match);
    void                clear_matches();
    void                get_match_lcd(str_base& out) const;

private:
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
class match_result_builder
{
public:
                    match_result_builder(matches& result, const char* match_word);
                    ~match_result_builder();
    void            operator << (const char* candidate);

private:
    matches&        m_result;
    const char*     m_match_word;
    int             m_word_char_count;
};
