// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_pipeline.h"
#include "line_state.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "matches_impl.h"

#include <core/array.h>
#include <core/str_compare.h>

#include <algorithm>

//------------------------------------------------------------------------------
static unsigned int normal_selector(
    const char* needle,
    const match_store& store,
    match_info* infos,
    int count)
{
    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* name = store.get(infos[i].store_id);
        int j = str_compare(needle, name);
        infos[i].select = (j < 0 || !needle[j]);
        ++select_count;
    }

    return select_count;
}

//------------------------------------------------------------------------------
static void alpha_sorter(const match_store& store, match_info* infos, int count)
{
    struct predicate
    {
        predicate(const match_store& store)
        : store(store)
        {
        }

        bool operator () (const match_info& lhs, const match_info& rhs)
        {
            const char* l = store.get(lhs.store_id);
            const char* r = store.get(rhs.store_id);
            return (stricmp(l, r) < 0);
        }

        const match_store& store;
    };

    std::sort(infos, infos + count, predicate(store));
}



//------------------------------------------------------------------------------
match_pipeline::match_pipeline(matches_impl& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
void match_pipeline::reset() const
{
    m_matches.reset();
}

//------------------------------------------------------------------------------
void match_pipeline::generate(
    const line_state& state,
    const array<match_generator*>& generators) const
{
    match_builder builder(m_matches);
    for (auto* generator : generators)
        if (generator->generate(state, builder))
            break;
}

//------------------------------------------------------------------------------
void match_pipeline::fill_info() const
{
    int count = m_matches.get_info_count();
    if (!count)
        return;

    match_info* info = m_matches.get_infos();
    for (int i = 0; i < count; ++i, ++info)
    {
        const char* displayable = m_matches.get_displayable(i);
        info->visible_chars = char_count(displayable);
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* needle) const
{
    int count = m_matches.get_info_count();
    if (!count)
        return;

    unsigned int selected_count = 0;
    selected_count = normal_selector(needle, m_matches.get_store(),
        m_matches.get_infos(), count);

    m_matches.coalesce(selected_count);
}

//------------------------------------------------------------------------------
void match_pipeline::sort() const
{
    int count = m_matches.get_match_count();
    if (!count)
        return;

    alpha_sorter(m_matches.get_store(), m_matches.get_infos(), count);
}
