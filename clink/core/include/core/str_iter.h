// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

//------------------------------------------------------------------------------
template <typename TYPE>
class str_iter_impl
{
public:
                str_iter_impl(const TYPE* s) : m_ptr(s) {}
                str_iter_impl(const str_impl<TYPE>& s) : m_ptr(s.c_str()) {}
    int         next();

private:
    const TYPE* m_ptr;
};

//------------------------------------------------------------------------------
typedef str_iter_impl<char>     str_iter;
typedef str_iter_impl<wchar_t>  wstr_iter;
