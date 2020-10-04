// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "globber.h"
#include "os.h"
#include "path.h"

#include <sys/stat.h>

//------------------------------------------------------------------------------
globber::globber(const char* pattern)
: m_files(true)
, m_directories(true)
, m_dir_suffix(true)
, m_hidden(false)
, m_system(false)
, m_dots(false)
{
    // Don't bother trying to complete a UNC path that doesn't have at least
    // both a server and share component.
    if (path::is_incomplete_unc(pattern))
    {
        m_handle = nullptr;
        return;
    }

    // Windows: Expand if the path to complete is drive relative (e.g. 'c:foobar')
    // Drive X's current path is stored in the environment variable "=X:"
    str<288> rooted;
    if (pattern[0] && pattern[1] == ':' && pattern[2] != '\\' && pattern[2] != '/')
    {
        char env_var[4] = { '=', pattern[0], ':', 0 };
        if (os::get_env(env_var, rooted))
        {
            rooted << "\\";
            rooted << (pattern + 2);
            pattern = rooted.c_str();
        }
    }

    wstr<280> wglob(pattern);
    m_handle = FindFirstFileW(wglob.c_str(), &m_data);
    if (m_handle == INVALID_HANDLE_VALUE)
        m_handle = nullptr;

    path::get_directory(pattern, m_root);
}

//------------------------------------------------------------------------------
globber::~globber()
{
    if (m_handle != nullptr)
        FindClose(m_handle);
}

//------------------------------------------------------------------------------
bool globber::next(str_base& out, bool rooted, int* st_mode)
{
    if (m_handle == nullptr)
        return false;

    str<280> file_name;
    int attr;

    while (true)
    {
        if (m_handle == nullptr)
            return false;

        file_name = m_data.cFileName;
        attr = m_data.dwFileAttributes;

        bool again = false;

        const wchar_t* c = m_data.cFileName;
        again |= (c[0] == '.' && (!c[1] || (c[1] == '.' && !c[2])) && !m_dots);

        again |= (attr & FILE_ATTRIBUTE_SYSTEM) && !m_system;
        again |= (attr & FILE_ATTRIBUTE_HIDDEN) && !m_hidden;
        again |= (attr & FILE_ATTRIBUTE_DIRECTORY) && !m_directories;
        again |= !(attr & FILE_ATTRIBUTE_DIRECTORY) && !m_files;

        next_file();

        if (!again)
            break;
    }

    out.clear();
    if (rooted)
        out << m_root;

    path::append(out, file_name.c_str());

    if (st_mode)
    {
        int mode = 0;
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            mode |= _S_IFDIR;
#ifdef S_ISLNK
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
            mode |= _S_IFLNK;
#endif
        if (!mode)
            mode |= _S_IFREG;
        *st_mode = mode;
    }
    else
    {
        if (attr & FILE_ATTRIBUTE_DIRECTORY && m_dir_suffix)
            out << "\\";
    }

    return true;
}

//------------------------------------------------------------------------------
void globber::next_file()
{
    if (FindNextFileW(m_handle, &m_data))
        return;

    FindClose(m_handle);
    m_handle = nullptr;
}
