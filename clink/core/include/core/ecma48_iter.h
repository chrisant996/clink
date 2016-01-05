// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str_iter.h"

//------------------------------------------------------------------------------
struct ecma48_csi
{
    unsigned short      func;
    unsigned short      param_count;
    unsigned short      params[8];
};

//------------------------------------------------------------------------------
struct ecma48_code
{
    union {
        int             c0;
        int             c1;
        int             icf;
        ecma48_csi*     csi;
        struct {
            const char* str;
            int         length;
        };
    };
    char                type;

    enum { type_chars, type_c0, type_c1, type_csi, type_icf };
};

//------------------------------------------------------------------------------
struct ecma48_state
{
    int                 state = 0;
    ecma48_csi          csi;
};

//------------------------------------------------------------------------------
class ecma48_iter
{
public:
                        ecma48_iter(const char* s, ecma48_state& state, int len=-1);
    bool                next(ecma48_code& code);

private:
    str_iter            m_iter;
    ecma48_state&       m_state;
};
