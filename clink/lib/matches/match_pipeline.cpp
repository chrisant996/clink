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
, m_result(result)
{
}

//------------------------------------------------------------------------------
void match_pipeline::generate(const line_state& state)
{
    m_result.reset();
    for (const auto& iter : m_system.m_generators)
    {
        auto* generator = (match_generator*)(iter.ptr);
        if (generator->generate(state, m_result))
            break;
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* selector_name, const char* needle)
{
    int count = m_result.get_info_count();
    if (!count)
        return;

    unsigned int selected_count = 0;
    if (match_selector* selector = m_system.get_selector(selector_name))
        selected_count = selector->select(needle, m_result.get_store(),
            m_result.get_infos(), count);

    m_result.coalesce(selected_count);
}

//------------------------------------------------------------------------------
void match_pipeline::sort(const char* sorter_name)
{
    int count = m_result.get_match_count();
    if (!count)
        return;

    if (match_sorter* sorter = m_system.get_sorter(sorter_name))
        sorter->sort(m_result.get_store(), m_result.get_infos(), count);
}

//------------------------------------------------------------------------------
void match_pipeline::finalise(unsigned int key)
{
    m_result.set_match_key(key);
}
