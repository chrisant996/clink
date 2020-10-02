// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

#include <vector>

//------------------------------------------------------------------------------
struct match_info
{
    unsigned short  store_id;
    unsigned short  displayable_store_id;
    unsigned short  aux_store_id;
    unsigned short  cell_count;
    match_type      type;
    unsigned char   suffix : 7; // TODO: suffix can be in store instead of info.
    unsigned char   select : 1;
};



//------------------------------------------------------------------------------
class match_store
{
public:
    const char*             get(unsigned int id) const;

protected:
    static const int        alignment_bits = 1;
    static const int        alignment = 1 << alignment_bits;
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
    virtual const char*     get_displayable(unsigned int index) const override;
    virtual const char*     get_aux(unsigned int index) const override;
    virtual char            get_suffix(unsigned int index) const override;
    virtual match_type      get_match_type(unsigned int index) const override;
    virtual unsigned int    get_cell_count(unsigned int index) const override;
    virtual bool            has_aux() const override;
    bool                    is_prefix_included() const;
    virtual void            get_match_lcd(str_base& out) const override;

private:
    friend class            match_pipeline;
    friend class            match_builder;
    void                    set_prefix_included(bool included);
    bool                    add_match(const match_desc& desc);
    unsigned int            get_info_count() const;
    match_info*             get_infos();
    const match_store&      get_store() const;
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
        int                 store_front(const char* str);
        int                 store_back(const char* str);

    private:
        unsigned int        get_size(const char* str) const;
        unsigned int        m_front;
        unsigned int        m_back;
    };

    typedef std::vector<match_info> infos;

    store_impl              m_store;
    infos                   m_infos;
    unsigned short          m_count = 0;
    bool                    m_coalesced = false;
    bool                    m_has_aux = false;
    bool                    m_prefix_included = false;
};
