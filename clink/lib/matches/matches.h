// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
class matches
{
public:
                        matches(unsigned int store_size=0x10000);
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;
    void                reset();
    void                add_match(const char* match);
    void                coalesce();

//private:
    struct info
    {
        unsigned short  store_id : 15;
        unsigned short  selected : 1;
    };

    class store
    {
    public:
                        store(unsigned int size);
                        ~store();
        void            reset();
        const char*     get(unsigned int id) const;
        int             store_front(const char* str);
        int             store_back(const char* str);

    private:
        unsigned int    get_size(const char* str) const;
        char*           m_ptr;
        unsigned int    m_size;
        unsigned int    m_front;
        unsigned int    m_back;
    };

    store               m_store;
    std::vector<info>   m_infos;
    unsigned int        m_match_count;
    bool                m_coalesced;

private:
                        matches(const matches&) = delete;
                        matches(matches&&) = delete;
    void                operator = (const matches&) = delete;
    void                operator = (matches&& rhs) = delete;

    friend class match_pipeline;
};
