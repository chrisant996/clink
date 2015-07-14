// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

#include <Windows.h>

//------------------------------------------------------------------------------
class globber
{
public:
    struct context
    {
        const char*     path;
        const char*     wildcard;
        bool            no_files;
        bool            no_directories;
        bool            no_dir_suffix;
        bool            hidden;
        bool            dots;
    };

                        globber(const context& ctx);
                        ~globber();
    bool                next(str_base& out);

private:
                        globber(const globber&) = delete;
    void                operator = (const globber&) = delete;
    void                next_file();
    WIN32_FIND_DATAW    m_data;
    HANDLE              m_handle;
    str<MAX_PATH>       m_root;
    context             m_context;
};
