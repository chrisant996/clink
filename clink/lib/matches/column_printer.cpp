// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "column_printer.h"
#include "matches.h"
#include "terminal.h"

#include <core/base.h>
#include <core/str.h>

//------------------------------------------------------------------------------
column_printer::column_printer(terminal* terminal)
: match_printer(terminal)
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
        longest = max<int>(char_count(matches.get_match(i)), longest);

    if (!longest)
        return;

    int query_items = 100 ; // MODE4: get from rl_completion_query_items
    if (query_items > 0 && query_items <= match_count)
        if (!do_display_prompt(match_count))
            return;

    int display_width = 106; // MODE4: take from rl's completion-display-width or terminal
    int columns = max(1, display_width / longest);
    int rows = (match_count + columns - 1) / columns;

    int pager_row = term->get_rows() - 1;

    bool vertical = true; // MODE4: get from readline.

    int dx = vertical ? rows : 1;
    for (int y = 0; y < rows; ++y)
    {
        int index = vertical ? y : (y * columns);

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

            const char* match = matches.get_match(index);
            term->write(match, int(strlen(match)));

            for (int i = longest - char_count(match); i >= 0; --i)
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

    int ret = term->get_rows() - 2;

    static const char prompt[] = { "--More--" };
    term->write(prompt, sizeof(prompt));
    term->flush();

    while (int i = term->read())
    {
        if (i == ' ')                          break;
        if (i == '\r')                         { ret = 1; break; }
        if (i == 'q' || i == 'Q' || i == 0x03) { ret = pager_row = 0; break; }
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
        }
    }

    term->write("\n", 1);
    return ret;
}
