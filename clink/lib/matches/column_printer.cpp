// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "column_printer.h"
#include "match_handler.h"
#include "matches.h"
#include "terminal.h"

#include <core/base.h>
#include <core/str.h>

//------------------------------------------------------------------------------
column_printer::column_printer(terminal* terminal)
: match_printer(terminal)
, m_query_threshold(100)
, m_max_columns(106)
, m_vertical(true)
{
}

//------------------------------------------------------------------------------
column_printer::~column_printer()
{
}

//------------------------------------------------------------------------------
void column_printer::print(const matches& matches)
{
    terminal* term = get_terminal();
    int match_count = matches.get_match_count();

    // Get the longest match length.
    int longest = 0;
    for (int i = 0; i < match_count; ++i)
    {
        const char* match = matches.get_match(i);

        str<> displayable;
        matches.get_handler().get_displayable(match, displayable);

        longest = max<int>(displayable.char_count(), longest);
    }

    if (!longest)
        return;

    if (m_query_threshold > 0 && m_query_threshold <= match_count)
        if (!do_display_prompt(match_count))
            return;

    int columns = max(1, m_max_columns / longest);
    int rows = (match_count + columns - 1) / columns;
    int pager_row = term->get_rows() - 1;

    int dx = m_vertical ? rows : 1;
    for (int y = 0; y < rows; ++y)
    {
        int index = m_vertical ? y : (y * columns);

        if (y == pager_row)
        {
            pager_row = do_pager(pager_row);
            if (!pager_row)
                break;
        }

        for (int x = 0; x < columns; ++x)
        {
            if (index >= match_count)
                continue;

            str<> displayable;
            const char* match = matches.get_match(index);
            matches.get_handler().get_displayable(match, displayable);

            term->write(displayable.c_str(), int(displayable.length()));

            for (int i = longest - displayable.char_count(); i >= 0; --i)
                term->write(" ", 1);

            index += dx;
        }

        term->write("\n", 1);
    }

    term->flush();
}

//------------------------------------------------------------------------------
int column_printer::do_pager(int pager_row)
{
    terminal* term = get_terminal();

    static const char prompt[] = { "--More--" };
    term->write(prompt, sizeof(prompt));
    term->flush();

    int ret = term->get_rows() - 2;
    for (bool loop = true; loop; )
    {
        switch (term->read())
        {
        case ' ':
        case '\t':
            loop = false;
            break;

        case '\r':
            loop = false;
            ret = 1;
            break;

        case 'q':
        case 'Q':
        case 0x03: // ctrl-c
        case 0x7f: // esc
            loop = false;
            ret = pager_row = 0;
            break;

        default:
            term->write("\x07", 1);
        }
    }

    term->write("\r", 1);
    return pager_row + ret;
}

//------------------------------------------------------------------------------
bool column_printer::do_display_prompt(int count)
{
    terminal* term = get_terminal();

    str<64> prompt;
    prompt.format("Show %d matches? [Yn]", count);
    term->write(prompt.c_str(), prompt.length());
    term->flush();

    bool ret = true;
    for (bool loop = true; loop; )
    {
        switch(term->read())
        {
        case 'y':
        case 'Y':
        case ' ':
        case '\t':
        case '\r':
            loop = false;
            break;

        case 'n':
        case 'N':
        case 0x03:
        case 0x7f:
            ret = false;
            loop = false;
            break;

        default:
            term->write("\x07", 1);
        }
    }

    term->write("\n", 1);
    return ret;
}
