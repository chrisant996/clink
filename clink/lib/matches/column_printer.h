// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "match_printer.h"

//------------------------------------------------------------------------------
class column_printer
    : public match_printer
{
public:
                    column_printer(terminal* terminal);
    virtual         ~column_printer();
    virtual void    print(const matches& matches) override;

private:
    int             do_pager(int pager_row);
    bool            do_display_prompt(int count);
    int             m_query_threshold;  // MODE4: get from rl_completion_query_items
    int             m_max_columns;      // MODE4: take from rl's completion-display-width or terminal
    bool            m_vertical;         // MODE4: get from readline.
};
