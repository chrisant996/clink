// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_iter.h"

#include <core/base.h>
#include <core/str_tokeniser.h>

#include <assert.h>

//------------------------------------------------------------------------------
extern "C" int mk_wcwidth(char32_t);
inline int clink_wcwidth(char32_t c)
{
    if (c >= ' ' && c <= '~')
        return 1;
    return mk_wcwidth(c);
}



//------------------------------------------------------------------------------
unsigned int cell_count(const char* in)
{
    unsigned int count = 0;

    ecma48_state state;
    ecma48_iter iter(in, state);
    while (const ecma48_code& code = iter.next())
    {
        if (code.get_type() != ecma48_code::type_chars)
            continue;

        str_iter inner_iter(code.get_pointer(), code.get_length());
        while (int c = inner_iter.next())
        {
            int w = clink_wcwidth(c);
            assert(w >= 0); // TODO: Negative isn't handled correctly yet.
            if (w >= 0)
                count += w;
        }
    }

    return count;
}

//------------------------------------------------------------------------------
static bool in_range(int value, int left, int right)
{
    return (unsigned(right - value) <= unsigned(right - left));
}



//------------------------------------------------------------------------------
enum ecma48_state_enum
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
void ecma48_state::reset()
{
    state = ecma48_state_unknown;
    count = 0;
}



//------------------------------------------------------------------------------
bool ecma48_code::decode_csi(csi_base& base, int* params, unsigned int max_params) const
{
    if (get_type() != type_c1 || get_code() != c1_csi)
        return false;

    /* CSI P ... P I .... I F */
    str_iter iter(get_pointer(), get_length());

    // Skip CSI
    if (iter.next() == 0x1b)
        iter.next();

    // Is the parameter string tagged as private/experimental?
    if (base.private_use = in_range(iter.peek(), 0x3c, 0x3f))
        iter.next();

    // Extract parameters.
    base.intermediate = 0;
    base.final = 0;
    int param = 0;
    unsigned int count = 0;
    bool trailing_param = false;
    while (int c = iter.next())
    {
        if (in_range(c, 0x30, 0x3b))
        {
            trailing_param = true;

            if (c == 0x3b)
            {
                if (count < max_params)
                    params[count++] = param;

                param = 0;
            }
            else if (c != 0x3a) // Blissfully gloss over ':' part of spec.
                param = (param * 10) + (c - 0x30);
        }
        else if (in_range(c, 0x20, 0x2f))
            base.intermediate = char(c);
        else if (!in_range(c, 0x3c, 0x3f))
            base.final = char(c);
    }

    if (trailing_param)
        if (count < max_params)
            params[count++] = param;

    base.param_count = char(count);
    return true;
}

//------------------------------------------------------------------------------
bool ecma48_code::get_c1_str(str_base& out) const
{
    if (get_type() != type_c1 || get_code() == c1_csi)
        return false;

    str_iter iter(get_pointer(), get_length());

    // Skip announce
    if (iter.next() == 0x1b)
        iter.next();

    const char* start = iter.get_pointer();

    // Skip until terminator
    while (int c = iter.peek())
    {
        if (c == 0x9c || c == 0x1b)
            break;

        iter.next();
    }

    out.clear();
    out.concat(start, int(iter.get_pointer() - start));
    return true;
}



//------------------------------------------------------------------------------
ecma48_iter::ecma48_iter(const char* s, ecma48_state& state, int len)
: m_iter(s, len)
, m_code(state.code)
, m_state(state)
{
}

//------------------------------------------------------------------------------
const ecma48_code& ecma48_iter::next()
{
    m_code.m_str = m_iter.get_pointer();

    const char* copy = m_iter.get_pointer();
    bool done = true;
    while (1)
    {
        int c = m_iter.peek();
        if (!c)
        {
            if (m_state.state != ecma48_state_char)
            {
                m_code.m_length = 0;
                return m_code;
            }

            break;
        }

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

        if (m_state.state != ecma48_state_char)
        {
            while (copy != m_iter.get_pointer())
            {
                m_state.buffer[m_state.count] = *copy++;
                m_state.count += (m_state.count < sizeof_array(m_state.buffer) - 1);
            }
        }

        if (done)
            break;
    }

    if (m_state.state != ecma48_state_char)
    {
        m_code.m_str = m_state.buffer;
        m_code.m_length = m_state.count;
    }
    else
        m_code.m_length = int(m_iter.get_pointer() - m_code.get_pointer());

    m_state.reset();

    return m_code;
}

//------------------------------------------------------------------------------
bool ecma48_iter::next_c1()
{
    // Convert c1 code to its 7-bit version.
    m_code.m_code = (m_code.m_code & 0x1f) | 0x40;

    switch (m_code.get_code())
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
    m_state.reset();
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
    m_state.reset();
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
    return next_csi_f(c);
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
    {
        m_iter.next();
        return true;
    }

    m_code.m_str = m_iter.get_pointer();
    m_code.m_length = 0;
    m_state.reset();
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

    m_code.m_type = ecma48_code::type_chars;
    m_state.state = ecma48_state_char;
    return false;
}
