// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "matches_impl.h"
#include "display_matches.h"
#include "slash_translation.h"

#include <core/array.h>
#include <core/path.h>
#include <core/match_wild.h>
#include <core/str_compare.h>
#include <core/str_unordered_set.h>
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

setting_bool g_files_hidden(
    "files.hidden",
    "Include hidden files",
    "Includes or excludes files with the 'hidden' attribute set when generating\n"
    "file lists.",
    true);

setting_bool g_files_system(
    "files.system",
    "Include system files",
    "Includes or excludes files with the 'system' attribute set when generating\n"
    "file lists.",
    false);

extern setting_enum g_default_bindings;
extern setting_bool g_match_wild;



//------------------------------------------------------------------------------
static bool include_match_type(match_type type)
{
    if (is_match_type_system(type))
        return g_files_system.get();
    if (is_match_type_hidden(type))
        return _rl_match_hidden_files && g_files_hidden.get();
    return true;
}

//------------------------------------------------------------------------------
class match_info_indexer
{
public:
    match_info_indexer(match_info* infos) : m_infos(infos) {}
    inline match_info& get_info(int32 i) { return m_infos[i]; }
private:
    match_info* const m_infos;
};

//------------------------------------------------------------------------------
template<class INDEXER>
static uint32 prefix_selector(
    const char* needle,
    INDEXER& indexer,
    int32 count)
{
    const bool include_hidden = (_rl_match_hidden_files || *path::get_name(needle) == '.');
    int32 select_count = 0;
    for (int32 i = 0; i < count; ++i)
    {
        auto& info = indexer.get_info(i);
        const char* const name = info.match;
        const int32 j = str_compare(needle, name);
        const bool select = ((j < 0 || !needle[j]) &&
                             (include_hidden || !path::is_unix_hidden(name, true)) &&
                             include_match_type(info.type));
        info.select = select;
        if (select)
            ++select_count;
    }
    return select_count;
}

//------------------------------------------------------------------------------
template<class INDEXER>
static uint32 pattern_selector(
    const char* needle,
    INDEXER& indexer,
    int32 count,
    bool dot_prefix)
{
    const int32 needle_len = strlen(needle);
    const bool include_hidden = (_rl_match_hidden_files || *path::get_name(needle) == '.');
    int32 select_count = 0;
    for (int32 i = 0; i < count; ++i)
    {
        auto& info = indexer.get_info(i);
        const char* const match = info.match;
        int32 match_len = int32(strlen(match));
        while (match_len && path::is_separator(uint8(match[match_len - 1])))
            match_len--;

        const path::star_matches_everything flag = (is_pathish(info.type) ? path::at_end : path::yes);
        const bool select = ((include_hidden || !path::is_unix_hidden(match, true)) &&
                             include_match_type(info.type) &&
                             path::match_wild(str_iter(needle, needle_len), str_iter(match, match_len), dot_prefix, flag));
        info.select = select;
        if (select)
            ++select_count;
    }
    return select_count;
}

