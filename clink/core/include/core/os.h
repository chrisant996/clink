// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class str_base;

//------------------------------------------------------------------------------
namespace os
{
    enum {
        path_type_invalid,
        path_type_file,
        path_type_dir,
    };

    int     get_path_type(const char* path);
    void    get_current_dir(str_base& out);
    bool    set_current_dir(const char* dir);
    bool    make_dir(const char* dir);
    bool    remove_dir(const char* dir);
    bool    unlink(const char* path);
    bool    move(const char* src_path, const char* dest_path);
    bool    copy(const char* src_path, const char* dest_path);
    bool    get_temp_dir(str_base& out);
    bool    get_env(const char* name, str_base& out);
}; // namespace os
