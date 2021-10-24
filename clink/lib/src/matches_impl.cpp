// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches_impl.h"
#include "match_generator.h"

#include <core/base.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_hash.h>
#include <core/str_tokeniser.h>
#include <core/match_wild.h>
#include <core/path.h>
#include <sys/stat.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
};

#include <assert.h>

//------------------------------------------------------------------------------
static int s_slash_translation = 0;
void set_slash_translation(int mode) { s_slash_translation = mode; }
int get_slash_translation() { return s_slash_translation; }

//------------------------------------------------------------------------------
static setting_enum g_translate_slashes(
    "match.translate_slashes",
    "Translate slashes and backslashes",
    "File and directory completions can be translated to use consistent slashes.\n"
    "The default is 'system' to use the appropriate path separator for the OS host\n"
    "(backslashes on Windows).  Use 'slash' to use forward slashes, or 'backslash'\n"
    "to use backslashes.  Use 'off' to turn off translating slashes from custom\n"
    "match generators.",
    "off,system,slash,backslash",
    1
);



//------------------------------------------------------------------------------
struct matches_impl::match_lookup_hasher
{
    size_t operator()(const match_lookup& info) const
    {
        return str_hash(info.match);
    }
};

//------------------------------------------------------------------------------
struct matches_impl::match_lookup_comparator
{
    bool operator()(const match_lookup& i1, const match_lookup& i2) const
    {
        return (i1.type == i2.type && strcmp(i1.match, i2.match) == 0);
    }
};



//------------------------------------------------------------------------------
match_type to_match_type(int mode, int attr, const char* path)
{
    static_assert(int(match_type::none) == MATCH_TYPE_NONE, "match_type enum must match readline constants");
    static_assert(int(match_type::word) == MATCH_TYPE_WORD, "match_type enum must match readline constants");
    static_assert(int(match_type::arg) == MATCH_TYPE_ARG, "match_type enum must match readline constants");
    static_assert(int(match_type::cmd) == MATCH_TYPE_COMMAND, "match_type enum must match readline constants");
    static_assert(int(match_type::alias) == MATCH_TYPE_ALIAS, "match_type enum must match readline constants");
    static_assert(int(match_type::file) == MATCH_TYPE_FILE, "match_type enum must match readline constants");
    static_assert(int(match_type::dir) == MATCH_TYPE_DIR, "match_type enum must match readline constants");
    static_assert(int(match_type::mask) == MATCH_TYPE_MASK, "match_type enum must match readline constants");
    static_assert(int(match_type::link) == MATCH_TYPE_LINK, "match_type enum must match readline constants");
    static_assert(int(match_type::orphaned) == MATCH_TYPE_ORPHANED, "match_type enum must match readline constants");
    static_assert(int(match_type::hidden) == MATCH_TYPE_HIDDEN, "match_type enum must match readline constants");
    static_assert(int(match_type::readonly) == MATCH_TYPE_READONLY, "match_type enum must match readline constants");

    match_type type;

    if (mode & _S_IFDIR)
        type = match_type::dir;
    else
        type = match_type::file;

    if (attr & FILE_ATTRIBUTE_HIDDEN)
        type |= match_type::hidden;
    if (attr & FILE_ATTRIBUTE_READONLY)
        type |= match_type::readonly;

#ifdef _S_IFLNK
    if (mode & _S_IFLNK)
        type |= match_type::link;
#endif

    if (int(type & match_type::link))
    {
        wstr<288> wfile(path);
        struct _stat64 st;
        if (_wstat64(wfile.c_str(), &st) < 0)
            type |= match_type::orphaned;
    }

    return type;
}

