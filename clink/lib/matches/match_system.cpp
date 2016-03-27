// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_system.h"
#include "line_state.h"
#include "match_generator.h"
#include "matches.h"

#include <core/path.h>
#include <core/str.h>
#include <core/str_compare.h>

//------------------------------------------------------------------------------
match_system::match_system()
{
}

//------------------------------------------------------------------------------
match_system::~match_system()
{
}

//------------------------------------------------------------------------------
bool match_system::add_generator(match_generator* generator, int priority)
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

    *(iter + 1) = { generator, priority };

    return true;
}

//------------------------------------------------------------------------------
void match_system::generate_matches(
    const char* line,
    int cursor,
    matches& result) const
{
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
}
