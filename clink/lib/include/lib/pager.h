// Copyright (c) 2020 Christopher Antos, Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "editor_module.h"

class printer;

//------------------------------------------------------------------------------
class pager
{
public:
    virtual void    start_pager(printer& printer) = 0;
    virtual bool    on_print_lines(printer& printer, int32 lines) = 0;
};
