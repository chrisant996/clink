// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class str_base;

//------------------------------------------------------------------------------
struct match_info
{
    unsigned int    score;
    unsigned short  store_id;
    unsigned char   first_quoteable;
    unsigned char   visible_chars;
};



//------------------------------------------------------------------------------
class match_store
{
public:
    const char*         get(unsigned int id) const;

protected:
    char*               m_ptr;
    unsigned int        m_size;
};



//------------------------------------------------------------------------------
class matches
{
public:
                        matches(unsigned int store_size=0x10000);
    unsigned int        get_match_count() const;
    const char*         get_match(unsigned int index) const;
    unsigned int        get_visible_chars(unsigned int index) const;
    bool                has_quoteable() const;
    int                 get_first_quoteable(unsigned int index) const;
    void                get_match_lcd(str_base& out) const;

private:
    friend class        match_pipeline;
    friend class        match_builder;
    bool                add_match(const char* match);
    unsigned int        get_info_count() const;
    match_info*         get_infos();
    const match_store&  get_store() const;
    void                reset();
    void                coalesce(unsigned int count_hint);
    void                set_has_quoteable();

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

    typedef std::vector<match_info> infos; // MODE4

    store_impl          m_store;
    infos               m_infos;
    unsigned short      m_count = 0;
    bool                m_coalesced = false;
    bool                m_has_quotable = false;

private:
                        matches(const matches&) = delete;
                        matches(matches&&) = delete;
    void                operator = (const matches&) = delete;
    void                operator = (matches&& rhs) = delete;
};



//------------------------------------------------------------------------------
class match_builder
{
public:
                match_builder(matches& matches);
    bool        add_match(const char* match);
    
private:
    matches&    m_matches;
};

//------------------------------------------------------------------------------
inline match_builder::match_builder(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
inline bool match_builder::add_match(const char* match)
{
    return m_matches.add_match(match);
}
