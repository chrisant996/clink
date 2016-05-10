// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_iter.h"

#include <core/base.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
enum
{
    ecma48_state_unknown = 0,
    ecma48_state_char,
    ecma48_state_esc,
    ecma48_state_esc_st,
    ecma48_state_csi_p,
    ecma48_state_csi_f,
    ecma48_state_cmd_str,
    ecma48_state_char_str,
};

//------------------------------------------------------------------------------
#define in_range(value, left, right)\
    (unsigned(right - value) <= unsigned(right - left))


//------------------------------------------------------------------------------
int ecma48_code::decode_csi(int& final, int* params, unsigned int max_params) const
{
    if (get_type() != type_c1 || get_code() != c1_csi)
        return -1;

    /* CSI P ... P I .... I F */
    str_iter iter(get_str(), get_length());

    int c = iter.peek();

    // Reserved? Then skip all Ps
    if (in_range(c, 0x3c, 0x3f))
        while (in_range(iter.peek(), 0x30, 0x3f))
            iter.next();

    // Extract parameters.
    unsigned int count = 0;
    str_tokeniser tokens(iter, "\x3b");
    const char* param_start;
    int param_length;
    while (tokens.next(param_start, param_length))
    {
        int param = 0;
        bool has_param = (param_length == 0);
        for (; param_length != 0; --param_length, ++param_start)
        {
            if (!in_range(*param_start, 0x30, 0x3a))
                break;

            // Blissfully ignore ':' spec.
            has_param = true;
            if (*param_start != 0x3a)
                param = (param * 10) + (*param_start - 0x30);
        }

        if (has_param && count < max_params)
            params[count++] = param;

        // Intermediates and final.
        final = 0;
        for (; param_length != 0; --param_length, ++param_start)
            final = (final << 8) + *param_start;
    }
    
    return count;
}



//------------------------------------------------------------------------------
ecma48_iter::ecma48_iter(const char* s, ecma48_state& state, int len)
: m_iter(s, len)
, m_state(state)
{
}

//------------------------------------------------------------------------------
const ecma48_code* ecma48_iter::next()
{
    m_code.m_type = ecma48_code::type_chars;
    m_code.m_str = m_iter.get_pointer();
    m_code.m_length = 0;

    bool done = true;
    while (1)
    {
        int c = m_iter.peek();
        if (!c)
            if (m_state.state != ecma48_state_char)
                return nullptr;

        switch (m_state.state)
        {
        case ecma48_state_char:     done = next_char(c);     break;
        case ecma48_state_char_str: done = next_char_str(c); break;
        case ecma48_state_cmd_str:  done = next_cmd_str(c);  break;
        case ecma48_state_csi_f:    done = next_csi_f(c);    break;
        case ecma48_state_csi_p:    done = next_csi_p(c);    break;
        case ecma48_state_esc:      done = next_esc(c);      break;
        case ecma48_state_esc_st:   done = next_esc_st(c);   break;
        case ecma48_state_unknown:  done = next_unknown(c);  break;
        }

        if (done)
            break;
    }

    m_code.m_length = int(m_iter.get_pointer() - m_code.get_str());
    m_state.state = ecma48_state_unknown;
    return (m_code.get_length() != 0) ? &m_code : nullptr;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_c1()
{
    // Convert c1 code to its 7-bit version.
    int seven_bit = (m_code.get_code() <= 0x5f);
    m_code.m_code = (m_code.m_code & 0x1f) | 0x40;

    const char* str_start = m_iter.get_pointer();
    switch (m_code.get_code())
    {
        case 0x50: /* dcs */
        case 0x5d: /* osc */
        case 0x5e: /* pm  */
        case 0x5f: /* apc */
            m_code.m_str = str_start;
            m_state.state = ecma48_state_cmd_str;
            return false;

        case 0x5b: /* csi */
            m_code.m_str = str_start;
            m_state.state = ecma48_state_csi_p;
            return false;

        case 0x58: /* sos */
            m_code.m_str = str_start;
            m_state.state = ecma48_state_char_str;
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_char(int c)
{
    if (in_range(c, 0x00, 0x1f))
    {
        m_code.m_type = ecma48_code::type_chars;
        return true;
    }

    m_iter.next();
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_char_str(int c)
{
    m_iter.next();

    if (c == 0x1b)
    {
        m_state.state = ecma48_state_esc_st;
        return false;
    }

    return (c == 0x9c);
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_cmd_str(int c)
{
    if (c == 0x1b)
    {
        m_iter.next();
        m_state.state = ecma48_state_esc_st;
        return false;
    }
    else if (c == 0x9c)
    {
        m_iter.next();
        return true;
    }
    else if (in_range(c, 0x08, 0x0d) || in_range(c, 0x20, 0x7e))
    {
        m_iter.next();
        return false;
    }

    // Reset
    m_code.m_str = m_iter.get_pointer();
    m_code.m_length = 0;
    m_state.state = ecma48_state_unknown;
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_csi_f(int c)
{
    if (in_range(c, 0x20, 0x2f))
    {
        m_iter.next();
        return false;
    }
    else if (in_range(c, 0x40, 0x7e))
    {
        m_iter.next();
        return true;
    }

    // Reset
    m_code.m_str = m_iter.get_pointer();
    m_code.m_length = 0;
    m_state.state = ecma48_state_unknown;
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_csi_p(int c)
{
    if (in_range(c, 0x30, 0x3f))
    {
        m_iter.next();
        return false;
    }

    m_state.state = ecma48_state_csi_f;
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_esc(int c)
{
    m_iter.next();

    if (in_range(c, 0x40, 0x5f))
    {
        m_code.m_type = ecma48_code::type_c1;
        m_code.m_code = c;
        return next_c1();
    }
    else if (in_range(c, 0x60, 0x7f))
    {
        m_code.m_type = ecma48_code::type_icf;
        m_code.m_code = c;
        return true;
    }

    m_state.state = ecma48_state_char;
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_esc_st(int c)
{
    if (c == 0x5c)
        return true;

    m_code.m_str = m_iter.get_pointer();
    m_code.m_length = 0;
    m_state.state = ecma48_state_unknown;
    return false;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_unknown(int c)
{
    m_iter.next();

    if (c == 0x1b)
    {
        m_state.state = ecma48_state_esc;
        return false;
    }
    else if (in_range(c, 0x00, 0x1f))
    {
        m_code.m_type = ecma48_code::type_c0;
        m_code.m_code = c;
        return true;
    }
    else if (in_range(c, 0x80, 0x9f))
    {
        m_code.m_type = ecma48_code::type_c1;
        m_code.m_code = c;
        return next_c1();
    }

    m_state.state = ecma48_state_char;
    return false;
}
