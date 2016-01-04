// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "column_printer.h"
#include "matches.h"
#include "terminal.h"

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

    for (int i = 0, n = matches.get_match_count(); i < n; ++i)
    {
        const char* match = matches.get_match(i);
        term->write(match, int(strlen(match)));
        term->write("\n", 1);
    }

    term->flush();
}
