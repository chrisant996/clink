// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_pipeline.h"
#include "line_state.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "matches_impl.h"

#include <core/array.h>
#include <core/path.h>
#include <core/match_wild.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
};

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
static bool s_nosort = false;

//------------------------------------------------------------------------------
static unsigned int normal_selector(
    const char* needle,
    match_info* infos,
    int count)
{
    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* name = infos[i].match;
        int j = str_compare(needle, name);
        infos[i].select = (j < 0 || !needle[j]);
        ++select_count;
    }

    return select_count;
}

//------------------------------------------------------------------------------
static unsigned int restrict_selector(
    const char* needle,
    match_info* infos,
    int count)
{
    int needle_len = strlen(needle);

    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* match = infos[i].match;
        int match_len = int(strlen(match));
        while (match_len && path::is_separator((unsigned char)match[match_len - 1]))
            match_len--;

        const path::star_matches_everything flag = (is_pathish(infos[i].type) ? path::at_end : path::yes);
        infos[i].select = path::match_wild(str_iter(needle, needle_len), str_iter(match, match_len), flag);
        ++select_count;
    }

    return select_count;
}

//------------------------------------------------------------------------------
static bool is_dir_match(const wstr_base& match, match_type type)
{
    if (is_match_type(type, match_type::dir))
        return true;
    if (!is_match_type(type, match_type::none))
        return false;
    if (match.empty())
        return false;
    return path::is_separator(match.c_str()[match.length() - 1]);
}

//------------------------------------------------------------------------------
inline bool sort_worker(wstr_base& l, match_type l_type,
                        wstr_base& r, match_type r_type,
                        int order)
{
    bool l_dir = is_dir_match(l, l_type);
    bool r_dir = is_dir_match(r, r_type);

    if (order != 1 && l_dir != r_dir)
        return (order == 0) ? l_dir : r_dir;

    if (l_dir)
        path::maybe_strip_last_separator(l);
    if (r_dir)
        path::maybe_strip_last_separator(r);

    DWORD flags = SORT_DIGITSASNUMBERS|NORM_LINGUISTIC_CASING;
    if (true/*casefold*/)
        flags |= LINGUISTIC_IGNORECASE;
    int cmp = CompareStringW(LOCALE_USER_DEFAULT, flags,
                            l.c_str(), l.length(),
                            r.c_str(), r.length());
    cmp -= CSTR_EQUAL;
    if (cmp) return (cmp < 0);

    unsigned char t1 = ((unsigned char)l_type) & MATCH_TYPE_MASK;
    unsigned char t2 = ((unsigned char)r_type) & MATCH_TYPE_MASK;

    cmp = int(t1 == MATCH_TYPE_DIR) - int(t2 == MATCH_TYPE_DIR);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_ALIAS) - int(t2 == MATCH_TYPE_ALIAS);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_WORD) - int(t2 == MATCH_TYPE_WORD);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_ARG) - int(t2 == MATCH_TYPE_ARG);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_FILE) - int(t2 == MATCH_TYPE_FILE);
    if (cmp) return (cmp < 0);

    return (cmp < 0);
}

//------------------------------------------------------------------------------
static void alpha_sorter(match_info* infos, int count)
{
    int order = g_sort_dirs.get();
    wstr<> ltmp;
    wstr<> rtmp;

    auto predicate = [&] (const match_info& lhs, const match_info& rhs) {
        ltmp.clear();
        rtmp.clear();
        to_utf16(ltmp, lhs.match);
        to_utf16(rtmp, rhs.match);
        return sort_worker(ltmp, lhs.type, rtmp, rhs.type, order);
    };

    std::sort(infos, infos + count, predicate);
}

//------------------------------------------------------------------------------
void sort_match_list(char** matches, int len)
{
    if (s_nosort || len <= 0)
        return;

    int order = g_sort_dirs.get();
    wstr<> ltmp;
    wstr<> rtmp;

    auto predicate = [&] (const char* l, const char* r) {
        match_type l_type = (match_type)lookup_match_type(l);
        match_type r_type = (match_type)lookup_match_type(r);

        ltmp.clear();
        rtmp.clear();
        to_utf16(ltmp, l);
        to_utf16(rtmp, r);

        return sort_worker(ltmp, l_type, rtmp, r_type, order);
    };

    std::sort(matches, matches + len, predicate);
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
    s_nosort = false;
}

//------------------------------------------------------------------------------
void match_pipeline::set_nosort(bool nosort)
{
    s_nosort = nosort;
}

//------------------------------------------------------------------------------
void match_pipeline::generate(
    const line_state& state,
    match_generator* generator,
    bool old_filtering) const
{
    m_matches.set_word_break_position(state.get_end_word_offset());

    match_builder builder(m_matches);
    generator->generate(state, builder, old_filtering);

    m_matches.done_building();

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_PIPELINE"))
    {
        printf("GENERATE, %u matches, word break %u, file_comp %u %s --%s",
               m_matches.get_match_count(),
               m_matches.get_word_break_position(),
               m_matches.is_filename_completion_desired().get(),
               m_matches.is_filename_completion_desired().is_explicit() ? "(exp)" : "(imp)",
               m_matches.get_match_count() ? "" : " <none>");

        int i = 0;
        for (matches_iter iter = m_matches.get_iter(); i < 21 && iter.next(); i++)
        {
            if (i == 20)
                printf(" ...");
            else
                printf(" %s", iter.get_match());
        }
        printf("\n");
    }
#endif
}

//------------------------------------------------------------------------------
void match_pipeline::restrict(str_base& needle) const
{
    int count = m_matches.get_info_count();
    unsigned int selected_count = 0;

    char* expanded = nullptr;
    if (rl_complete_with_tilde_expansion)
    {
        expanded = tilde_expand(needle.c_str());
        if (expanded && strcmp(needle.c_str(), expanded) != 0)
            needle = expanded;
    }

    if (count)
        selected_count = restrict_selector(needle.c_str(), m_matches.get_infos(), count);

    m_matches.coalesce(selected_count, true/*restrict*/);

    // Trim any wildcards from needle.
    str_iter iter(needle.c_str(), needle.length());
    while (iter.more())
    {
        const char* ptr = iter.get_pointer();
        int c = iter.next();
        if (c == '*' || c == '?')
        {
            needle.truncate(int(ptr - needle.c_str()));
            break;
        }
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* needle) const
{
    int count = m_matches.get_info_count();
    unsigned int selected_count = 0;

    char* expanded = nullptr;
    if (rl_complete_with_tilde_expansion)
    {
        expanded = tilde_expand(needle);
        if (expanded && strcmp(needle, expanded) != 0)
            needle = expanded;
    }

    if (count)
        selected_count = normal_selector(needle, m_matches.get_infos(), count);

    m_matches.coalesce(selected_count);

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_PIPELINE"))
    {
        printf("COALESCED, file_comp %u %s -- needle '%s' selected %u matches\n",
               m_matches.is_filename_completion_desired().get(),
               m_matches.is_filename_completion_desired().is_explicit() ? "(exp)" : "(imp)",
               needle,
               m_matches.get_match_count());
    }
#endif

    free(expanded);
}

//------------------------------------------------------------------------------
void match_pipeline::sort() const
{
    // Clink takes over responsibility for sorting, and disables Readline's
    // internal sorting.  However, Clink's Lua API allows generators to disable
    // sorting.

    if (s_nosort)
        return;

    int count = m_matches.get_match_count();
    if (!count)
        return;

    alpha_sorter(m_matches.get_infos(), count);
}
