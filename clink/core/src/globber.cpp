// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "globber.h"
#include "os.h"
#include "path.h"

//------------------------------------------------------------------------------
globber::globber(const char* pattern)
: m_files(true)
, m_directories(true)
, m_dir_suffix(true)
, m_hidden(false)
, m_system(false)
, m_dots(false)
{
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
bool globber::next(str_base& out, bool rooted)
{
    if (m_handle == nullptr)
        return false;

    str<280> file_name(m_data.cFileName);

    bool skip = false;

    const wchar_t* c = m_data.cFileName;
    skip |= (c[0] == '.' && (!c[1] || (c[1] == '.' && !c[2])) && !m_dots);

    int attr = m_data.dwFileAttributes;
    skip |= (attr & FILE_ATTRIBUTE_SYSTEM) && !m_system;
    skip |= (attr & FILE_ATTRIBUTE_HIDDEN) && !m_hidden;
    skip |= (attr & FILE_ATTRIBUTE_DIRECTORY) && !m_directories;
    skip |= !(attr & FILE_ATTRIBUTE_DIRECTORY) && !m_files;

    next_file();

    if (skip)
        return next(out, rooted);

    out.clear();
    if (rooted)
        out << m_root;

    path::append(out, file_name.c_str());

    if (attr & FILE_ATTRIBUTE_DIRECTORY && m_dir_suffix)
        out << "\\";

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
