// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches_impl.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "slash_translation.h"

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
char* __printable_part(char* text);
};

#include <assert.h>

//------------------------------------------------------------------------------
static int32 s_slash_translation = slash_translation::off;
void set_slash_translation(int32 mode) { s_slash_translation = mode; }
int32 get_slash_translation() { return s_slash_translation; }

//------------------------------------------------------------------------------
setting_enum g_translate_slashes(
    "match.translate_slashes",
    "Translate slashes and backslashes",
    "File and directory completions can be translated to use consistent slashes.\n"
    "The default is 'auto' which translates all slashes in the completed word to\n"
    "match the first kind of slash in the word (or the system path separator if\n"
    "the word didn't have any slashes before being completed).  Use 'slash' for\n"
    "forward slashes, 'backslash' for backslashes, or 'system' for the appropriate\n"
    "path separator for the OS host (backslashes on Windows).  Use 'off' to turn\n"
    "off translating slashes.",
    "off,system,slash,backslash,auto", // IMPORTANT: Keep this in sync with slash_translation.h!
    slash_translation::automatic
);

static setting_bool g_substring(
    "match.substring",
    "Try substring if no prefix matches",
    "When set, if no completions are found with a prefix search, then a substring\n"
    "search is used.",
    false
);

extern setting_bool g_match_wild;
extern setting_enum g_default_bindings;



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
match_type to_match_type(DWORD attr, const char* path, bool symlink)
{
    static_assert(int32(match_type::none) == MATCH_TYPE_NONE, "match_type enum must match readline constants");
    static_assert(int32(match_type::word) == MATCH_TYPE_WORD, "match_type enum must match readline constants");
    static_assert(int32(match_type::arg) == MATCH_TYPE_ARG, "match_type enum must match readline constants");
    static_assert(int32(match_type::cmd) == MATCH_TYPE_COMMAND, "match_type enum must match readline constants");
    static_assert(int32(match_type::alias) == MATCH_TYPE_ALIAS, "match_type enum must match readline constants");
    static_assert(int32(match_type::file) == MATCH_TYPE_FILE, "match_type enum must match readline constants");
    static_assert(int32(match_type::dir) == MATCH_TYPE_DIR, "match_type enum must match readline constants");
    static_assert(int32(match_type::mask) == MATCH_TYPE_MASK, "match_type enum must match readline constants");
    static_assert(int32(match_type::link) == MATCH_TYPE_LINK, "match_type enum must match readline constants");
    static_assert(int32(match_type::orphaned) == MATCH_TYPE_ORPHANED, "match_type enum must match readline constants");
    static_assert(int32(match_type::hidden) == MATCH_TYPE_HIDDEN, "match_type enum must match readline constants");
    static_assert(int32(match_type::readonly) == MATCH_TYPE_READONLY, "match_type enum must match readline constants");
    static_assert(int32(match_type::system) == MATCH_TYPE_SYSTEM, "match_type enum must match readline constants");

    match_type type;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        type = match_type::dir;
    else
        type = match_type::file;

    if (attr & FILE_ATTRIBUTE_HIDDEN)
        type |= match_type::hidden;
    if (attr & FILE_ATTRIBUTE_SYSTEM)
        type |= match_type::system;
    if (attr & FILE_ATTRIBUTE_READONLY)
        type |= match_type::readonly;

    if (symlink)
    {
        wstr<288> wfile(path);
        struct _stat64 st;
        if (_wstat64(wfile.c_str(), &st) < 0)
            type |= match_type::orphaned;
        type |= match_type::link;
    }

    return type;
}

