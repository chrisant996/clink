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
static unsigned int prefix_selector(
    const char* needle,
    match_info* infos,
    int count)
{
    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* const name = infos[i].match;
        const int j = str_compare(needle, name);
        const bool select = (j < 0 || !needle[j]);
        infos[i].select = select;
        if (select)
            ++select_count;
    }
    return select_count;
}

//------------------------------------------------------------------------------
static unsigned int pattern_selector(
    const char* needle,
    match_info* infos,
    int count)
{
    const int needle_len = strlen(needle);
    int select_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const char* const match = infos[i].match;
        int match_len = int(strlen(match));
        while (match_len && path::is_separator((unsigned char)match[match_len - 1]))
            match_len--;

        const path::star_matches_everything flag = (is_pathish(infos[i].type) ? path::at_end : path::yes);
        const bool select = path::match_wild(str_iter(needle, needle_len), str_iter(match, match_len), flag);
        infos[i].select = select;
        if (select)
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

    int cmp;
    DWORD flags = SORT_DIGITSASNUMBERS|NORM_LINGUISTIC_CASING;
    if (true/*casefold*/)
        flags |= LINGUISTIC_IGNORECASE;
    const wchar_t* l_str = l.c_str();
    const wchar_t* r_str = r.c_str();

    // Sort first by number of leading minus signs.  This is intended so that
    // `-` flags precede `--` flags.
    if (*l_str == '-' || *r_str == '-')
    {
        int l_minus = 0;
        int r_minus = 0;
        for (const wchar_t* l_walk = l_str; *l_walk == '-'; ++l_walk)
            l_minus++;
        for (const wchar_t* r_walk = r_str; *r_walk == '-'; ++r_walk)
            r_minus++;
        cmp = l_minus - r_minus;
        if (cmp) return (cmp < 0);
    }

    // Sort next by the strings.
    cmp = CompareStringW(LOCALE_USER_DEFAULT, flags, l_str, l.length(), r_str, r.length());
    cmp -= CSTR_EQUAL;
    if (cmp) return (cmp < 0);

    // If case insensitive sort, then compare again as case sensitive, for
    // consistent ordering.
    if (flags & LINGUISTIC_IGNORECASE)
    {
        flags &= ~LINGUISTIC_IGNORECASE;
        cmp = CompareStringW(LOCALE_USER_DEFAULT, flags, l_str, l.length(), r_str, r.length());
        cmp -= CSTR_EQUAL;
        if (cmp) return (cmp < 0);
    }

    // Finally sort by type (dir, alias, command, word, arg, file).
    unsigned char t1 = ((unsigned char)l_type) & MATCH_TYPE_MASK;
    unsigned char t2 = ((unsigned char)r_type) & MATCH_TYPE_MASK;

    cmp = int(t1 == MATCH_TYPE_DIR) - int(t2 == MATCH_TYPE_DIR);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_ALIAS) - int(t2 == MATCH_TYPE_ALIAS);
    if (cmp) return (cmp < 0);

    cmp = int(t1 == MATCH_TYPE_COMMAND) - int(t2 == MATCH_TYPE_COMMAND);
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
static void ordinal_sorter(match_info* infos, int count)
{
    auto predicate = [&] (const match_info& lhs, const match_info& rhs) {
        return lhs.ordinal < rhs.ordinal;
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
void match_pipeline::set_no_sort()
{
    m_matches.set_no_sort();
}

//------------------------------------------------------------------------------
void match_pipeline::generate(
    const line_states& states,
    match_generator* generator,
    bool old_filtering) const
{
    const auto& state = states.back();

    m_matches.set_word_break_position(state.get_end_word_offset());

    match_builder builder(m_matches);
    if (generator)
        generator->generate(states, builder, old_filtering);

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
    const int count = m_matches.get_info_count();

    str<> expanded;
    if (rl_complete_with_tilde_expansion && needle.c_str()[0] == '~')
    {
        if (path::tilde_expand(needle.c_str(), expanded))
            needle = expanded.c_str();
    }

    if (count)
    {
        if (!pattern_selector(needle.c_str(), m_matches.get_infos(), count) &&
            can_try_substring_pattern(needle.c_str()))
        {
            char* sub = make_substring_pattern(needle.c_str());
            if (sub)
            {
                pattern_selector(sub, m_matches.get_infos(), count);
                free(sub);
            }
        }
    }

    m_matches.coalesce(count, true/*restrict*/);

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
    const int count = m_matches.get_info_count();

    str<> expanded;
    if (rl_complete_with_tilde_expansion && needle[0] == '~')
    {
        if (path::tilde_expand(needle, expanded))
            needle = expanded.c_str();
    }

    if (count)
    {
        if (!prefix_selector(needle, m_matches.get_infos(), count) &&
            can_try_substring_pattern(needle))
        {
            char* sub = make_substring_pattern(needle, "*");
            if (sub)
            {
                pattern_selector(sub, m_matches.get_infos(), count);
                free(sub);
            }
        }
    }

    m_matches.coalesce(count);

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
}

//------------------------------------------------------------------------------
void match_pipeline::sort() const
{
    // Clink takes over responsibility for sorting, and disables Readline's
    // internal sorting.  However, Clink's Lua API allows generators to disable
    // sorting.

    int count = m_matches.get_match_count();
    if (!count)
        return;

    if (m_matches.m_nosort)
        ordinal_sorter(m_matches.get_infos(), count); // "no sort" means "original order".
    else
        alpha_sorter(m_matches.get_infos(), count);
}
