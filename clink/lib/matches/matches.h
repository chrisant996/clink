// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
class matches
{
public:
                        matches(unsigned int buffer_size=0x10000);
                        ~matches();
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;
    void                reset();
    void                add_match(const char* match);

//private:
    struct info
    {
        unsigned short  index : 15;
        unsigned short  selected : 1;
    };

    class buffer
    {
    public:
                        buffer(unsigned int size);
                        ~buffer();
        int             store_front(const char* str);
        int             store_back(const char* str);

    private:
        unsigned int    get_size(const char* str);
        char*           m_ptr;
        unsigned int    m_front;
        unsigned int    m_back;
    };

    buffer              m_buffer;
    std::vector<char*>  m_matches;
    std::vector<info>   m_infos;

private:
                        matches(const matches&) = delete;
                        matches(matches&&) = delete;
    void                operator = (const matches&) = delete;
    void                operator = (matches&& rhs) = delete;

    friend class match_pipeline;
};
