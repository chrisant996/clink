// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class seh_scope
{
public:
                seh_scope();
                ~seh_scope();

private:
    void*       m_prev_filter;
};
