// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_pipeline.h"
#include "line_state.h"
#include "match_pipeline.h"
#include "match_system.h"
#include "matches.h"

//------------------------------------------------------------------------------
match_pipeline::match_pipeline(const match_system& system, matches& result)
: m_system(system)
, m_matches(result)
{
}

//------------------------------------------------------------------------------
void match_pipeline::reset()
{
    m_matches.reset();
}

//------------------------------------------------------------------------------
void match_pipeline::generate(const line_state& state)
{
    for (const auto& iter : m_system.m_generators)
    {
        auto* generator = (match_generator*)(iter.ptr);
        if (generator->generate(state, m_matches))
            break;
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* selector_name, const char* needle)
{
    int count = m_matches.get_info_count();
    if (!count)
        return;

    unsigned int selected_count = 0;
    if (match_selector* selector = m_system.get_selector(selector_name))
        selected_count = selector->select(needle, m_matches.get_store(),
            m_matches.get_infos(), count);

    m_matches.coalesce(selected_count);
}

//------------------------------------------------------------------------------
void match_pipeline::sort(const char* sorter_name)
{
    int count = m_matches.get_match_count();
    if (!count)
        return;

    if (match_sorter* sorter = m_system.get_sorter(sorter_name))
        sorter->sort(m_matches.get_store(), m_matches.get_infos(), count);
}

//------------------------------------------------------------------------------
void match_pipeline::finalise(unsigned int key)
{
    m_matches.set_match_key(key);
}
