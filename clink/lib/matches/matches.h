// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
struct match_info
{
    unsigned short  selected : 1;
    unsigned short  store_id : 15;
};



//------------------------------------------------------------------------------
class match_store
{
public:
    const char*     get(unsigned int id) const;

protected:
    char*           m_ptr;
    unsigned int    m_size;
};



//------------------------------------------------------------------------------
class matches
{
public:
                        matches(unsigned int store_size=0x10000);
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;
    void                add_match(const char* match); // MODE4

private:
    friend class        match_pipeline;
    unsigned int        get_info_count() const;
    match_info*         get_infos();
    const match_store&  get_store() const;
    void                reset();
    void                coalesce(unsigned int count_hint);

private:
    class store_impl
        : public match_store
    {
    public:
                        store_impl(unsigned int size);
                        ~store_impl();
        void            reset();
        int             store_front(const char* str);
        int             store_back(const char* str);

    private:
        unsigned int    get_size(const char* str) const;
        unsigned int    m_front;
        unsigned int    m_back;
    };

    typedef std::vector<match_info> infos;

    store_impl          m_store;
    infos               m_infos;
    unsigned short      m_count = 0;
    bool                m_coalesced = false;

private:
                        matches(const matches&) = delete;
                        matches(matches&&) = delete;
    void                operator = (const matches&) = delete;
    void                operator = (matches&& rhs) = delete;
};
