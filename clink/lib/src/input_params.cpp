// Copyright (c) 2022-2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "input_params.h"
#include <core/base.h>
#include <assert.h>

//------------------------------------------------------------------------------
bool input_params::get(unsigned int param, unsigned int& value) const
{
    if (param >= m_num)
    {
        value = 0;
        return false;
    }

    value = m_params[param];
    return true;
}

//------------------------------------------------------------------------------
unsigned int input_params::count() const
{
    return m_num;
}

//------------------------------------------------------------------------------
short input_params::length() const
{
    return m_len;
}

//------------------------------------------------------------------------------
bool input_params::add(unsigned short value, unsigned char len)
{
    if (m_num >= sizeof_array(m_params))
        return false;

    m_params[m_num++] = value;
    m_len += len;

    // Offset the '*' key, so that depth + len is always the key sequence
    // length, even when the param is empty (no digits).
    m_len--;
    return true;
}

//------------------------------------------------------------------------------
void input_params::clear()
{
    m_num = 0;
    m_len = 0;
}
