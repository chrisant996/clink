// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
template <typename T> uint32 str_hash_impl(const T* in, uint32 length)
{
    uint32 hash = 5381;

    while (int32 c = *in++)
    {
        hash = ((hash << 5) + hash) ^ c;
        if (!--length)
            break;
    }

    return hash;
}

//------------------------------------------------------------------------------
inline uint32 str_hash(const char* in, int32 length=-1)
{
    return str_hash_impl<char>(in, length);
}

//------------------------------------------------------------------------------
inline uint32 wstr_hash(const wchar_t* in, int32 length=-1)
{
    return str_hash_impl<wchar_t>(in, length);
}
