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
#include <core/settings.h>
#include <terminal/ecma48_iter.h>

#include <algorithm>
#include <assert.h>

//------------------------------------------------------------------------------
static setting_enum g_sort_dirs(
    "match.sort_dirs",
    "Where to sort matching directories",
    "Matching directories can go before files, with files, or after files.",
    "before,with,after",
    1);



//------------------------------------------------------------------------------
inline bool is_path_separator(char c)
{
    return (c == '\\' || c == '/');
}

//------------------------------------------------------------------------------
static unsigned int normal_selector(
    const char* orig_needle,
    unsigned int len_prefix,
    match_info* infos,
    int count)
{
    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* name = infos[i].match;
        const char* needle = orig_needle + (is_pathish(infos[i].type) ? len_prefix : 0);
        int j = str_compare(needle, name);
        infos[i].select = (j < 0 || !needle[j]);
        ++select_count;
    }

    return select_count;
}

//------------------------------------------------------------------------------
static void alpha_sorter(match_info* infos, int count)
{
    int order = g_sort_dirs.get();

    auto predicate = [&] (const match_info& lhs, const match_info& rhs) {
        const char* l = lhs.match;
        const char* r = rhs.match;
        if (order != 1)
        {
            size_t l_len = strlen(l);
            size_t r_len = strlen(r);
            bool l_dir = (l_len && is_path_separator(l[l_len - 1]));
            bool r_dir = (r_len && is_path_separator(r[r_len - 1]));
            if (l_dir != r_dir)
                return (order == 0) ? l_dir : r_dir;
        }
        return (stricmp(l, r) < 0);
    };

    std::sort(infos, infos + count, predicate);
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
        info->cell_count = cell_count(displayable);
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* needle, unsigned int len_prefix) const
{
    int count = m_matches.get_info_count();
    if (!count)
        return;

    unsigned int selected_count = 0;
    selected_count = normal_selector(needle, len_prefix, m_matches.get_infos(), count);

    m_matches.coalesce(selected_count);
}

//------------------------------------------------------------------------------
void match_pipeline::sort() const
{
    int count = m_matches.get_match_count();
    if (!count)
        return;

    alpha_sorter(m_matches.get_infos(), count);
}
