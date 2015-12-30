// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_system.h"
#include "line_state.h"
#include "match_generator.h"
#include "matches.h"

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
void match_system::add_generator(match_generator* generator, int priority)
{
    if (generator == nullptr)
        return;

    int insert_at = 0;
    for (int n = int(m_generators.size()); insert_at < n; ++insert_at)
        if (m_generators[insert_at].priority >= priority)
            break;

    Generator g = { generator, priority };
    m_generators.insert(m_generators.begin() + insert_at, g);
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
    matches_builder builder(temp_result);

    line_state state = { word.c_str(), line, word_start, cursor, cursor };
    for (int i = 0, n = int(m_generators.size()); i < n; ++i)
        if (m_generators[i].generator->generate(state, builder))
            break;

    // Filter the matches to ones that are candidate matches for the word.
    int word_length = word.length();
    for (unsigned int i = 0, n = temp_result.get_match_count(); i < n; ++i)
    {
        const char* match = temp_result.get_match(i);

        int offset = 0;
        int j;
        while (1)
        {
            j = str_compare(word.c_str() + offset, match + offset);
            if (match[j] != '\\' && match[j] != '/')
                break;

            offset = j + 1;
        }

        j += offset;
        if (j < 0 || j >= word_length)
            result.add_match(match);
    }
}
