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
    while (iter >= m_generators.begin())
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
