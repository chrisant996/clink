// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

#include <vector>

//------------------------------------------------------------------------------
struct match_info
{
    const char*     match;
    match_type      type;
    bool            select;
};



//------------------------------------------------------------------------------
class match_store
{
protected:
    char*                   m_ptr;
    unsigned int            m_size;
};



//------------------------------------------------------------------------------
class matches_impl
    : public matches
{
public:
                            matches_impl(unsigned int store_size=0x10000);
    virtual unsigned int    get_match_count() const override;
    virtual const char*     get_match(unsigned int index) const override;
    virtual match_type      get_match_type(unsigned int index) const override;
    virtual bool            is_suppress_append() const override;
    virtual bool            is_prefix_included() const override;
    virtual int             get_prefix_excluded() const override;
    virtual char            get_append_character() const override;
    virtual int             get_suppress_quoting() const override;

private:
    friend class            match_pipeline;
    friend class            match_builder;
    void                    set_append_character(char append);
    void                    set_prefix_included(bool included);
    void                    set_prefix_included(int amount);
    void                    set_suppress_append(bool suppress);
    void                    set_suppress_quoting(int suppress);
    bool                    add_match(const match_desc& desc);
    unsigned int            get_info_count() const;
    match_info*             get_infos();
    void                    reset();
    void                    coalesce(unsigned int count_hint);

private:
    class store_impl
        : public match_store
    {
    public:
                            store_impl(unsigned int size);
                            ~store_impl();
        void                reset();
        const char*         store_front(const char* str);
        const char*         store_back(const char* str);

    private:
        unsigned int        get_size(const char* str) const;
        bool                new_page();
        void                free_chain(bool keep_one);
        unsigned int        m_front;
        unsigned int        m_back;
    };

    typedef std::vector<match_info> infos;

    store_impl              m_store;
    infos                   m_infos;
    unsigned short          m_count = 0;
    bool                    m_coalesced = false;
    char                    m_append_character = 0;
    bool                    m_prefix_included = false;
    int                     m_prefix_excluded = 0;
    bool                    m_suppress_append = false;
    int                     m_suppress_quoting = 0;
};
