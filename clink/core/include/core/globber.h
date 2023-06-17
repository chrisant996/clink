// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

//------------------------------------------------------------------------------
class globber
{
public:
    struct extrainfo
    {
        int32               st_mode;
        int32               attr;
        unsigned long long  size;
        FILETIME            accessed;
        FILETIME            modified;
        FILETIME            created;
    };

                        globber(const char* pattern);
                        ~globber();
    void                files(bool state)       { m_files = state; }
    void                directories(bool state) { m_directories = state; }
    void                suffix_dirs(bool state) { m_dir_suffix = state; }
    void                hidden(bool state)      { m_hidden = state; }
    void                system(bool state)      { m_system = state; }
    void                dots(bool state)        { m_dots = state; }
    bool                older_than(int32 seconds);
    bool                next(str_base& out, bool rooted=true, extrainfo* extrainfo=nullptr);
    void                close();

private:
                        globber(const globber&) = delete;
    void                operator = (const globber&) = delete;
    void                next_file();
    WIN32_FIND_DATAW    m_data;
    HANDLE              m_handle;
    str<280>            m_root;
    bool                m_files;
    bool                m_directories;
    bool                m_dir_suffix;
    bool                m_hidden;
    bool                m_system;
    bool                m_dots;
    bool                m_onlyolder;
    FILETIME            m_olderthan;

};
