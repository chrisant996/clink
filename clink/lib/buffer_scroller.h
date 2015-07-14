// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <Windows.h>

//------------------------------------------------------------------------------
class buffer_scroller
{
public:
                    buffer_scroller();
    void            begin();
    void            end();
    void            page_up();
    void            page_down();

private:
    HANDLE          m_handle;
    COORD           m_cursor_position;
};