//------------------------------------------------------------------------------
match_type to_match_type(const char* type_name)
{
    match_type type = match_type::none;

    str_tokeniser tokens(type_name ? type_name : "", ",;+|./ ");
    str_iter token;

    while (tokens.next(token))
    {
        const char* t = token.get_pointer();
        int l = token.length();

        // Trim whitespace.
        while (l && isspace((unsigned char)*t))
            t++, l--;
        while (l && isspace((unsigned char)t[l-1]))
            l--;
        if (!l)
            continue;

        // Translate type names into match_type values.
        if (_strnicmp(t, "word", l) == 0)
            type = (type & ~match_type::mask) | match_type::word;
        else if (_strnicmp(t, "arg", l) == 0)
            type = (type & ~match_type::mask) | match_type::arg;
        else if (_strnicmp(t, "cmd", l) == 0)
            type = (type & ~match_type::mask) | match_type::cmd;
        else if (_strnicmp(t, "alias", l) == 0)
            type = (type & ~match_type::mask) | match_type::alias;
        else if (_strnicmp(t, "file", l) == 0)
            type = (type & ~match_type::mask) | match_type::file;
        else if (_strnicmp(t, "dir", l) == 0)
            type = (type & ~match_type::mask) | match_type::dir;
        else if (_strnicmp(t, "link", l) == 0 ||
                 _strnicmp(t, "symlink", l) == 0)
            type |= match_type::link;
        else if (_strnicmp(t, "hidden", l) == 0)
            type |= match_type::hidden;
        else if (_strnicmp(t, "readonly", l) == 0)
            type |= match_type::readonly;
        else if (_strnicmp(t, "orphaned", l) == 0)
            type |= match_type::orphaned;
    }

    // Only files and dirs can be links.
    if (int(type & match_type::link) && !is_match_type(type, match_type::file) && !is_match_type(type, match_type::dir))
        type &= ~match_type::link;

    // Only links can be orphaned.
    if ((type & (match_type::link|match_type::orphaned)) == match_type::orphaned)
        type &= ~match_type::orphaned;

    return type;
}

//------------------------------------------------------------------------------
void match_type_to_string(match_type type, str_base& out)
{
    match_type base = type & match_type::mask;

    static const char* const type_names[] =
    {
        "invalid",
        "none",
        "word",
        "arg",
        "cmd",
        "alias",
        "file",
        "dir",
    };

    out.clear();
    out.concat(type_names[int(base)]);

    if (int(type & match_type::link))
        out.concat(",link");
    if (int(type & match_type::orphaned))
        out.concat(",orphaned");
    if (int(type & match_type::hidden))
        out.concat(",hidden");
    if (int(type & match_type::readonly))
        out.concat(",readonly");
}

//------------------------------------------------------------------------------
match_builder::match_builder(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const char* match, match_type type, bool already_normalised)
{
    char suffix = 0;
    match_desc desc = {
        match,
        nullptr,
        nullptr,
        type,
        false,
    };
    return add_match(desc, already_normalised);
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const match_desc& desc, bool already_normalized)
{
    return ((matches_impl&)m_matches).add_match(desc, already_normalized);
}

//------------------------------------------------------------------------------
void match_builder::set_append_character(char append)
{
    return ((matches_impl&)m_matches).set_append_character(append);
}

//------------------------------------------------------------------------------
void match_builder::set_suppress_append(bool suppress)
{
    return ((matches_impl&)m_matches).set_suppress_append(suppress);
}

//------------------------------------------------------------------------------
void match_builder::set_suppress_quoting(int suppress)
{
    return ((matches_impl&)m_matches).set_suppress_quoting(suppress);
}

//------------------------------------------------------------------------------
void match_builder::set_deprecated_mode()
{
    ((matches_impl&)m_matches).set_deprecated_mode();
}

//------------------------------------------------------------------------------
void match_builder::set_matches_are_files(bool files)
{
    return ((matches_impl&)m_matches).set_matches_are_files(files);
}



//------------------------------------------------------------------------------
matches_iter::matches_iter(const matches& matches, const char* pattern)
: m_matches(matches)
, m_expanded_pattern(pattern && rl_complete_with_tilde_expansion ? tilde_expand(pattern) : nullptr)
, m_pattern((m_expanded_pattern ? m_expanded_pattern : pattern),
            (m_expanded_pattern ? m_expanded_pattern : pattern) ? -1 : 0)
, m_has_pattern(pattern != nullptr)
, m_filename_completion_desired(matches.is_filename_completion_desired())
, m_filename_display_desired(matches.is_filename_display_desired())
{
}

