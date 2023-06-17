// Copyright (c) 2020-2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <stdlib.h>

#include "auto_free_str.h"

//------------------------------------------------------------------------------
auto_free_str::~auto_free_str()
{
    free(m_ptr);
}

//------------------------------------------------------------------------------
auto_free_str& auto_free_str::operator=(auto_free_str&& other)
{
    free(m_ptr);
    m_ptr = other.m_ptr;
    other.m_ptr = nullptr;
    return *this;
}

//------------------------------------------------------------------------------
void auto_free_str::set(const char* s, int32 len)
{
    if (s == m_ptr)
    {
        if (len < int32(strlen(m_ptr)))
            m_ptr[len] = '\0';
    }
    else
    {
        char* old = m_ptr;
        if (len < 0)
            len = int32(strlen(s));
        m_ptr = (char*)malloc(len + 1);
        memcpy(m_ptr, s, len);
        m_ptr[len] = '\0';
        free(old);
    }
}