//------------------------------------------------------------------------------
match_type backcompat_match_type(const char* path)
{
    bool symlink;
    const DWORD attr = os::get_file_attributes(path, &symlink);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return match_type::file;

    return to_match_type(attr, path, symlink);
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
        int32 l = token.length();

        // Trim whitespace.
        while (l && isspace(uint8(*t)))
            t++, l--;
        while (l && isspace(uint8(t[l-1])))
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
        else if (_strnicmp(t, "system", l) == 0)
            type |= match_type::system;
        else if (_strnicmp(t, "readonly", l) == 0)
            type |= match_type::readonly;
        else if (_strnicmp(t, "orphaned", l) == 0)
            type |= match_type::orphaned;
    }

    // Only files and dirs can be links.
    if (int32(type & match_type::link) && !is_match_type(type, match_type::file) && !is_match_type(type, match_type::dir))
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
    out.concat(type_names[int32(base)]);

    if (int32(type & match_type::link))
        out.concat(",link");
    if (int32(type & match_type::orphaned))
        out.concat(",orphaned");
    if (int32(type & match_type::hidden))
        out.concat(",hidden");
    if (int32(type & match_type::system))
        out.concat(",system");
    if (int32(type & match_type::readonly))
        out.concat(",readonly");
}



//------------------------------------------------------------------------------
match_desc::match_desc(const char* match, const char* display, const char* description, match_type type)
: match(match)
, display(display)
, description(description)
, type(type)
{
    append_char = 0;
    suppress_append = -1;
    append_display = false;
    missing_match = false;

    // Do not append a space after an arg type match that ends with a colon or
    // equal sign, because programs typically require flags and args like
    // "--foo=" or "foo=" to have no space after the ":" or "=" symbol.
    if (match && is_match_type(type, match_type::arg))
    {
        const size_t len = strlen(match);
        if (len)
        {
            const char c = match[len - 1];
            if (c == ':' || c == '=')
                suppress_append = true;
        }
    }
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
    match_desc desc(match, nullptr, nullptr, type);
    return add_match(desc, already_normalised);
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const match_desc& desc, bool already_normalized)
{
    return ((matches_impl&)m_matches).add_match(desc, already_normalized);
}

