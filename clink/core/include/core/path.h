// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
class path
{
public:
    static void     clean(str_base& in_out, int sep='\\');
    static bool     get_base_name(const char* in, str_base& out);
    static bool     get_directory(const char* in, str_base& out);
    static bool     get_directory(str_base& in_out);
    static bool     get_drive(const char* in, str_base& out);
    static bool     get_drive(str_base& in_out);
    static bool     get_extension(const char* in, str_base& out);
    static bool     get_name(const char* in, str_base& out);
    static bool     is_root(const char* path);
    static bool     join(const char* lhs, const char* rhs, str_base& out);
    static bool     append(str_base& out, const char* rhs);

private:
                    path() = delete;
                    ~path() = delete;
                    path(const char&) = delete;
    void            operator = (const path&) = delete;
    static int      get_directory_end(const char* path);
};
