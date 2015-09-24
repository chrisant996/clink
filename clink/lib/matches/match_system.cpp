// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_system.h"
#include "core/str.h"
#include "line_state.h"
#include "match_generator.h"
#include "matches.h"

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

    str<> builder_word;
    builder_word.concat(line + word_start, cursor - word_start);

    // Adjust the cursor so we get more matches than needed and can have the
    // match builder take care of deciding what's match.
    while (--cursor > word_start)
    {
        if (line[cursor] == '\\' || line[cursor] == '/')
        {
            ++cursor;
            break;
        }
    }

    str<> word;
    word.concat(line + word_start, cursor - word_start);

    // Count the number of quotes.
    line_state state = { word.c_str(), line, word_start, cursor, cursor };
    matches_builder builder(result, builder_word.c_str());
    for (int i = 0, n = int(m_generators.size()); i < n; ++i)
        m_generators[i].generator->generate(state, builder);
}
