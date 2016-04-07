// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "column_printer.h"
#include "matches.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <terminal/terminal.h>

//------------------------------------------------------------------------------
static setting_int g_query_threshold(
    "match.query_threshold",
    "Ask if matches > threshold",
    "", // MODE4
    100);

static setting_int g_max_width(
    "match.max_width",
    "Maximum display width",
    "", // MODE4
    106);

static setting_bool g_vertical(
    "match.vertical",
    "Display matches vertically",
    "", // MODE4
    false);

// MODE4 : unused
static setting_int g_colour_match(
    "match.colour",
    "Match display colour",
    "Colour to use when displaying matches. A value less than 0 will be\n"
    "the opposite brightness of the default colour.",
    -1);

// MODE4 : unused
static setting_int g_colour_match_lcd(
    "match.colour_lcd",
    "Match LCD colour",
    "",
    -1);

// MODE4 : unused
static setting_int g_colour_match_next(
    "match.colour_next",
    "Match next character colour",
    "",
    -1);


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
    {
        const char* match = matches.get_match(i);
        longest = max<int>(char_count(match), longest);
    }

    if (!longest)
        return;

    int query_threshold = g_query_threshold.get();
    if (query_threshold > 0 && query_threshold <= match_count)
        if (!do_display_prompt(match_count))
            return;

    int columns = max(1, g_max_width.get() / longest);
    int rows = (match_count + columns - 1) / columns;
    int pager_row = term->get_rows() - 1;

    bool vertical = g_vertical.get();
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

            str<> displayable;
            const char* match = matches.get_match(index);
            term->write(match, int(strlen(match)));

            displayable = match; // MODE4
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
        case 0x1b: // esc
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
        case 0x03: // ctrl-c
        case 0x1b: // esc
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
