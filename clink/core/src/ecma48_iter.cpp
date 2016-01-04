// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "base.h"
#include "ecma48_iter.h"

//------------------------------------------------------------------------------
enum
{
    ecma48_state_unknown,
    ecma48_state_char,
    ecma48_state_esc,
    ecma48_state_csi_p,
    ecma48_state_csi_f,
};

//------------------------------------------------------------------------------
#define in_range(value, left, right)\
    (unsigned int)(right - value) <= (unsigned int)(right - left)

//------------------------------------------------------------------------------
ecma48_iter::ecma48_iter(const char* s, ecma48_state& state)
: m_iter(s)
, m_state(state)
{
}

//------------------------------------------------------------------------------
bool ecma48_iter::next(ecma48_code& code)
{
    code.type = ecma48_code::type_chars;
    code.str = m_iter.get_pointer();

    while (int c = m_iter.peek())
    {
        switch (m_state.state)
        {
        case ecma48_state_unknown:
            m_iter.next();

            if (c == 0x1b)
            {
                m_state.state = ecma48_state_esc;
                continue;
            }
            else if (c == 0x9b)
            {
                m_state.csi = { 0, 0 };
                m_state.state = ecma48_state_csi_p;
                continue;
            }
            else if (in_range(c, 0x00, 0x1f))
            {
                code.type = ecma48_code::type_c0;
                code.c0 = c;
                return true;
            }
            else if (in_range(c, 0x80, 0x9f))
            {
                code.type = ecma48_code::type_c1;
                code.c1 = c - 0x40; // as 7-bit code
                return true;
            }

            m_state.state = ecma48_state_char;
            continue;

        case ecma48_state_char:
            if (in_range(c, 0x00, 0x1f))
            {
                code.type = ecma48_code::type_chars;
                code.length = int(m_iter.get_pointer() - code.str);
                m_state.state = ecma48_state_unknown;
                return true;
            }

            m_iter.next();
            continue;

        case ecma48_state_esc:
            m_iter.next();

            if (c == 0x5b)
            {
                m_state.csi = { 0, 0 };
                m_state.state = ecma48_state_csi_p;
                continue;
            }
            else if (in_range(c, 0x40, 0x5f))
            {
                code.type = ecma48_code::type_c1;
                code.c1 = c;
                m_state.state = ecma48_state_unknown;
                return true;
            }
            else if (in_range(c, 0x60, 0x7f))
            {
                code.type = ecma48_code::type_icf;
                code.icf = c;
                m_state.state = ecma48_state_unknown;
                return true;
            }

            m_state.state = ecma48_state_char;
            continue;

        case ecma48_state_csi_p:
            if (in_range(c, 0x30, 0x3f))
            {
                ecma48_csi& csi = m_state.csi;

                if (!csi.param_count)
                {
                    csi.params[0] = 0;
                    ++csi.param_count;
                }

                int d = c - 0x30;
                if (d <= 9) // [0x30, 0x39]
                {
                    if (csi.param_count <= sizeof_array(csi.params))
                    {
                        int i = csi.param_count - 1;
                        csi.params[i] *= 10;
                        csi.params[i] += d;
                    }
                }
                else if (d == 11) // 0x3b
                {
                    if (csi.param_count < sizeof_array(csi.params))
                        csi.params[csi.param_count] = 0;

                    ++csi.param_count;
                }

                m_iter.next();
                continue;
            }
            /* fall through */

        case ecma48_state_csi_f:
            if (in_range(c, 0x20, 0x2f))
            {
                m_state.csi.func = short(c << 8);
                m_iter.next();
                continue;
            }
            else if (in_range(c, 0x40, 0x7e))
            {
                ecma48_csi& csi = m_state.csi;
                csi.func += c;
                csi.param_count = min<short>(csi.param_count, sizeof_array(csi.params));

                code.type = ecma48_code::type_csi;
                code.csi = &csi;

                m_state.state = ecma48_state_unknown;
                m_iter.next();
                return true;
            }

            m_state.state = ecma48_state_unknown;
            continue;
        }
    }

    if (m_state.state != ecma48_state_char)
        return false;

    code.length = int(m_iter.get_pointer() - code.str);
    return code.length != 0;
}
