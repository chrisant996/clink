// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "array.h"
#include "str.h"
#include "str_iter.h"

//------------------------------------------------------------------------------
class str_token
{
public:
    enum : uint8 {
        invalid_delim   = 0xff,
    };
                        str_token(char c) : delim(c) {}
    explicit            operator bool () const       { return (delim != invalid_delim); }
    uint8               delim;
};



//------------------------------------------------------------------------------
template <typename T>
class str_tokeniser_impl
{
public:
                        str_tokeniser_impl(const T* in=(const T*)L"", const char* delims=" ");
                        str_tokeniser_impl(const str_iter_impl<T>& in, const char* delims=" ");
    bool                add_quote_pair(const char* pair);
    str_token           next(str_impl<T>& out);
    str_token           next(const T*& start, int32& length);
    str_token           next(str_iter_impl<T>& out);
    int32               peek_delims() const;
    const T*            get_pointer() const;

private:
    struct quote
    {
        char            left;
        char            right;
    };

    typedef fixed_array<quote, 4> quotes;

    int32               get_right_quote(int32 left) const;
    str_token           next_impl(const T*& out_start, int32& out_length);
    quotes              m_quotes;
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
template <typename T>
str_tokeniser_impl<T>::str_tokeniser_impl(const str_iter_impl<T>& in, const char* delims)
: m_iter(in)
, m_delims(delims)
{
}

//------------------------------------------------------------------------------
template <typename T>
bool str_tokeniser_impl<T>::add_quote_pair(const char* pair)
{
    if (pair == nullptr || !pair[0])
        return false;

    quote* q = m_quotes.push_back();
    if (q == nullptr)
        return false;

    *q = { pair[0], (pair[1] ? pair[1] : pair[0]) };
    return true;
}

//------------------------------------------------------------------------------
template <typename T>
int32 str_tokeniser_impl<T>::peek_delims() const
{
    str_iter iter = m_iter;

    // Skip initial delimiters.
    while (const int32 c = iter.peek())
    {
        const char* delim = str_chr(m_delims, c);
        if (delim == nullptr)
            break;

        iter.next();
    }

    return int32(iter.get_pointer() - m_iter.get_pointer());
}

//------------------------------------------------------------------------------
template <typename T>
const T* str_tokeniser_impl<T>::get_pointer() const
{
    return m_iter.get_pointer();
}

//------------------------------------------------------------------------------
typedef str_tokeniser_impl<char>    str_tokeniser;
typedef str_tokeniser_impl<wchar_t> wstr_tokeniser;
