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
    char            custom_display;     // Negative means not calculated yet.
    bool            select;
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
    uint32                  m_size;
};



//------------------------------------------------------------------------------
class match_generator;

//------------------------------------------------------------------------------
class matches_impl
    :
#ifdef USE_DEBUG_OBJECT
    public object,
#endif
    public matches
{
    struct match_lookup_hasher;
    struct match_lookup_comparator;
    friend class ignore_volatile_matches;

public:
    typedef std::unordered_set<match_lookup, match_lookup_hasher, match_lookup_comparator> match_lookup_unordered_set;

                            matches_impl(uint32 store_size=0x10000);
                            ~matches_impl();
    matches_iter            get_iter() const;
    matches_iter            get_iter(const char* pattern) const;

    virtual void            get_lcd(str_base& out) const override;
    virtual uint32          get_match_count() const override;
    virtual const char*     get_match(uint32 index) const override;
    virtual match_type      get_match_type(uint32 index) const override;
    virtual const char*     get_match_display(uint32 index) const override;
    virtual const char*     get_match_description(uint32 index) const override;
    virtual uint32          get_match_ordinal(uint32 index) const override;
    virtual char            get_match_append_char(uint32 index) const override;
    virtual shadow_bool     get_match_suppress_append(uint32 index) const override;
    virtual bool            get_match_append_display(uint32 index) const override;
    virtual bool            get_match_custom_display(uint32 index) const override;
    virtual bool            is_suppress_append() const override;
    virtual shadow_bool     is_filename_completion_desired() const override;
    virtual shadow_bool     is_filename_display_desired() const override;
    virtual bool            is_fully_qualify() const override;
    virtual char            get_append_character() const override;
    virtual int32           get_suppress_quoting() const override;
    virtual bool            get_force_quoting() const override;
    virtual int32           get_word_break_position() const override;
    virtual bool            has_descriptions() const override;
    virtual bool            is_volatile() const override;
    virtual bool            match_display_filter(const char* needle, char** matches, ::matches* out, display_filter_flags flags, bool* old_filtering=nullptr) const override;
    virtual bool            filter_matches(char** matches, char completion_type, bool filename_completion_desired) const override;

    void                    set_word_break_position(int32 position);
    void                    set_regen_blocked();
    bool                    is_regen_blocked() const { return m_regen_blocked; }

    void                    set_generator(match_generator* generator);
    void                    done_building();

    void                    transfer(matches_impl& from);
    void                    copy(const matches_impl& from);
    void                    clear();

private:
    virtual const char*     get_unfiltered_match(uint32 index) const override;
    virtual match_type      get_unfiltered_match_type(uint32 index) const override;
    virtual const char*     get_unfiltered_match_display(uint32 index) const override;
    virtual const char*     get_unfiltered_match_description(uint32 index) const override;
    virtual char            get_unfiltered_match_append_char(uint32 index) const override;
    virtual shadow_bool     get_unfiltered_match_suppress_append(uint32 index) const override;
    virtual bool            get_unfiltered_match_append_display(uint32 index) const override;

    friend class            match_pipeline;
    friend class            match_builder;
    friend class            matches_iter;
    void                    set_append_character(char append);
    void                    set_suppress_append(bool suppress);
    void                    set_suppress_quoting(int32 suppress);
    void                    set_force_quoting();
    void                    set_fully_qualify(bool fully_qualify);
    void                    set_deprecated_mode();
    void                    set_matches_are_files(bool files);
    void                    set_no_sort();
    void                    set_has_descriptions();
    void                    set_volatile();
    void                    set_input_line(const char* text);
    bool                    is_from_current_input_line();
    bool                    add_match(const match_desc& desc, bool already_normalised=false);
    uint32                  get_info_count() const;
    const match_info*       get_infos() const;
    match_info*             get_infos();
    void                    reset();
    void                    coalesce(uint32 count_hint, bool restrict=false);

private:
    class store_impl : public linear_allocator
    {
    public:
                            store_impl(uint32 size);
        const char*         store_front(const char* str) { return store(str); }
    };

    typedef std::vector<match_info> infos;

    match_generator*        m_generator = nullptr;

    store_impl              m_store;
    infos                   m_infos;
    unsigned short          m_count = 0;
    bool                    m_any_none_type = false;
    bool                    m_deprecated_mode = false;
    bool                    m_coalesced = false;
    char                    m_append_character = '\0';
    bool                    m_suppress_append = false;
    bool                    m_has_descriptions = false;
    bool                    m_fully_qualify = false;
    bool                    m_force_quoting = false;
    bool                    m_regen_blocked = false;
    bool                    m_nosort = false;
    bool                    m_volatile = false;
    int32                   m_suppress_quoting = 0;
    int32                   m_word_break_position = -1;
    shadow_bool             m_filename_completion_desired;
    shadow_bool             m_filename_display_desired;
    str_moveable            m_input_line;   // The line the generators were given.

    match_lookup_unordered_set* m_dedup = nullptr;
};

//------------------------------------------------------------------------------
class ignore_volatile_matches
{
public:
                            ignore_volatile_matches(matches_impl& matches);
                            ~ignore_volatile_matches();
private:
    matches_impl&           m_matches;
    const bool              m_volatile;
};

//------------------------------------------------------------------------------
bool can_try_substring_pattern(const char* pattern);
char* make_substring_pattern(const char* pattern, const char* append=nullptr);
