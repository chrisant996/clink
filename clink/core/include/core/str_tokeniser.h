// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "array.h"
#include "str.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
template <typename T>
class str_tokeniser_impl
{
public:
                        str_tokeniser_impl(const T* in, const char* delims);
                        str_tokeniser_impl(const str_iter_impl<T>& in, const char* delims);
    void                unquoted(bool state);
    bool                add_quotes(const char* pair);
    bool                next(str_impl<T>& out);
    bool                next(const T*& start, int& length);

private:
    struct quote
    {
        unsigned char   left;
        unsigned char   right;
    };

    typedef fixed_array<quote, 4> quotes;

    bool                next_impl(const T*& out_start, int& out_length);
    void                dequote(str_impl<T>& out) const;
    void                dequote(const T*& start, int& length) const;
    quotes              m_quotes;
    str_iter_impl<T>    m_iter;
    const char*         m_delims;
    bool                m_unquoted;
};

//------------------------------------------------------------------------------
template <typename T>
str_tokeniser_impl<T>::str_tokeniser_impl(const T* in, const char* delims)
: m_iter(in)
, m_delims(delims)
, m_unquoted(false)
{
}

//------------------------------------------------------------------------------
template <typename T>
str_tokeniser_impl<T>::str_tokeniser_impl(const str_iter_impl<T>& in, const char* delims)
: m_iter(in)
, m_delims(delims)
, m_unquoted(false)
{
}

//------------------------------------------------------------------------------
template <typename T>
bool str_tokeniser_impl<T>::add_quotes(const char* pair)
{
    if (pair == nullptr || !pair[0])
        return false;

    quote* q = m_quotes.push_back();
    if (q == nullptr)
        return false;

    *q = { pair[0], pair[1] };
    return true;
}

//------------------------------------------------------------------------------
template <typename T>
void str_tokeniser_impl<T>::unquoted(bool state)
{
    m_unquoted = state;
}

//------------------------------------------------------------------------------
typedef str_tokeniser_impl<char>    str_tokeniser;
typedef str_tokeniser_impl<wchar_t> wstr_tokeniser;
