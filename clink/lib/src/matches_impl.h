// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

#include "core/array.h"
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
class match_generator;

//------------------------------------------------------------------------------
class matches_impl
    : public matches
{
public:
    typedef fixed_array<match_generator*, 32> generators;

                            matches_impl(generators* generators=nullptr, unsigned int store_size=0x10000);
    matches_iter            get_iter() const;
    matches_iter            get_iter(const char* pattern) const;

    virtual unsigned int    get_match_count() const override;
    virtual bool            is_suppress_append() const override;
    virtual shadow_bool     is_filename_completion_desired() const override;
    virtual shadow_bool     is_filename_display_desired() const override;
    virtual char            get_append_character() const override;
    virtual int             get_suppress_quoting() const override;
    virtual int             get_word_break_position() const override;
    virtual bool            match_display_filter(char** matches, match_display_filter_entry*** filtered_matches, bool popup) const override;

    void                    set_word_break_position(int position);
    void                    set_regen_blocked();
    bool                    is_regen_blocked() const { return m_regen_blocked; }

private:
    virtual const char*     get_match(unsigned int index) const override;
    virtual match_type      get_match_type(unsigned int index) const override;
    virtual const char*     get_unfiltered_match(unsigned int index) const override;
    virtual match_type      get_unfiltered_match_type(unsigned int index) const override;

    friend class            match_pipeline;
    friend class            match_builder;
    friend class            matches_iter;
    void                    set_append_character(char append);
    void                    set_suppress_append(bool suppress);
    void                    set_suppress_quoting(int suppress);
    void                    set_matches_are_files(bool files);
    bool                    add_match(const match_desc& desc);
    unsigned int            get_info_count() const;
    const match_info*       get_infos() const;
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
    generators*             m_generators;
    infos                   m_infos;
    unsigned short          m_count = 0;
    bool                    m_coalesced = false;
    char                    m_append_character = '\0';
    bool                    m_suppress_append = false;
    bool                    m_regen_blocked = false;
    int                     m_suppress_quoting = 0;
    int                     m_word_break_position = -1;
    shadow_bool             m_filename_completion_desired;
    shadow_bool             m_filename_display_desired;
};
