// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str_iter.h>

// MODE4 : better support for progressive data, c1 codes (csi, osc, etc.)

//------------------------------------------------------------------------------
class ecma48_code
{
public:
    enum type : unsigned char
    {
        type_none,
        type_chars,
        type_c0,
        type_c1,
        type_icf
    };

    enum 
    {
        c1_apc          = 0x5f,
        c1_csi          = 0x5b,
        c1_dcs          = 0x50,
        c1_osc          = 0x5d,
        c1_pm           = 0x5e,
        c1_sos          = 0x58,
    };

    const char*         get_str() const     { return m_str; }
    unsigned int        get_length() const  { return m_length; }
    type                get_type() const    { return m_type; }
    unsigned int        get_code() const    { return m_code; }
    int                 decode_csi(int& final, int* params, unsigned int max_params) const;

private:
    friend class        ecma48_iter;
                        ecma48_code() {}
    const char*         m_str;
    unsigned short      m_length;
    type                m_type;
    unsigned char       m_code;
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
