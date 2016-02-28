// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
namespace path
{
    void        clean(str_base& in_out, int sep=0);
    void        clean(char* in_out, int sep=0);
    bool        get_base_name(const char* in, str_base& out);
    bool        get_directory(const char* in, str_base& out);
    bool        get_directory(str_base& in_out);
    bool        get_drive(const char* in, str_base& out);
    bool        get_drive(str_base& in_out);
    bool        get_extension(const char* in, str_base& out);
    bool        get_name(const char* in, str_base& out);
    const char* get_name(const char* in);
    bool        is_root(const char* path);
    bool        join(const char* lhs, const char* rhs, str_base& out);
    bool        append(str_base& out, const char* rhs);
}; // namespace path
