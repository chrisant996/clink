// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "globber.h"
#include "os.h"
#include "path.h"
#include "str.h"

#include <sys/stat.h>

//------------------------------------------------------------------------------
globber::globber(const char* pattern)
: m_files(true)
, m_directories(true)
, m_dir_suffix(true)
, m_hidden(false)
, m_system(false)
, m_dots(false)
, m_onlyolder(false)
{
    str<32> strip_quotes;
    concat_strip_quotes(strip_quotes, pattern);
    pattern = strip_quotes.c_str();

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
            rooted << PATH_SEP;
            rooted << (pattern + 2);
            pattern = rooted.c_str();
        }
    }

    // Include `.` and `..` directories if the pattern's filename part is
    // exactly `.` or `..`.
    if (pattern)
    {
        const char* name = path::get_name(pattern);
        if (name && name[0] == '.')
        {
            unsigned int index = 1;
            if (name[index] == '.')
                index++;
            if (name[index] == '*')
                index++;
            if (name[index] == '\0')
                dots(true);
        }
    }

    wstr<280> wglob(pattern);
    m_handle = FindFirstFileW(wglob.c_str(), &m_data);
    if (m_handle == INVALID_HANDLE_VALUE)
        m_handle = nullptr;

    path::get_directory(pattern, m_root);
    path::normalise_separators(m_root.data());
}

//------------------------------------------------------------------------------
globber::~globber()
{
    close();
}

//------------------------------------------------------------------------------
bool globber::older_than(int seconds)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    if (!SystemTimeToFileTime(&systime, &m_olderthan))
    {
        close();
        return false;
    }

    ULONGLONG delta = seconds;
    delta *= ULONGLONG(10000000);

    ULONGLONG* olderthan = reinterpret_cast<ULONGLONG*>(&m_olderthan);
    if ((*olderthan) <= delta)
        (*olderthan) = 0;
    else
        (*olderthan) -= delta;

    m_onlyolder = true;
    return true;
}

//------------------------------------------------------------------------------
bool globber::next(str_base& out, bool rooted, extrainfo* extrainfo)
{
    if (m_handle == nullptr)
        return false;

    str<280> file_name;
    int attr;
    ULARGE_INTEGER size;
    FILETIME accessed;
    FILETIME modified;
    FILETIME created;
    bool symlink;

    while (true)
    {
        if (m_handle == nullptr)
            return false;

        file_name = m_data.cFileName;
        attr = m_data.dwFileAttributes;
        symlink = ((attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
                   !(attr & FILE_ATTRIBUTE_OFFLINE) &&
                   (m_data.dwReserved0 == IO_REPARSE_TAG_SYMLINK));

        if (extrainfo)
        {
            size.LowPart = m_data.nFileSizeLow;
            size.HighPart = m_data.nFileSizeHigh;
            accessed = m_data.ftLastAccessTime;
            modified = m_data.ftLastWriteTime;
            created = m_data.ftCreationTime;
        }

        bool again = false;

        const wchar_t* c = m_data.cFileName;
        again |= (c[0] == '.' && (!c[1] || (c[1] == '.' && !c[2])) && !m_dots);

        again |= (attr & FILE_ATTRIBUTE_SYSTEM) && !m_system;
        again |= (attr & FILE_ATTRIBUTE_HIDDEN) && !m_hidden;
        again |= (attr & FILE_ATTRIBUTE_DIRECTORY) && !m_directories;
        again |= !(attr & FILE_ATTRIBUTE_DIRECTORY) && !m_files;

        if (m_onlyolder)
            again |= !(CompareFileTime(&m_data.ftLastWriteTime, &m_olderthan) < 0);

        next_file();

        if (!again)
            break;
    }

    out.clear();
    if (rooted)
        out << m_root;

    path::append(out, file_name.c_str());

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) && m_dir_suffix)
        out << PATH_SEP;

    if (extrainfo)
    {
        int mode = 0;
#ifdef S_ISLNK
        if (symlink)                                mode |= _S_IFLNK;
#endif
        if (attr & FILE_ATTRIBUTE_DIRECTORY)        mode |= _S_IFDIR;
        if (!mode)                                  mode |= _S_IFREG;
        extrainfo->st_mode = mode;

        extrainfo->attr = attr;

        extrainfo->size = size.QuadPart;

        extrainfo->accessed = accessed;
        extrainfo->modified = modified;
        extrainfo->created = created;
    }

    return true;
}

//------------------------------------------------------------------------------
void globber::close()
{
    if (m_handle)
    {
        FindClose(m_handle);
        m_handle = nullptr;
    }
}

//------------------------------------------------------------------------------
void globber::next_file()
{
    if (m_handle && !FindNextFileW(m_handle, &m_data))
        close();
}
