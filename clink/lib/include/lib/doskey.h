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
    bool            next(str_base& out);
    explicit        operator bool () const;

    const str_base& UNITTEST_get_stream() const { return m_buffer; }

private:
    friend class    doskey;
    str<32>         m_buffer;
    const char*     m_cursor;
};



//------------------------------------------------------------------------------
class doskey
{
public:
                    doskey(const char* shell_name);
                    doskey(const wchar_t* shell_name);
    bool            add_alias(const char* alias, const char* text);
    bool            remove_alias(const char* alias);
    void            resolve(const char* chars, doskey_alias& out, int32* point=nullptr);

private:
    bool            resolve_impl(str_iter& s, class str_stream& out, int32* point);
    wstr<16>        m_shell_name;
};
