// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "matches_impl.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <sys/stat.h>
#include <readline/readline.h> // for rl_last_path_separator

//------------------------------------------------------------------------------
match_type to_match_type(int mode)
{
    static_assert(int(match_type::none) == MATCH_TYPE_NONE, "match_type enum must match readline constants");
    static_assert(int(match_type::word) == MATCH_TYPE_WORD, "match_type enum must match readline constants");
    static_assert(int(match_type::file) == MATCH_TYPE_FILE, "match_type enum must match readline constants");
    static_assert(int(match_type::dir) == MATCH_TYPE_DIR, "match_type enum must match readline constants");
    static_assert(int(match_type::link) == MATCH_TYPE_LINK, "match_type enum must match readline constants");

    if (mode & _S_IFDIR)
        return match_type::dir;
#ifdef _S_IFLNK
    else if (mode & _S_IFLNK)
        return match_type::link;
#endif
    else
        return match_type::file;
}

//------------------------------------------------------------------------------
match_builder::match_builder(matches& matches)
: m_matches(matches)
{
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const char* match, match_type type)
{
    match_desc desc = { match, nullptr, nullptr, 0, type };
    return add_match(desc);
}

//------------------------------------------------------------------------------
bool match_builder::add_match(const match_desc& desc)
{
    return ((matches_impl&)m_matches).add_match(desc);
}

//------------------------------------------------------------------------------
void match_builder::set_prefix_included(bool included)
{
    return ((matches_impl&)m_matches).set_prefix_included(included);
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
const char* matches_impl::get_displayable(unsigned int index) const
{
    if (index >= get_match_count())
        return nullptr;

    const char* displayable = m_infos[index].displayable;
    return displayable ? displayable : m_infos[index].match;
}

//------------------------------------------------------------------------------
const char* matches_impl::get_aux(unsigned int index) const
{
    if (index >= get_match_count())
        return nullptr;

    return m_infos[index].aux;
}

//------------------------------------------------------------------------------
char matches_impl::get_suffix(unsigned int index) const
{
    if (index >= get_match_count())
        return 0;

    return m_infos[index].suffix;
}

//------------------------------------------------------------------------------
match_type matches_impl::get_match_type(unsigned int index) const
{
    if (index >= get_match_count())
        return match_type::none;

    return m_infos[index].type;
}

//------------------------------------------------------------------------------
unsigned int matches_impl::get_cell_count(unsigned int index) const
{
    return (index < get_match_count()) ? m_infos[index].cell_count : 0;
}

//------------------------------------------------------------------------------
bool matches_impl::has_aux() const
{
    return m_has_aux;
}

//------------------------------------------------------------------------------
void matches_impl::get_match_lcd(str_base& out) const
{
    int match_count = get_match_count();

    if (match_count <= 0)
        return;

    if (match_count == 1)
    {
        out = get_match(0);
        return;
    }

    out = get_match(0);
    int lcd_length = out.length();

    int cmp_mode = str_compare_scope::current();
    str_compare_scope _(min(cmp_mode, int(str_compare_scope::caseless)));

    for (int i = 1, n = get_match_count(); i < n; ++i)
    {
        const char* match = get_match(i);
        int d = str_compare(match, out.c_str());
        if (d >= 0)
            lcd_length = min(d, lcd_length);
    }

    out.truncate(lcd_length);
}

//------------------------------------------------------------------------------
bool matches_impl::is_prefix_included() const
{
    return m_prefix_included;
}

//------------------------------------------------------------------------------
void matches_impl::reset()
{
    m_store.reset();
    m_infos.clear();
    m_coalesced = false;
    m_count = 0;
    m_has_aux = false;
    m_prefix_included = false;
}

//------------------------------------------------------------------------------
void matches_impl::set_prefix_included(bool included)
{
    m_prefix_included = included;
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

    const char* store_displayable = nullptr;
    if (desc.displayable != nullptr)
        store_displayable = m_store.store_back(desc.displayable);

    const char* store_aux = nullptr;
    if (m_has_aux = (desc.aux != nullptr))
        store_aux = m_store.store_back(desc.aux);

    m_infos.push_back({
        store_match,
        store_displayable,
        store_aux,
        0,
        type,
        max<unsigned char>(0, desc.suffix),
    });
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