//------------------------------------------------------------------------------
template<class INDEXER>
static void select_matches(const char* needle, INDEXER& indexer, uint32 count)
{
    uint32 found = 0;

    const bool dot_prefix = (rl_completion_type == '%' && g_default_bindings.get() == 1);
    if (dot_prefix || g_match_wild.get())
    {
        str<> pat(needle);
        pat << "*";
        found = pattern_selector(pat.c_str(), indexer, count, dot_prefix);
    }
    else
    {
        found = prefix_selector(needle, indexer, count);
    }

    if (!found && can_try_substring_pattern(needle))
    {
        char* sub = make_substring_pattern(needle, "*");
        if (sub)
        {
            pattern_selector(sub, indexer, count, dot_prefix);
            free(sub);
        }
    }
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
                        int32 order)
{
    bool l_dir = is_dir_match(l, l_type);
    bool r_dir = is_dir_match(r, r_type);

    if (order != 1 && l_dir != r_dir)
        return (order == 0) ? l_dir : r_dir;

    if (l_dir)
        path::maybe_strip_last_separator(l);
    if (r_dir)
        path::maybe_strip_last_separator(r);

    int32 cmp;
    DWORD flags = SORT_DIGITSASNUMBERS|NORM_LINGUISTIC_CASING;
    if (true/*casefold*/)
        flags |= LINGUISTIC_IGNORECASE;
    const wchar_t* l_str = l.c_str();
    const wchar_t* r_str = r.c_str();

    // Sort first by number of leading minus signs.  This is intended so that
    // `-` flags precede `--` flags.
    if (*l_str == '-' || *r_str == '-')
    {
        int32 l_minus = 0;
        int32 r_minus = 0;
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
    const uint8 t1 = uint8(l_type) & MATCH_TYPE_MASK;
    const uint8 t2 = uint8(r_type) & MATCH_TYPE_MASK;

    cmp = int32(t1 == MATCH_TYPE_DIR) - int32(t2 == MATCH_TYPE_DIR);
    if (cmp) return (cmp < 0);

    cmp = int32(t1 == MATCH_TYPE_ALIAS) - int32(t2 == MATCH_TYPE_ALIAS);
    if (cmp) return (cmp < 0);

    cmp = int32(t1 == MATCH_TYPE_COMMAND) - int32(t2 == MATCH_TYPE_COMMAND);
    if (cmp) return (cmp < 0);

    cmp = int32(t1 == MATCH_TYPE_WORD) - int32(t2 == MATCH_TYPE_WORD);
    if (cmp) return (cmp < 0);

    cmp = int32(t1 == MATCH_TYPE_ARG) - int32(t2 == MATCH_TYPE_ARG);
    if (cmp) return (cmp < 0);

    cmp = int32(t1 == MATCH_TYPE_FILE) - int32(t2 == MATCH_TYPE_FILE);
    if (cmp) return (cmp < 0);

    return (cmp < 0);
}

//------------------------------------------------------------------------------
bool compare_matches(const char* l, match_type l_type, const char* r, match_type r_type)
{
    wstr<> ltmp;
    wstr<> rtmp;
    to_utf16(ltmp, l);
    to_utf16(rtmp, r);
    return sort_worker(ltmp, l_type, rtmp, r_type, g_sort_dirs.get());
}

//------------------------------------------------------------------------------
static void alpha_sorter(match_info* infos, int32 count)
{
    int32 order = g_sort_dirs.get();
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
static void ordinal_sorter(match_info* infos, int32 count)
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

    // Detect what kind of slash to use in case slash translation mode ends up
    // being automatic.  Generators can change the mode, so don't check it here.
    char sep = 0;
    str<32> endword;
    state.get_end_word(endword);
    for (const char* p = endword.c_str(); *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            sep = *p;
            break;
        }
    }
    m_matches.set_path_separator(sep);

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

        int32 limit = dbg_get_env_int("DEBUG_PIPELINE");
        if (limit < 0)
            limit = 0 - limit;
        else
            limit = 20;

        int32 i = 0;
        for (matches_iter iter = m_matches.get_iter(); iter.next(); i++)
        {
            if (i >= limit)
            {
                printf(" ...");
                break;
            }
            printf(" %s", iter.get_match());
        }
        printf("\n");
    }
#endif
}

//------------------------------------------------------------------------------
void match_pipeline::restrict(str_base& needle) const
{
    const int32 count = m_matches.get_info_count();

    str<> expanded;
    if (rl_complete_with_tilde_expansion && needle.c_str()[0] == '~')
    {
        if (path::tilde_expand(needle.c_str(), expanded))
        {
            if (!needle.c_str()[1])
                path::maybe_strip_last_separator(expanded);
            needle = expanded.c_str();
        }
    }

    if (count)
    {
        // WARNING:  This is subtly different from select_matches().
        const bool dot_prefix = (rl_completion_type == '%' && g_default_bindings.get() == 1);

        match_info_indexer indexer(m_matches.get_infos());
        if (!pattern_selector(needle.c_str(), indexer, count, dot_prefix) &&
            can_try_substring_pattern(needle.c_str()))
        {
            char* sub = make_substring_pattern(needle.c_str());
            if (sub)
            {
                pattern_selector(sub, indexer, count, dot_prefix);
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
        int32 c = iter.next();
        if (c == '*' || c == '?')
        {
            needle.truncate(int32(ptr - needle.c_str()));
            break;
        }
    }
}

//------------------------------------------------------------------------------
void match_pipeline::restrict(char** keep_matches) const
{
    const int32 count = m_matches.get_info_count();
    int32 select_count = 0;

    str_unordered_set set;
    if (keep_matches && keep_matches[0])
    {
        while (*(++keep_matches))
            set.insert(*keep_matches);
    }

    if (count)
    {
        match_info* infos = m_matches.get_infos();
        for (int32 i = 0; i < count; ++i, ++infos)
        {
            const bool select = (set.find(infos->match) != set.end());
            infos->select = select;
            if (select)
                ++select_count;
        }
    }

    m_matches.coalesce(select_count, true/*restrict*/);
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* needle) const
{
    const int32 count = m_matches.get_info_count();

    str<> expanded;
    if (rl_complete_with_tilde_expansion && needle[0] == '~')
    {
        if (path::tilde_expand(needle, expanded))
        {
            if (!needle[1])
                path::maybe_strip_last_separator(expanded);
            needle = expanded.c_str();
        }
    }

    if (count)
    {
        match_info_indexer indexer(m_matches.get_infos());
        select_matches(needle, indexer, count);
        m_matches.set_completion_type(rl_completion_type);
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

    int32 count = m_matches.get_match_count();
    if (!count)
        return;

    if (m_matches.m_nosort)
        ordinal_sorter(m_matches.get_infos(), count); // "no sort" means "original order".
    else
        alpha_sorter(m_matches.get_infos(), count);
}
