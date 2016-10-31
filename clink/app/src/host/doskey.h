// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>
#include <core/str.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
class doskey_alias
{
public:
                    doskey_alias();
    void            reset();
    bool            next(wstr_base& out);
    explicit        operator bool () const;

private:
    friend class    doskey;
    wstr<32>        m_buffer;
    const wchar_t*  m_cursor;
};



//------------------------------------------------------------------------------
class doskey
{
public:
                    doskey(const char* shell_name);
    bool            add_alias(const char* alias, const char* text);
    bool            remove_alias(const char* alias);
    void            resolve(const wchar_t* chars, doskey_alias& out);

private:
    bool            resolve_impl(const wstr_iter& in, class wstr_stream* out);
    const char*     m_shell_name;
};
