// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
template <typename T>
class str_tokeniser_impl
{
public:
                        str_tokeniser_impl(const T* in, const char* delims);
                        str_tokeniser_impl(const str_iter_impl<T>& in, const char* delims);
    bool                next(str_impl<T>& out);

private:
    str_iter_impl<T>    m_iter;
    const char*         m_delims;
};

//------------------------------------------------------------------------------
template <typename T>
str_tokeniser_impl<T>::str_tokeniser_impl(const T* in, const char* delims)
: m_iter(in)
, m_delims(delims)
{
}

//------------------------------------------------------------------------------
typedef str_tokeniser_impl<char>    str_tokeniser;
typedef str_tokeniser_impl<wchar_t> wstr_tokeniser;
