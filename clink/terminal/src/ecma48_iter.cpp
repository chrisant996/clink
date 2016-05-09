// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_iter.h"

#include <core/base.h>

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
    (unsigned int)(right - value) <= (unsigned int)(right - left)

//------------------------------------------------------------------------------
ecma48_iter::ecma48_iter(const char* s, ecma48_state& state, int len)
: m_iter(s, len)
, m_state(state)
{
}

//------------------------------------------------------------------------------
const ecma48_code* ecma48_iter::next()
{
    m_code.type = ecma48_code::type_chars;
    m_code.str = m_iter.get_pointer();
    m_code.length = 0;

    bool done = true;
    while (1)
    {
        int c = m_iter.peek();
        if (!c)
            if (m_state.state != ecma48_state_char)
                return nullptr;

        switch (m_state.state)
        {
        case ecma48_state_unknown:  done = next_unknown(c);  break;
        case ecma48_state_char:     done = next_char(c);     break;
        case ecma48_state_esc:      done = next_esc(c);      break;
        case ecma48_state_esc_st:   done = next_esc_st(c);   break;
        case ecma48_state_cmd_str:  done = next_cmd_str(c);  break;
        case ecma48_state_char_str: done = next_char_str(c); break;
        case ecma48_state_csi_p:    done = next_csi_p(c);    break;
        case ecma48_state_csi_f:    done = next_csi_f(c);    break;
        }

        if (done)
            break;
    }

    m_code.length = int(m_iter.get_pointer() - m_code.str);
    m_state.state = ecma48_state_unknown;
    return (m_code.length != 0) ? &m_code : nullptr;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_c1()
{
    switch (m_code.c1)
    {
        case 0x50: /* dcs */
        case 0x5d: /* osc */
        case 0x5e: /* pm  */
        case 0x5f: /* apc */
            m_state.state = ecma48_state_cmd_str;
            return false;

        case 0x5b: /* csi */
            m_state.state = ecma48_state_csi_p;
            return false;

        case 0x58: /* sos */
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
        m_code.type = ecma48_code::type_chars;
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
    m_code.str = m_iter.get_pointer();
    m_code.length = 0;
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
    m_code.str = m_iter.get_pointer();
    m_code.length = 0;
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
        m_code.type = ecma48_code::type_c1;
        m_code.c1 = c;
        return next_c1();
    }
    else if (in_range(c, 0x60, 0x7f))
    {
        m_code.type = ecma48_code::type_icf;
        m_code.icf = c;
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

    m_code.str = m_iter.get_pointer();
    m_code.length = 0;
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
        m_code.type = ecma48_code::type_c0;
        m_code.c0 = c;
        return true;
    }
    else if (in_range(c, 0x80, 0x9f))
    {
        m_code.type = ecma48_code::type_c1;
        m_code.c1 = c - 0x40; // as 7-bit code
        return next_c1();
    }

    m_state.state = ecma48_state_char;
    return false;
}
