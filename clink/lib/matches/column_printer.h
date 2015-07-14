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
};