//------------------------------------------------------------------------------
matches_iter::~matches_iter()
{
    free(m_expanded_pattern);
}

//------------------------------------------------------------------------------
bool matches_iter::next()
{
    if (m_has_pattern)
    {
        while (true)
        {
            m_index = m_next;
            m_next++;

            const char* match = get_match();
            if (!match)
            {
                m_next--;
                return false;
            }

            int match_len = int(strlen(match));
            while (match_len && path::is_separator((unsigned char)match[match_len - 1]))
                match_len--;

            const path::star_matches_everything flag = is_pathish(get_match_type()) ? path::at_end : path::yes;
            if (path::match_wild(m_pattern, str_iter(match, match_len), flag))
                goto found;
        }
    }

    m_index = m_next;
    if (m_index >= m_matches.get_match_count())
        return false;
    m_next++;

found:
    if (is_pathish(get_match_type()))
        m_any_pathish = true;
    else
        m_all_pathish = false;
    return true;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match() const
{
    if (m_has_pattern)
        return has_match() ? m_matches.get_unfiltered_match(m_index) : nullptr;
    return has_match() ? m_matches.get_match(m_index) : nullptr;
}

//------------------------------------------------------------------------------
match_type matches_iter::get_match_type() const
{
    if (m_has_pattern)
        return has_match() ? m_matches.get_unfiltered_match_type(m_index) : match_type::none;
    return has_match() ? m_matches.get_match_type(m_index) : match_type::none;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match_display() const
{
    if (m_has_pattern)
        return has_match() ? m_matches.get_unfiltered_match_display(m_index) : nullptr;
    return has_match() ? m_matches.get_match_display(m_index) : nullptr;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match_description() const
{
    if (m_has_pattern)
        return has_match() ? m_matches.get_unfiltered_match_description(m_index) : nullptr;
    return has_match() ? m_matches.get_match_description(m_index) : nullptr;
}

//------------------------------------------------------------------------------
bool matches_iter::get_match_append_display() const
{
    if (m_has_pattern)
        return has_match() ? m_matches.get_unfiltered_match_append_display(m_index) : false;
    return has_match() ? m_matches.get_match_append_display(m_index) : false;
}

//------------------------------------------------------------------------------
shadow_bool matches_iter::is_filename_completion_desired() const
{
    m_filename_completion_desired.set_implicit(m_any_pathish);
    return m_filename_completion_desired;
}

//------------------------------------------------------------------------------
shadow_bool matches_iter::is_filename_display_desired() const
{
    m_filename_display_desired.set_implicit(m_any_pathish && m_all_pathish);
    if (m_filename_completion_desired && m_filename_completion_desired.is_explicit())
        m_filename_display_desired.set_implicit(true);
    return m_filename_display_desired;
}



//------------------------------------------------------------------------------
matches_impl::store_impl::store_impl(unsigned int size)
{
    m_size = max((unsigned int)4096, size);
    m_ptr = nullptr;
    new_page();
}

//------------------------------------------------------------------------------
matches_impl::store_impl::~store_impl()
{
    free_chain(false/*keep_one*/);
}

//------------------------------------------------------------------------------
void matches_impl::store_impl::reset()
{
    free_chain(true/*keep_one*/);
    m_back = m_size;
    m_front = sizeof(m_ptr);
}

//------------------------------------------------------------------------------
const char* matches_impl::store_impl::store_front(const char* str)
{
    unsigned int size = get_size(str);
    unsigned int next = m_front + size;
    if (next > m_back && !new_page())
        return nullptr;

    str_base(m_ptr + m_front, size).copy(str);

    const char* ret = m_ptr + m_front;
    m_front = next;
    return ret;
}

//------------------------------------------------------------------------------
const char* matches_impl::store_impl::store_back(const char* str)
{
    unsigned int size = get_size(str);
    unsigned int next = m_back - size;
    if (next < m_front && !new_page())
        return nullptr;

    m_back = next;
    str_base(m_ptr + m_back, size).copy(str);

    return m_ptr + m_back;
}

//------------------------------------------------------------------------------
unsigned int matches_impl::store_impl::get_size(const char* str) const
{
    if (str == nullptr)
        return 1;

    return int(strlen(str) + 1);
}

//------------------------------------------------------------------------------
bool matches_impl::store_impl::new_page()
{
    char* temp = (char*)malloc(m_size);
    if (temp == nullptr)
        return false;

    *reinterpret_cast<char**>(temp) = m_ptr;
    m_front = sizeof(m_ptr);
    m_back = m_size;
    m_ptr = temp;
    return true;
}

//------------------------------------------------------------------------------
void matches_impl::store_impl::free_chain(bool keep_one)
{
    char* ptr = m_ptr;

    if (!keep_one)
    {
        m_ptr = nullptr;
        m_front = sizeof(m_ptr);
        m_back = m_size;
    }

    while (ptr)
    {
        char* tmp = ptr;
        ptr = *reinterpret_cast<char**>(ptr);
        if (keep_one)
        {
            keep_one = false;
            *reinterpret_cast<char**>(tmp) = nullptr;
        }
        else
            free(tmp);
    }
}



//------------------------------------------------------------------------------
matches_impl::matches_impl(generators* generators, unsigned int store_size)
: m_store(min(store_size, 0x10000u))
, m_generators(generators)
, m_filename_completion_desired(false)
, m_filename_display_desired(false)
{
    m_infos.reserve(1024);
}

//------------------------------------------------------------------------------
matches_iter matches_impl::get_iter() const
{
    return matches_iter(*this);
}

//------------------------------------------------------------------------------
matches_iter matches_impl::get_iter(const char* pattern) const
{
    return matches_iter(*this, pattern);
}

//------------------------------------------------------------------------------
unsigned int matches_impl::get_info_count() const
{
    return int(m_infos.size());
}

//------------------------------------------------------------------------------
const match_info* matches_impl::get_infos() const
{
    return m_infos.size() ? &(m_infos[0]) : nullptr;
}

//------------------------------------------------------------------------------
match_info* matches_impl::get_infos()
{
    return m_infos.size() ? &(m_infos[0]) : nullptr;
}

//------------------------------------------------------------------------------
unsigned int matches_impl::get_match_count() const
{
    return m_count;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match(unsigned int index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].match;
}

//------------------------------------------------------------------------------
match_type matches_impl::get_match_type(unsigned int index) const
{
    if (index >= get_match_count())
        return match_type::none;

    return m_infos[index].type;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match_display(unsigned int index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].display;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match_description(unsigned int index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].description;
}

//------------------------------------------------------------------------------
bool matches_impl::get_match_append_display(unsigned int index) const
{
    if (index >= get_match_count())
        return false;

    return m_infos[index].append_display;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match(unsigned int index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].match;
}

//------------------------------------------------------------------------------
match_type matches_impl::get_unfiltered_match_type(unsigned int index) const
{
    if (index >= get_info_count())
        return match_type::none;

    return m_infos[index].type;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match_display(unsigned int index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].display;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match_description(unsigned int index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].description;
}

//------------------------------------------------------------------------------
bool matches_impl::get_unfiltered_match_append_display(unsigned int index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].append_display;
}

//------------------------------------------------------------------------------
bool matches_impl::is_suppress_append() const
{
    return m_suppress_append;
}

//------------------------------------------------------------------------------
shadow_bool matches_impl::is_filename_completion_desired() const
{
    return m_filename_completion_desired;
}

//------------------------------------------------------------------------------
shadow_bool matches_impl::is_filename_display_desired() const
{
    if (m_filename_display_desired.is_explicit())
        return m_filename_display_desired;

    shadow_bool tmp(m_filename_display_desired);
    if (m_filename_completion_desired && m_filename_completion_desired.is_explicit())
        tmp.set_implicit(true);
    return tmp;
}

//------------------------------------------------------------------------------
char matches_impl::get_append_character() const
{
    return m_append_character;
}

//------------------------------------------------------------------------------
int matches_impl::get_suppress_quoting() const
{
    return m_suppress_quoting;
}

//------------------------------------------------------------------------------
int matches_impl::get_word_break_position() const
{
    return m_word_break_position;
}

//------------------------------------------------------------------------------
bool matches_impl::match_display_filter(const char* needle, char** matches, match_display_filter_entry*** filtered_matches, display_filter_flags flags, bool* old_filtering) const
{
    // TODO:  This doesn't really belong here.  But it's a convenient point to
    // cobble together Lua (via the generators) and the matches.  It's strange
    // to pass 'matches' into matches_impl, but the caller already has it and
    // this way we don't have to figure out how to reproduce 'matches'
    // accurately (it might have been produced by a pattern iterator) in order
    // to generate an array to pass to clink.match_display_filter.

    if (!m_generators)
        return false;

    for (auto *generator : *m_generators)
        if (generator->match_display_filter(needle, matches, filtered_matches, flags, old_filtering))
            return true;

    return false;
}

//------------------------------------------------------------------------------
void matches_impl::reset()
{
    m_store.reset();
    m_infos.clear();
    m_any_infer_type = false;
    m_can_infer_type = true;
    m_coalesced = false;
    m_count = 0;
    m_append_character = '\0';
    m_regen_blocked = false;
    m_suppress_append = false;
    m_suppress_quoting = 0;
    m_word_break_position = -1;
    m_filename_completion_desired.reset();
    m_filename_display_desired.reset();

    s_slash_translation = g_translate_slashes.get();
}

//------------------------------------------------------------------------------
void matches_impl::set_append_character(char append)
{
    m_append_character = append;
}

//------------------------------------------------------------------------------
void matches_impl::set_suppress_append(bool suppress)
{
    m_suppress_append = suppress;
}

//------------------------------------------------------------------------------
void matches_impl::set_suppress_quoting(int suppress)
{
    m_suppress_quoting = suppress;
}

//------------------------------------------------------------------------------
void matches_impl::set_word_break_position(int position)
{
    m_word_break_position = position;
}

//------------------------------------------------------------------------------
void matches_impl::set_regen_blocked()
{
    m_regen_blocked = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_deprecated_mode()
{
    m_can_infer_type = false;
}

//------------------------------------------------------------------------------
void matches_impl::set_matches_are_files(bool files)
{
    m_filename_completion_desired.set_explicit(files);
    m_filename_display_desired.set_explicit(files);
}

//------------------------------------------------------------------------------
bool matches_impl::add_match(const match_desc& desc, bool already_normalized)
{
    const char* match = desc.match;
    match_type type = desc.type;

    if (m_coalesced || match == nullptr || !*match)
        return false;

    char* sep = rl_last_path_separator(match);
    bool ends_with_sep = (sep && !sep[1]);

    if (is_match_type(desc.type, match_type::none) && ends_with_sep)
        type |= match_type::dir;

    // Slash translation happens only for dir, file, and none match types.  And
    // only when `clink.slash_translation` is enabled.  already_normalized means
    // desc has already been normalized to system format, and a performance
    // optimization can skip translation if system format is configured.
    int mode = (s_slash_translation &&
                (is_match_type(type, match_type::dir) ||
                 is_match_type(type, match_type::file) ||
                 (is_match_type(type, match_type::none) &&
                  m_filename_completion_desired.get()))) ? s_slash_translation : 0;
    bool translate = (mode > 0 && (mode > 1 || !already_normalized));

    str<280> tmp;
    const bool is_none = is_match_type(type, match_type::none);
    if (is_match_type(type, match_type::dir) && !ends_with_sep)
    {
        // insert_match() relies on Clink always including a trailing path
        // separator on directory matches, so add one if the caller omitted it.
        tmp = match;
        path::append(tmp, "");
        match = tmp.c_str();
    }
    else if (translate || is_none)
    {
        tmp = match;
        match = tmp.c_str();
    }

    if (translate)
    {
        assert(mode > 0);
        int sep;
        switch (mode)
        {
        default:    sep = 0; break;
        case 2:     sep = '/'; break;
        case 3:     sep = '\\'; break;
        }
        path::normalise_separators(tmp, sep);
        match = tmp.c_str();
    }

    if (!m_dedup)
        m_dedup = new match_lookup_unordered_set;

    if (m_dedup->find({ match, type }) != m_dedup->end())
        return false;

    if (is_none)
    {
        // Make room for a trailing path separator in case it's needed later.
        assert(tmp.c_str() == match);
        tmp.concat("#");
        match = tmp.c_str();
    }

    const char* store_match = m_store.store_front(match);
    if (!store_match)
        return false;

    if (is_none)
    {
        // Remove the placeholder character added earlier.  There is now room
        // reserved to add it again later, in done_building(), if needed.
        assert(tmp.length() > 0);
        assert(strcmp(store_match, tmp.c_str()) == 0);
        const_cast<char*>(store_match)[tmp.length() - 1] = '\0';
        m_any_infer_type = true;
    }

    const char* store_display = desc.display && *desc.display ? m_store.store_front(desc.display) : nullptr;
    const char* store_description = desc.description && *desc.description ? m_store.store_front(desc.description) : nullptr;

    match_lookup lookup = { store_match, type };
    m_dedup->emplace(std::move(lookup));

    match_info info = { store_match, store_display, store_description, type, desc.append_display, false/*select*/, is_none/*infer_type*/ };
    m_infos.emplace_back(std::move(info));
    ++m_count;

    return true;
}

//------------------------------------------------------------------------------
void matches_impl::done_building()
{
    // If there were any `none` type matches and file completion has not been
    // explicitly disabled, then it's necessary to post-process the matches to
    // identify which are directories, or files, or neither.
    if (m_any_infer_type && (m_filename_completion_desired.get() ||
                             (m_can_infer_type && !m_filename_completion_desired.is_explicit())))
    {
        char sep = rl_preferred_path_separator;
        if (s_slash_translation == 2)
            sep = '/';
        else if (s_slash_translation == 3)
            sep = '\\';

        for (unsigned int i = m_count; i--;)
        {
            if (m_infos[i].infer_type)
            {
                // If matches are relative, but not relative to the current
                // directory, then get_path_type() might yield unexpected
                // results.  But that will interfere with many things, so no
                // effort is invested here to compensate.
                match_lookup lookup = { m_infos[i].match, m_infos[i].type };
                switch (os::get_path_type(m_infos[i].match))
                {
                case os::path_type_dir:
                    {
                        // Remove it from the dup map before modifying it.
                        m_dedup->erase(lookup);
                        // It's a directory, so update the type and add a
                        // trailing path separator.
                        const size_t len = strlen(m_infos[i].match);
                        const_cast<char*>(m_infos[i].match)[len] = sep;
                        m_infos[i].type |= match_type::dir;
                        assert(m_infos[i].match[len + 1] == '\0');
                    }
                    break;
                case os::path_type_file:
                    {
                        // Remove it from the dup map before modifying it.
                        m_dedup->erase(lookup);
                        // It's a file, so update the type.
                        lookup.type |= match_type::file;
                        m_infos[i].type |= lookup.type;
                    }
                    break;
                default:
                    continue;
                }

                // Check if it has become a duplicate.
                if (m_dedup->find(lookup) != m_dedup->end())
                    m_infos.erase(m_infos.begin() + i);
                else
                    m_dedup->emplace(std::move(lookup));
            }
        }
    }

    delete m_dedup;
    m_dedup = nullptr;
}

//------------------------------------------------------------------------------
void matches_impl::coalesce(unsigned int count_hint, bool restrict)
{
    match_info* infos = get_infos();

    bool any_pathish = false;
    bool all_pathish = true;

    unsigned int j = 0;
    for (unsigned int i = 0, n = m_infos.size(); i < n && j < count_hint; ++i)
    {
        if (!infos[i].select)
            continue;

        if (is_pathish(infos[i].type))
            any_pathish = true;
        else
            all_pathish = false;

        if (i != j)
        {
            match_info temp = infos[j];
            infos[j] = infos[i];
            infos[i] = temp;
        }
        ++j;
    }

    m_filename_completion_desired.set_implicit(any_pathish);
    m_filename_display_desired.set_implicit(any_pathish && all_pathish);

    m_count = j;
    m_coalesced = true;

    if (restrict)
        m_infos.resize(j);
}
