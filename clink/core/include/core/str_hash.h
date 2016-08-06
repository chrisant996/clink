// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T> unsigned int str_hash_impl(const T* in, unsigned int length)
{
    unsigned int hash = 5381;

    while (int c = *in++)
    {
        hash = ((hash << 5) + hash) ^ c;
        if (!--length)
            break;
    }

    return hash;
}

//------------------------------------------------------------------------------
inline unsigned int str_hash(const char* in, int length=-1)
{
    return str_hash_impl<char>(in, length);
}

//------------------------------------------------------------------------------
inline unsigned int wstr_hash(const wchar_t* in, int length=-1)
{
    return str_hash_impl<wchar_t>(in, length);
}
