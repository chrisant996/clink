// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_pipeline.h"
#include "match_generator.h"
#include "line_state.h"
#include "match_pipeline.h"
#include "matches.h"

#include <core/str_compare.h>

#include <algorithm>

//------------------------------------------------------------------------------
unsigned int normal_selector(
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
        infos[i].selected = (j < 0 || !needle[j]);
        ++select_count;
    }

    return select_count;
}

//------------------------------------------------------------------------------
void alpha_sorter(const match_store& store, match_info* infos, int count)
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
match_pipeline::match_pipeline(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
void match_pipeline::reset()
{
    m_matches.reset();
}

//------------------------------------------------------------------------------
void match_pipeline::generate(
    const line_state& state,
    const array<match_generator*>& generators)
{
    match_builder builder(m_matches);
    for (auto* generator : generators)
    {
        if (generator->generate(state, builder))
            break;
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* needle)
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
void match_pipeline::sort()
{
    int count = m_matches.get_match_count();
    if (!count)
        return;

    alpha_sorter(m_matches.get_store(), m_matches.get_infos(), count);
}
