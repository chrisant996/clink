// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "globber.h"
#include "os.h"
#include "path.h"

//------------------------------------------------------------------------------
globber::globber(const context& ctx)
: m_context(ctx)
{
    if (m_context.path == nullptr)      m_context.path = "";
    if (m_context.wildcard == nullptr)  m_context.wildcard = "*";

    str<MAX_PATH> glob;

    // Windows: Expand if the path to complete is drive relative (e.g. 'c:foobar')
    // Drive X's current path is stored in the environment variable "=X:"
    const char* path = m_context.path;
    if (path[0] && path[1] == ':' && path[2] != '\\' && path[2] != '/')
    {
        char env_var[4] = { '=', path[0], ':', 0 };
        os::get_env(env_var, glob);
        glob << "/";
        glob << (path + 2);
    }
    else
        glob << m_context.path;

    glob << m_context.wildcard;

    wstr<MAX_PATH> wglob(glob.c_str());
    m_handle = FindFirstFileW(wglob.c_str(), &m_data);
    if (m_handle == INVALID_HANDLE_VALUE)
        m_handle = nullptr;

    path::get_directory(path, m_root);
}

//------------------------------------------------------------------------------
globber::~globber()
{
    if (m_handle != nullptr)
        FindClose(m_handle);
}

//------------------------------------------------------------------------------
bool globber::next(str_base& out)
{
    if (m_handle == nullptr)
        return false;

    str<MAX_PATH> file_name(m_data.cFileName);

    const wchar_t* c = m_data.cFileName;
    if (c[0] == '.' && (!c[1] || (c[1] == '.' && !c[2])) && !m_context.dots)
        goto skip_file;

    int attr = m_data.dwFileAttributes;
// MODE4
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
        goto skip_file;
// MODE4

    if ((attr & FILE_ATTRIBUTE_HIDDEN) && !m_context.hidden)
        goto skip_file;

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) && m_context.no_directories)
        goto skip_file;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) && m_context.no_files)
        goto skip_file;

    out.clear();
    path::join(m_root.c_str(), file_name.c_str(), out);
    if (attr & FILE_ATTRIBUTE_DIRECTORY && !m_context.no_dir_suffix)
        out << "\\";

    next_file();
    return true;

skip_file:
    next_file();
    return next(out);
}

//------------------------------------------------------------------------------
void globber::next_file()
{
    if (FindNextFileW(m_handle, &m_data))
        return;

    FindClose(m_handle);
    m_handle = nullptr;
}
