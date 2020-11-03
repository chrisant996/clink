// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches_impl.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <sys/stat.h>
#include <readline/readline.h> // for rl_last_path_separator

//------------------------------------------------------------------------------
match_type to_match_type(int mode, int attr)
{
    static_assert(int(match_type::none) == MATCH_TYPE_NONE, "match_type enum must match readline constants");
    static_assert(int(match_type::word) == MATCH_TYPE_WORD, "match_type enum must match readline constants");
    static_assert(int(match_type::alias) == MATCH_TYPE_ALIAS, "match_type enum must match readline constants");
    static_assert(int(match_type::file) == MATCH_TYPE_FILE, "match_type enum must match readline constants");
    static_assert(int(match_type::dir) == MATCH_TYPE_DIR, "match_type enum must match readline constants");
    static_assert(int(match_type::link) == MATCH_TYPE_LINK, "match_type enum must match readline constants");
    static_assert(int(match_type::mask) == MATCH_TYPE_MASK, "match_type enum must match readline constants");
    static_assert(int(match_type::hidden) == MATCH_TYPE_HIDDEN, "match_type enum must match readline constants");
    static_assert(int(match_type::readonly) == MATCH_TYPE_READONLY, "match_type enum must match readline constants");

    match_type type;

    if (mode & _S_IFDIR)
        type = match_type::dir;
#ifdef _S_IFLNK
    else if (mode & _S_IFLNK)
        type = match_type::link;
#else
    else if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
        type = match_type::link;
#endif
    else
        type = match_type::file;

    if (attr & FILE_ATTRIBUTE_HIDDEN)
        type |= match_type::hidden;
    if (attr & FILE_ATTRIBUTE_READONLY)
        type |= match_type::readonly;

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
        else if (_strnicmp(t, "alias", l) == 0)
            type = (type & ~match_type::mask) | match_type::alias;
        else if (_strnicmp(t, "file", l) == 0)
            type = (type & ~match_type::mask) | match_type::file;
        else if (_strnicmp(t, "dir", l) == 0)
            type = (type & ~match_type::mask) | match_type::dir;
        else if (_strnicmp(t, "link", l) == 0 ||
                 _strnicmp(t, "symlink", l) == 0)
            type = (type & ~match_type::mask) | match_type::link;
        else if (_strnicmp(t, "hidden", l) == 0)
            type |= match_type::hidden;
        else if (_strnicmp(t, "readonly", l) == 0)
            type |= match_type::readonly;
    }

    return type;
}

//------------------------------------------------------------------------------
match_builder::match_builder(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const char* match, match_type type)
{
    char suffix = 0;
    match_desc desc = {
        match,
        type
    };
    return add_match(desc);
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const match_desc& desc)
{
    return ((matches_impl&)m_matches).add_match(desc);
}

//------------------------------------------------------------------------------
void match_builder::set_append_character(char append)
{
    return ((matches_impl&)m_matches).set_append_character(append);
}

//------------------------------------------------------------------------------
void match_builder::set_prefix_included(bool included)
{
    return ((matches_impl&)m_matches).set_prefix_included(included);
}

//------------------------------------------------------------------------------
void match_builder::set_prefix_included(int amount)
{
    return ((matches_impl&)m_matches).set_prefix_included(amount);
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
matches_impl::matches_impl(unsigned int store_size)
: m_store(min(store_size, 0x10000u))
{
    m_infos.reserve(1024);
}

//------------------------------------------------------------------------------
unsigned int matches_impl::get_info_count() const
{
    return int(m_infos.size());
}

//------------------------------------------------------------------------------
match_info* matches_impl::get_infos()
{
    return &(m_infos[0]);
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
bool matches_impl::is_suppress_append() const
{
    return m_suppress_append;
}

//------------------------------------------------------------------------------
bool matches_impl::is_prefix_included() const
{
    return m_prefix_included;
}

//------------------------------------------------------------------------------
int matches_impl::get_prefix_excluded() const
{
    return m_prefix_excluded;
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
void matches_impl::reset()
{
    m_store.reset();
    m_infos.clear();
    m_coalesced = false;
    m_count = 0;
    m_prefix_included = false;
    m_prefix_excluded = 0;
    m_suppress_append = false;
    m_suppress_quoting = 0;
}

//------------------------------------------------------------------------------
void matches_impl::set_append_character(char append)
{
    m_append_character = append;
}

//------------------------------------------------------------------------------
void matches_impl::set_prefix_included(bool included)
{
    m_prefix_included = included;
    m_prefix_excluded = 0;
}

//------------------------------------------------------------------------------
void matches_impl::set_prefix_included(int amount)
{
    m_prefix_included = amount < 0;
    m_prefix_excluded = 0 - amount;
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
bool matches_impl::add_match(const match_desc& desc)
{
    const char* match = desc.match;
    match_type type = desc.type;

    if (m_coalesced || match == nullptr || !*match)
        return false;

    if (desc.type == match_type::none)
    {
        char* sep = rl_last_path_separator(match);
        if (sep && !sep[1])
            type = match_type::dir;
    }

    const char* store_match = m_store.store_front(match);
    if (!store_match)
        return false;

    m_infos.push_back({ store_match, type });
    ++m_count;
    return true;
}

//------------------------------------------------------------------------------
void matches_impl::coalesce(unsigned int count_hint)
{
    match_info* infos = &(m_infos[0]);

    unsigned int j = 0;
    for (int i = 0, n = int(m_infos.size()); i < n && j < count_hint; ++i)
    {
        if (!infos[i].select)
            continue;

        if (i != j)
        {
            match_info temp = infos[j];
            infos[j] = infos[i];
            infos[i] = temp;
        }
        ++j;
    }

    m_count = j;
    m_coalesced = true;
}
