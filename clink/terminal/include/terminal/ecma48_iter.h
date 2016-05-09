// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str_iter.h>

// MODE4 : better support for progressive data, c1 codes (csi, osc, etc.)

//------------------------------------------------------------------------------
enum
{
    ecma48_c1_apc = 0x5f,
    ecma48_c1_csi = 0x5b,
    ecma48_c1_dcs = 0x50,
    ecma48_c1_osc = 0x5d,
    ecma48_c1_pm  = 0x5e,
    ecma48_c1_sos = 0x58,
};

//------------------------------------------------------------------------------
struct ecma48_code
{
    const char*         str;
    unsigned short      length;
    unsigned short      type;
    union {
        int             c0;
        int             c1;
        int             icf;
    };

    enum { type_chars, type_c0, type_c1, type_icf };
};

//------------------------------------------------------------------------------
struct ecma48_state
{
    int                 state = 0;
};

//------------------------------------------------------------------------------
class ecma48_iter
{
public:
                        ecma48_iter(const char* s, ecma48_state& state, int len=-1);
    const ecma48_code*  next();

private:
    bool                next_c1();
    bool                next_char(int c);
    bool                next_char_str(int c);
    bool                next_cmd_str(int c);
    bool                next_csi_f(int c);
    bool                next_csi_p(int c);
    bool                next_esc(int c);
    bool                next_esc_st(int c);
    bool                next_unknown(int c);
    str_iter            m_iter;
    ecma48_code         m_code;
    ecma48_state&       m_state;
};
