// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
class os
{
public:
    enum {
        path_type_invalid,
        path_type_file,
        path_type_dir,
    };

    static int      get_path_type(const char* path);
    static void     get_current_dir(str_base& out);
    static bool     change_dir(const char* dir);
    static bool     make_dir(const char* dir);
    static bool     remove_dir(const char* dir);
    static bool     unlink(const char* path);
    static bool     get_temp_dir(str_base& out);
    static bool     get_env(const char* name, str_base& out);

private:
                    os() = delete;
                    ~os() = delete;
                    os(const os&) = delete;
    void            operator = (const os&) = delete;
};
