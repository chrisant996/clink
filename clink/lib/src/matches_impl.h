// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches.h"

#include "core/array.h"
#include "core/linear_allocator.h"
#include <unordered_set>
#include <vector>

//------------------------------------------------------------------------------
struct match_info
{
    const char*     match;
    const char*     display;
    const char*     description;
    unsigned        ordinal;            // Original unsorted order.
    match_type      type;
    char            append_char;        // Zero means not specified.
    char            suppress_append;    // Negative means not specified.
    bool            append_display;
    bool            select;
    bool            infer_type;
};

//------------------------------------------------------------------------------
struct match_lookup
{
    const char*     match;
    match_type      type;
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
    : DBGOBJECT_ public matches
{
    struct match_lookup_hasher;
    struct match_lookup_comparator;

public:
    typedef std::unordered_set<match_lookup, match_lookup_hasher, match_lookup_comparator> match_lookup_unordered_set;

                            matches_impl(unsigned int store_size=0x10000);
                            ~matches_impl();
    matches_iter            get_iter() const;
    matches_iter            get_iter(const char* pattern) const;

    virtual void            get_lcd(str_base& out) const override;
    virtual unsigned int    get_match_count() const override;
    virtual const char*     get_match(unsigned int index) const override;
    virtual match_type      get_match_type(unsigned int index) const override;
    virtual const char*     get_match_display(unsigned int index) const override;
    virtual const char*     get_match_description(unsigned int index) const override;
    virtual unsigned int    get_match_ordinal(unsigned int index) const override;
    virtual char            get_match_append_char(unsigned int index) const override;
    virtual shadow_bool     get_match_suppress_append(unsigned int index) const override;
    virtual bool            get_match_append_display(unsigned int index) const override;
    virtual bool            is_suppress_append() const override;
    virtual shadow_bool     is_filename_completion_desired() const override;
    virtual shadow_bool     is_filename_display_desired() const override;
    virtual char            get_append_character() const override;
    virtual int             get_suppress_quoting() const override;
    virtual int             get_word_break_position() const override;
    virtual bool            match_display_filter(const char* needle, char** matches, match_display_filter_entry*** filtered_matches, display_filter_flags flags, bool* old_filtering=nullptr) const override;

    void                    set_word_break_position(int position);
    void                    set_regen_blocked();
    bool                    is_regen_blocked() const { return m_regen_blocked; }
    bool                    is_volatile() const { return m_volatile; }

    void                    set_generator(match_generator* generator);
    void                    done_building();

    void                    transfer(matches_impl& from);
    void                    clear();

private:
    virtual const char*     get_unfiltered_match(unsigned int index) const override;
    virtual match_type      get_unfiltered_match_type(unsigned int index) const override;
    virtual const char*     get_unfiltered_match_display(unsigned int index) const override;
    virtual const char*     get_unfiltered_match_description(unsigned int index) const override;
    virtual char            get_unfiltered_match_append_char(unsigned int index) const override;
    virtual shadow_bool     get_unfiltered_match_suppress_append(unsigned int index) const override;
    virtual bool            get_unfiltered_match_append_display(unsigned int index) const override;

    friend class            match_pipeline;
    friend class            match_builder;
    friend class            matches_iter;
    void                    set_append_character(char append);
    void                    set_suppress_append(bool suppress);
    void                    set_suppress_quoting(int suppress);
    void                    set_deprecated_mode();
    void                    set_matches_are_files(bool files);
    void                    set_no_sort();
    void                    set_volatile();
    bool                    add_match(const match_desc& desc, bool already_normalised=false);
    unsigned int            get_info_count() const;
    const match_info*       get_infos() const;
    match_info*             get_infos();
    void                    reset();
    void                    coalesce(unsigned int count_hint, bool restrict=false);

private:
    class store_impl : public linear_allocator
    {
    public:
                            store_impl(unsigned int size);
        const char*         store_front(const char* str) { return store(str); }
    };

    typedef std::vector<match_info> infos;

    match_generator*        m_generator = nullptr;

    store_impl              m_store;
    infos                   m_infos;
    unsigned short          m_count = 0;
    bool                    m_any_infer_type = false;
    bool                    m_can_infer_type = true;
    bool                    m_coalesced = false;
    char                    m_append_character = '\0';
    bool                    m_suppress_append = false;
    bool                    m_regen_blocked = false;
    bool                    m_nosort = false;
    bool                    m_volatile = false;
    int                     m_suppress_quoting = 0;
    int                     m_word_break_position = -1;
    shadow_bool             m_filename_completion_desired;
    shadow_bool             m_filename_display_desired;

    match_lookup_unordered_set* m_dedup = nullptr;
};

//------------------------------------------------------------------------------
bool can_try_substring_pattern(const char* pattern);
char* make_substring_pattern(const char* pattern, const char* append=nullptr);
