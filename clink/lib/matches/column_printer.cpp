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

    // Get the longest match length.
    int longest = 0;
    for (int i = 0, n = matches.get_match_count(); i < n; ++i)
    {
        const char* match = matches.get_match(i);
        longest = max<int>(char_count(match), longest);
    }

    if (!longest)
        return;

    int display_width = 106; // MODE4: take from rl's completion-display-width or terminal
    int match_count = matches.get_match_count();
    int columns = max(1, display_width / longest);
    int rows = (match_count + columns - 1) / columns;

    bool vertical = true; // MODE4: get from readline.

    int dx = vertical ? rows : 1;
    for (int y = 0; y < rows; ++y)
    {
        int index = vertical ? y : (y * columns);

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
