// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "str_compare.h"

int str_compare_scope::ts_mode = str_compare_scope::exact;

//------------------------------------------------------------------------------
str_compare_scope::str_compare_scope(int mode)
{
    m_prev_mode = ts_mode;
    ts_mode = mode;
}

//------------------------------------------------------------------------------
str_compare_scope::~str_compare_scope()
{
    ts_mode = m_prev_mode;
}

//------------------------------------------------------------------------------
int str_compare_scope::current()
{
    return ts_mode;
}
