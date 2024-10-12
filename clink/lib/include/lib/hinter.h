// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "core/str.h"

class line_state;

//------------------------------------------------------------------------------
class input_hint
{
public:
    bool            empty() const { return m_empty; }
    void            clear();
    void            set(const char* hint, int32 pos);
    bool            equals(const input_hint& other) const;
    const char*     c_str() const { return m_hint.c_str(); }
    int32           pos() const { return m_pos; }

    DWORD           get_timeout() const;
    void            clear_timeout();

private:
    str_moveable    m_hint;
    int32           m_pos = -1;
    DWORD           m_defer = 0;
    bool            m_empty = true;
};

//------------------------------------------------------------------------------
class hinter
{
public:
    virtual void    get_hint(const line_state& line, input_hint& out) = 0;
};
