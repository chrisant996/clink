// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor.h"
#include "core/os.h"
#include "line_state.h"
#include "matches/match_generator.h"
#include "matches/matches.h"

//------------------------------------------------------------------------------
struct cwd_restorer
{
                    cwd_restorer();
                    ~cwd_restorer();
    str<MAX_PATH>   m_path;
};

//------------------------------------------------------------------------------
cwd_restorer::cwd_restorer()
{
    os::get_current_dir(m_path);
}

//------------------------------------------------------------------------------
cwd_restorer::~cwd_restorer()
{
    os::set_current_dir(m_path.c_str());
}



//------------------------------------------------------------------------------
line_editor::line_editor(const desc& desc)
: m_terminal(desc.term)
, m_match_printer(desc.match_printer)
, m_match_generator(desc.match_generator)
{
    m_shell_name << desc.shell_name;
}

//------------------------------------------------------------------------------
bool line_editor::edit_line(const char* prompt, str_base& out)
{
    cwd_restorer cwd;
    return edit_line_impl(prompt, out);
}

//------------------------------------------------------------------------------
void line_editor::generate_matches(
    const char* line,
    int cursor,
    matches& result) const
{
    if (m_match_generator == nullptr)
        return;

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
    m_match_generator->generate(state, builder);
}