//------------------------------------------------------------------------------
bool match_builder::is_empty()
{
    return ((matches_impl&)m_matches).get_match_count() == 0;
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
void match_builder::set_suppress_quoting(int32 suppress)
{
    return ((matches_impl&)m_matches).set_suppress_quoting(suppress);
}

//------------------------------------------------------------------------------
void match_builder::set_force_quoting()
{
    return ((matches_impl&)m_matches).set_force_quoting();
}

//------------------------------------------------------------------------------
void match_builder::set_fully_qualify(bool fully_qualify)
{
    return ((matches_impl&)m_matches).set_fully_qualify(fully_qualify);
}

//------------------------------------------------------------------------------
void match_builder::set_no_sort()
{
    return ((matches_impl&)m_matches).set_no_sort();
}

//------------------------------------------------------------------------------
void match_builder::set_has_descriptions()
{
    return ((matches_impl&)m_matches).set_has_descriptions();
}

//------------------------------------------------------------------------------
void match_builder::set_volatile()
{
    return ((matches_impl&)m_matches).set_volatile();
}

//------------------------------------------------------------------------------
void match_builder::set_input_line(const char* text)
{
    return ((matches_impl&)m_matches).set_input_line(text);
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
class match_builder_toolkit_impl : public match_builder_toolkit
{
public:
                            match_builder_toolkit_impl(int32 generation_id, uint32 end_word_offset);
                            ~match_builder_toolkit_impl();
    int32                   get_generation_id() const override { return m_generation_id; }
    matches*                get_matches() const override { return m_matches; }
    match_builder*          get_builder() const override { return m_builder; }
    void                    clear() override;

private:
    const int32             m_generation_id;
    matches_impl*           m_matches;
    match_builder*          m_builder;
};

//------------------------------------------------------------------------------
match_builder_toolkit_impl::match_builder_toolkit_impl(int32 generation_id, uint32 end_word_offset)
: m_generation_id(generation_id)
{
    matches_impl* matches = new matches_impl();
    matches->set_word_break_position(end_word_offset);

    m_matches = matches;
    m_builder = new match_builder(*matches);
}

//------------------------------------------------------------------------------
match_builder_toolkit_impl::~match_builder_toolkit_impl()
{
    delete m_matches;
    delete m_builder;
}

//------------------------------------------------------------------------------
void match_builder_toolkit_impl::clear()
{
    static_cast<matches_impl*>(m_matches)->clear();
}

//------------------------------------------------------------------------------
std::shared_ptr<match_builder_toolkit> make_match_builder_toolkit(int32 generation_id, uint32 end_word_offset)
{
    return std::make_shared<match_builder_toolkit_impl>(generation_id, end_word_offset);
}



//------------------------------------------------------------------------------
static char* __tilde_expand(const char* in)
{
    str_moveable tmp;
    path::tilde_expand(in, tmp);
    return tmp.detach();
}

//------------------------------------------------------------------------------
matches_iter::matches_iter(const matches& matches, const char* pattern)
: m_matches(matches)
, m_expanded_pattern(pattern && rl_complete_with_tilde_expansion ? __tilde_expand(pattern) : nullptr)
, m_pattern((m_expanded_pattern ? m_expanded_pattern : pattern),
            (m_expanded_pattern ? m_expanded_pattern : pattern) ? -1 : 0)
, m_has_pattern(pattern != nullptr)
, m_can_try_substring(can_try_substring_pattern(pattern))
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
        const bool dot_prefix = (rl_completion_type == '%' && g_default_bindings.get() == 1);

        while (true)
        {
            m_index = m_next;
            m_next++;

            const char* match = get_match();
            if (!match)
            {
                m_next--;
                if (try_substring())
                    continue;
                return false;
            }

            int32 match_len = int32(strlen(match));
            while (match_len && path::is_separator(uint8(match[match_len - 1])))
                match_len--;

            const path::star_matches_everything flag = is_pathish(get_match_type()) ? path::at_end : path::yes;
            if (path::match_wild(m_pattern, str_iter(match, match_len), dot_prefix, flag))
                goto found;
        }
    }

    m_index = m_next;
    if (m_index >= m_matches.get_match_count())
        return false;
    m_next++;

found:
    m_can_try_substring = false;
    if (is_pathish(get_match_type()))
        m_any_pathish = true;
    else
        m_all_pathish = false;
    return true;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match() const
{
    return has_match() ? m_matches.get_match(m_index) : nullptr;
}

//------------------------------------------------------------------------------
match_type matches_iter::get_match_type() const
{
    return has_match() ? m_matches.get_match_type(m_index) : match_type::none;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match_display() const
{
    return has_match() ? m_matches.get_match_display(m_index) : nullptr;
}

//------------------------------------------------------------------------------
const char* matches_iter::get_match_description() const
{
    return has_match() ? m_matches.get_match_description(m_index) : nullptr;
}

//------------------------------------------------------------------------------
char matches_iter::get_match_append_char() const
{
    return has_match() ? m_matches.get_match_append_char(m_index) : 0;
}

//------------------------------------------------------------------------------
shadow_bool matches_iter::get_match_suppress_append() const
{
    return has_match() ? m_matches.get_match_suppress_append(m_index) : shadow_bool(false);
}

//------------------------------------------------------------------------------
bool matches_iter::get_match_append_display() const
{
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
bool matches_iter::try_substring()
{
    if (!m_can_try_substring)
        return false;

    m_can_try_substring = false;

    char* pattern = make_substring_pattern(m_pattern.get_pointer());
    if (!pattern)
        return false;

    free(m_expanded_pattern);
    m_expanded_pattern = pattern;
    m_pattern = str_iter(m_expanded_pattern, -1);
    m_index = 0;
    m_next = 0;
    return true;
}



//------------------------------------------------------------------------------
matches_impl::store_impl::store_impl(uint32 size)
: linear_allocator(max<uint32>(4096, size))
{
}



//------------------------------------------------------------------------------
matches_impl::matches_impl(uint32 store_size)
: m_store(min(store_size, 0x10000u))
, m_filename_completion_desired(false)
, m_filename_display_desired(false)
{
}

//------------------------------------------------------------------------------
matches_impl::~matches_impl()
{
    delete m_dedup;
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
uint32 matches_impl::get_info_count() const
{
    return int32(m_infos.size());
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
void matches_impl::get_lcd(str_base& out) const
{
    for (uint32 i = 0; i < m_count; i++)
    {
        const char *match = m_infos[i].match;
        if (!i)
        {
            out = match;
        }
        else
        {
            int32 matching = str_compare<char, true/*compute_lcd*/>(out.c_str(), match);
            out.truncate(matching);
        }
    }
}

//------------------------------------------------------------------------------
uint32 matches_impl::get_match_count() const
{
    return m_count;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match(uint32 index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].match;
}

//------------------------------------------------------------------------------
match_type matches_impl::get_match_type(uint32 index) const
{
    if (index >= get_match_count())
        return match_type::none;

    return m_infos[index].type;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match_display(uint32 index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].display;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_match_description(uint32 index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].description;
}

//------------------------------------------------------------------------------
uint32 matches_impl::get_match_ordinal(uint32 index) const
{
    if (index >= get_match_count())
        return 0;

    return m_infos[index].ordinal;
}

//------------------------------------------------------------------------------
char matches_impl::get_match_append_char(uint32 index) const
{
    if (index >= get_match_count())
        return 0;

    return m_infos[index].append_char;
}

//------------------------------------------------------------------------------
shadow_bool matches_impl::get_match_suppress_append(uint32 index) const
{
    shadow_bool tmp(false);
    if (index < get_match_count())
    {
        char suppress = m_infos[index].suppress_append;
        if (suppress >= 0)
            tmp.set_explicit(suppress);
    }
    return tmp;
}

//------------------------------------------------------------------------------
bool matches_impl::get_match_append_display(uint32 index) const
{
    if (index >= get_match_count())
        return false;

    return m_infos[index].append_display;
}

//------------------------------------------------------------------------------
bool matches_impl::get_match_custom_display(uint32 index) const
{
    auto& info = m_infos[index];
    if (info.custom_display < 0)
    {
        const char* match = info.match;
        if (!is_match_type(info.type, match_type::none))
            match = __printable_part(const_cast<char*>(match));
        return (strcmp(match, info.display) != 0);
    }
    return info.custom_display > 0;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match(uint32 index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].match;
}

//------------------------------------------------------------------------------
match_type matches_impl::get_unfiltered_match_type(uint32 index) const
{
    if (index >= get_info_count())
        return match_type::none;

    return m_infos[index].type;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match_display(uint32 index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].display;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_unfiltered_match_description(uint32 index) const
{
    if (index >= get_info_count())
        return nullptr;

    return m_infos[index].description;
}

//------------------------------------------------------------------------------
char matches_impl::get_unfiltered_match_append_char(uint32 index) const
{
    if (index >= get_info_count())
        return 0;

    return m_infos[index].append_char;
}

//------------------------------------------------------------------------------
shadow_bool matches_impl::get_unfiltered_match_suppress_append(uint32 index) const
{
    shadow_bool tmp(false);
    if (index < get_info_count())
    {
        char suppress = m_infos[index].suppress_append;
        if (suppress >= 0)
            tmp.set_explicit(suppress);
    }
    return tmp;
}

//------------------------------------------------------------------------------
bool matches_impl::get_unfiltered_match_append_display(uint32 index) const
{
    if (index >= get_info_count())
        return false;

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
bool matches_impl::is_fully_qualify() const
{
    return m_fully_qualify;
}

//------------------------------------------------------------------------------
char matches_impl::get_append_character() const
{
    return m_append_character;
}

//------------------------------------------------------------------------------
int32 matches_impl::get_suppress_quoting() const
{
    return m_suppress_quoting;
}

//------------------------------------------------------------------------------
bool matches_impl::get_force_quoting() const
{
    return m_force_quoting;
}

//------------------------------------------------------------------------------
int32 matches_impl::get_word_break_position() const
{
    return m_word_break_position;
}

//------------------------------------------------------------------------------
bool matches_impl::has_descriptions() const
{
    return m_has_descriptions;
}

//------------------------------------------------------------------------------
bool matches_impl::is_volatile() const
{
    return m_volatile;
}

//------------------------------------------------------------------------------
bool matches_impl::match_display_filter(const char* needle, char** matches, ::matches* out, display_filter_flags flags, bool* old_filtering) const
{
    // TODO:  This doesn't really belong here.  But it's a convenient point to
    // cobble together Lua (via the generators) and the matches.  It's strange
    // to pass 'matches' into matches_impl, but the caller already has it and
    // this way we don't have to figure out how to reproduce 'matches'
    // accurately (it might have been produced by a pattern iterator) in order
    // to generate an array to pass to clink.match_display_filter.

    bool ret = false;
    if (m_generator)
    {
        match_builder* builder = out ? new match_builder(*out) : nullptr;
        ret = m_generator->match_display_filter(needle, matches, builder, flags, m_nosort, old_filtering);
        if (out)
        {
            match_pipeline pipeline(*(matches_impl*)out);
            pipeline.select(needle);
            // match display filtering uses the provided order; no sorting.
        }
        delete builder;
    }
    return ret;
}

//------------------------------------------------------------------------------
bool matches_impl::filter_matches(char** matches, char completion_type, bool filename_completion_desired) const
{
    // TODO:  This doesn't really belong here.  But it's a convenient point to
    // cobble together Lua (via the generators) and the matches.  It's strange
    // to pass 'matches' into matches_impl, but the caller already has it and
    // this way we don't have to figure out how to reproduce 'matches'
    // accurately (it might have been produced by a pattern iterator) in order
    // to generate an array to pass to onfiltermatches event callbacks.

    return m_generator && m_generator->filter_matches(matches, completion_type, filename_completion_desired);
}

//------------------------------------------------------------------------------
void matches_impl::reset()
{
    delete m_dedup;
    m_dedup = nullptr;

    m_store.reset();
    m_infos.clear();
    m_count = 0;
    m_any_none_type = false;
    m_deprecated_mode = false;
    m_coalesced = false;
    m_append_character = '\0';
    m_suppress_append = false;
    m_has_descriptions = false;
    m_fully_qualify = false;
    m_force_quoting = false;
    m_regen_blocked = false;
    m_nosort = false;
    m_volatile = false;
    m_sep = '\0';
    m_suppress_quoting = 0;
    m_word_break_position = -1;
    m_filename_completion_desired.reset();
    m_filename_display_desired.reset();
    m_input_line.clear();

    s_slash_translation = g_translate_slashes.get();
}

//------------------------------------------------------------------------------
void matches_impl::transfer(matches_impl& from)
{
    // Do not transfer m_generator; it is consumer configuration, not part of
    // the matches state.

    m_store = std::move(from.m_store);
    m_infos = std::move(from.m_infos);
    m_count = from.m_count;
    m_any_none_type = from.m_any_none_type;
    m_deprecated_mode = from.m_deprecated_mode;
    m_coalesced = from.m_coalesced;
    m_append_character = from.m_append_character;
    m_suppress_append = from.m_suppress_append;
    m_has_descriptions = from.m_has_descriptions;
    m_fully_qualify = from.m_fully_qualify;
    m_force_quoting = from.m_force_quoting;
    m_regen_blocked = from.m_regen_blocked;
    m_nosort = from.m_nosort;
    m_volatile = from.m_volatile;
    m_sep = from.m_sep;
    m_suppress_quoting = from.m_suppress_quoting;
    m_word_break_position = from.m_word_break_position;
    m_filename_completion_desired = from.m_filename_completion_desired;
    m_filename_display_desired = from.m_filename_display_desired;
    m_input_line = std::move(from.m_input_line);

    m_dedup = from.m_dedup;

    from.m_dedup = nullptr;
    from.clear();
}

//------------------------------------------------------------------------------
void matches_impl::copy(const matches_impl& from)
{
    clear();

    for (const auto& info : from.m_infos)
    {
        match_info add;
        add.match = info.match ? m_store.store_front(info.match) : nullptr;
        add.display = info.display ? m_store.store_front(info.display) : nullptr;
        add.description = info.description ? m_store.store_front(info.description) : nullptr;
        add.ordinal = m_infos.size();
        add.type = info.type;
        add.append_char = info.append_char;
        add.suppress_append = info.suppress_append;
        add.append_display = info.append_display;
        add.custom_display = info.custom_display;
        add.select = false; // (Shouldn't matter.)
        m_infos.emplace_back(std::move(add));
    }

    m_count = from.m_count;
    m_any_none_type = from.m_any_none_type;
    m_deprecated_mode = from.m_deprecated_mode;
    m_coalesced = from.m_coalesced;
    m_append_character = from.m_append_character;
    m_suppress_append = from.m_suppress_append;
    m_has_descriptions = from.m_has_descriptions;
    m_fully_qualify = from.m_fully_qualify;
    m_force_quoting = from.m_force_quoting;
    m_regen_blocked = from.m_regen_blocked;
    m_nosort = from.m_nosort;
    m_volatile = from.m_volatile;
    m_sep = from.m_sep;
    m_suppress_quoting = from.m_suppress_quoting;
    m_word_break_position = from.m_word_break_position;
    m_filename_completion_desired = from.m_filename_completion_desired;
    m_filename_display_desired = from.m_filename_display_desired;
    m_input_line << from.m_input_line;
}

//------------------------------------------------------------------------------
void matches_impl::clear()
{
    reset();
    m_store.clear();
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
void matches_impl::set_suppress_quoting(int32 suppress)
{
    m_suppress_quoting = suppress;
    m_force_quoting = false;
}

//------------------------------------------------------------------------------
void matches_impl::set_force_quoting()
{
    m_suppress_quoting = 0;
    m_force_quoting = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_fully_qualify(bool fully_qualify)
{
    m_fully_qualify = fully_qualify;
}

//------------------------------------------------------------------------------
void matches_impl::set_word_break_position(int32 position)
{
    m_word_break_position = position;
}

//------------------------------------------------------------------------------
void matches_impl::set_path_separator(char sep)
{
    m_sep = sep;
}

//------------------------------------------------------------------------------
void matches_impl::set_regen_blocked()
{
    m_regen_blocked = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_deprecated_mode()
{
    m_deprecated_mode = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_matches_are_files(bool files)
{
    m_filename_completion_desired.set_explicit(files);
    m_filename_display_desired.set_explicit(files);
}

//------------------------------------------------------------------------------
void matches_impl::set_no_sort()
{
    m_nosort = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_has_descriptions()
{
    m_has_descriptions = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_volatile()
{
    m_volatile = true;
}

//------------------------------------------------------------------------------
void matches_impl::set_input_line(const char* text)
{
    m_input_line = text;
}

//------------------------------------------------------------------------------
bool matches_impl::is_from_current_input_line()
{
    const bool diff = (m_input_line.length() != rl_end ||
                       strncmp(m_input_line.c_str(), rl_line_buffer, rl_end) != 0);
    return !diff;
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
    int32 mode = (s_slash_translation &&
                (is_match_type(type, match_type::dir) ||
                 is_match_type(type, match_type::file) ||
                 (is_match_type(type, match_type::none) &&
                  m_filename_completion_desired.get()))) ? s_slash_translation : slash_translation::off;
    bool translate = (mode > slash_translation::off && (mode > slash_translation::system || !already_normalized));

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
        assert(mode > slash_translation::off);
        int32 sep;
        switch (mode)
        {
        default:                            sep = 0; break;
        case slash_translation::slash:      sep = '/'; break;
        case slash_translation::backslash:  sep = '\\'; break;
        case slash_translation::automatic:  sep = m_sep; break;
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
        if (is_none)
            m_any_none_type = true;
    }

    const char* store_display = (desc.display && *desc.display) ? m_store.store_front(desc.display) : nullptr;
    const char* store_description = (desc.description && *desc.description) ? m_store.store_front(desc.description) : nullptr;
    bool append_display = (desc.append_display && store_display);

    match_lookup lookup = { store_match, type };
    m_dedup->emplace(std::move(lookup));

    match_info info;
    info.match = store_match;
    info.display = store_display;
    info.description = store_description;
    info.ordinal = uint32(m_infos.size());
    info.type = type;
    info.append_char = desc.append_char;
    info.suppress_append = desc.suppress_append;
    info.append_display = append_display;
    info.custom_display = (desc.missing_match ? true : (store_display ? -1 : false));
    info.select = false;
    m_infos.emplace_back(std::move(info));
    ++m_count;

    if (store_description)
        m_has_descriptions = true;

    return true;
}

//------------------------------------------------------------------------------
void matches_impl::set_generator(match_generator* generator)
{
    m_generator = generator;
}

//------------------------------------------------------------------------------
void matches_impl::done_building()
{
    // If there were any `none` type matches and file completion has not been
    // explicitly disabled, then it's necessary to post-process the matches to
    // identify which are directories, or files, or neither.
    if (m_any_none_type && (m_deprecated_mode ?
                            (m_filename_completion_desired.get() && m_filename_completion_desired.is_explicit()) :
                            (m_filename_completion_desired.get() || !m_filename_completion_desired.is_explicit())))
    {
        char sep = rl_preferred_path_separator;
        if (s_slash_translation == slash_translation::slash)
            sep = '/';
        else if (s_slash_translation == slash_translation::backslash)
            sep = '\\';
        else if (s_slash_translation == slash_translation::automatic)
            sep = m_sep;

        for (uint32 i = m_count; i--;)
        {
            if (is_match_type(m_infos[i].type, match_type::none))
            {
                // If matches are relative, but not relative to the current
                // directory, then get_path_type() might yield unexpected
                // results.  But that will interfere with many things, so no
                // effort is invested here to compensate.
                match_lookup lookup = { m_infos[i].match, m_infos[i].type };

                // Remove it from the dup map before modifying it.
                m_dedup->erase(lookup);

                // Apply backward compatibility logic to the match type.
                lookup.type = backcompat_match_type(lookup.match);
                m_infos[i].type = lookup.type;

                // If it's a directory, add a trailing path separator.
                if (is_match_type(lookup.type, match_type::dir))
                {
                    const size_t len = strlen(m_infos[i].match);
                    const_cast<char*>(m_infos[i].match)[len] = sep;
                    assert(m_infos[i].match[len + 1] == '\0');
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
void matches_impl::coalesce(uint32 count_hint, bool restrict)
{
    match_info* infos = get_infos();

    bool any_pathish = false;
    bool all_pathish = true;

    uint32 j = 0;
    for (uint32 i = 0, n = m_infos.size(); i < n && j < count_hint; ++i)
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

//------------------------------------------------------------------------------
bool can_try_substring_pattern(const char* pattern)
{
    // Can try substring when no prefix matches, unless:
    //  - No pattern.
    //  - Setting 'match.substring' is off.
    //  - Pattern already starts with '*'.
    //  - Pattern starts with '~'.
    //  - Setting 'match.wild' is off and:
    //      - Pattern contains '?'.
    //      - Pattern contains '*' (unless part of a run at the end).
    if (pattern && *pattern && !strchr("~*", *pattern) && g_substring.get())
    {
        if (!g_match_wild.get())
        {
            for (const char* walk = pattern; *walk; ++walk)
                if (*walk == '?' || (*walk == '*' && walk[1] && walk[1] != '*'))
                    return false;
        }
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
char* make_substring_pattern(const char* pattern, const char* append)
{
    const char* first = pattern;
    const char* last = rl_last_path_separator(first);
    if (last)
        last++;
    else
        last = first;

    str<> tmp;
    tmp.concat(pattern, int32(last - first));
    tmp.concat("*", 1);
    tmp.concat(last);
    tmp.concat(append);

    char* make = _rl_savestring(tmp.c_str());
    if (!make || !*make)
    {
        free(make);
        return nullptr;
    }

    return make;
}
