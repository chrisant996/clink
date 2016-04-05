// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_system.h"
#include "line_state.h"
#include "matches.h"

#include <core/globber.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_hash.h>

#include <algorithm>

//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    static class : public match_generator
    {
        virtual bool generate(const line_state& line, matches& out) override
        {
            str<MAX_PATH> buffer;
            line.get_end_word(buffer);
            buffer << "*";

            globber globber(buffer.c_str());
            while (globber.next(buffer, false))
                out.add_match(buffer.c_str());

            return true;
        }
    } instance;

    return instance;
}

//------------------------------------------------------------------------------
match_selector& normal_match_selector()
{
    static class : public match_selector
    {
        virtual unsigned int select(const char* needle, const match_store& store, match_info* infos, int count) override
        {
            int select_count = 0;
            for (int i = 0; i < count; ++i)
            {
                const char* name = store.get(infos[i].store_id);
                int j = str_compare(needle, name);
                infos[i].selected = (j < 0 || !needle[j]);
                ++select_count;
            }

            return select_count;
        }
    } instance;

    return instance;
};

//------------------------------------------------------------------------------
match_sorter& alpha_match_sorter()
{
    static class : public match_sorter
    {
        virtual void sort(const match_store& store, match_info* infos, int count) override
        {
            struct predicate
            {
                predicate(const match_store& store)
                : store(store)
                {
                }
        
                bool operator () (const match_info& lhs, const match_info& rhs)
                {
                    const char* l = store.get(lhs.store_id);
                    const char* r = store.get(rhs.store_id);
                    return (stricmp(l, r) < 0);
                }
        
                const match_store& store;
            };
        
            std::sort(infos, infos + count, predicate(store));
        }
    } instance;

    return instance;
}



//------------------------------------------------------------------------------
match_system::match_system()
{
}

//------------------------------------------------------------------------------
match_system::~match_system()
{
}

//------------------------------------------------------------------------------
bool match_system::add_generator(int priority, match_generator& generator)
{
    item* iter = m_generators.push_back();
    if (iter == nullptr)
        return false;

    --iter;
    while (iter >= m_generators.front())
    {
        if (int(iter->key) < priority)
            break;

        iter[1] = iter[0];
        --iter;
    }

    *(iter + 1) = { &generator, priority };

    return true;
}

//------------------------------------------------------------------------------
bool match_system::add_selector(const char* name, match_selector& selector)
{
    item* item = m_selectors.push_back();
    if (item == nullptr)
        return false;
    
    *item = { &selector, str_hash(name) };
    return true;
}

//------------------------------------------------------------------------------
bool match_system::add_sorter(const char* name, match_sorter& sorter)
{
    item* item = m_sorters.push_back();
    if (item == nullptr)
        return false;
    
    *item = { &sorter, str_hash(name) };
    return true;
}

//------------------------------------------------------------------------------
unsigned int match_system::get_generator_count() const
{
    return m_generators.size();
}

//------------------------------------------------------------------------------
match_generator* match_system::get_generator(unsigned int index) const
{
    if (index < m_generators.size())
        return (match_generator*)(m_generators[index]->ptr);

    return nullptr;
}

//------------------------------------------------------------------------------
match_selector* match_system::get_selector(const char* name) const
{
    unsigned int hash = str_hash(name);
    for (const auto& item : m_selectors)
        if (item.key == hash)
            return (match_selector*)(item.ptr);

    return nullptr;
}

//------------------------------------------------------------------------------
match_sorter* match_system::get_sorter(const char* name) const
{
    unsigned int hash = str_hash(name);
    for (const auto& item : m_sorters)
        if (item.key == hash)
            return (match_sorter*)(item.ptr);

    return nullptr;
}

//------------------------------------------------------------------------------
void match_system::generate_matches(
    const char* line,
    int cursor,
    matches& result) const
{
#if MODE4
    // Find position of an opening quote.
    int quote = INT_MAX;
    for (int i = 0; i < cursor; ++i)
        if (line[i] == '\"')
            quote = (quote == INT_MAX) ? i : INT_MAX;

    // Find where to last word-breaking character is before the cursor.
    int word_break = cursor;
    while (word_break--)
        if (line[word_break] == ' ')
            break;

    // Extract the word.
    int word_start = (word_break < quote) ? word_break : quote;
    ++word_start;

    str<> word;
    word.concat(line + word_start, cursor - word_start);

    // Call each registered match generator until one says it's returned matches
    matches temp_result;
    line_state state = { word.c_str(), line, word_start, cursor, cursor };
    for (int i = 0, n = int(m_generators.size()); i < n; ++i)
        if (m_generators[i].generator->generate(state, temp_result))
            break;

    match_handler& handler = temp_result.get_handler();
    result.set_handler(&handler);

    // Filter the matches to ones that are candidate matches for the word.
    for (unsigned int i = 0, n = temp_result.get_match_count(); i < n; ++i)
    {
        const char* match = temp_result.get_match(i);
        if (handler.compare(word.c_str(), match))
            result.add_match(match);
    }
#endif // MODE4
}
