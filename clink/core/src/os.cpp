// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os.h"
#include "path.h"
#include "str.h"
#include "str_iter.h"
#include <locale.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>

//------------------------------------------------------------------------------
extern "C" void __cdecl __acrt_errno_map_os_error(unsigned long const oserrno);

//------------------------------------------------------------------------------
// We use UTF8 everywhere, and we need to tell the CRT so that mbrtowc and etc
// use UTF8 instead of the default CRT pseudo-locale.
class auto_set_locale_utf8
{
public:
    auto_set_locale_utf8() { setlocale(LC_ALL, ".utf8"); }
};
static auto_set_locale_utf8 s_auto_utf8;



namespace os
{

//------------------------------------------------------------------------------
void map_errno() { __acrt_errno_map_os_error(GetLastError()); }
void map_errno(unsigned long const oserrno) { __acrt_errno_map_os_error(oserrno); }

//------------------------------------------------------------------------------
static int s_errorlevel = 0;
void set_errorlevel(int errorlevel) { s_errorlevel = errorlevel; }
int get_errorlevel() { return s_errorlevel; }

//------------------------------------------------------------------------------
DWORD get_file_attributes(const wchar_t* path)
{
    // FindFirstFileW can handle cases that GetFileAttributesW can't (e.g. files
    // open exclusively, some hidden/system files in the system root directory).
    // But it can't handle a root directory, so if the incoming path ends with a
    // separator then use GetFileAttributesW instead.
    if (*path && path::is_separator(path[wcslen(path) - 1]))
    {
        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
error:
            map_errno();
            return INVALID_FILE_ATTRIBUTES;
        }
        return attr;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        goto error;

    FindClose(h);
    return fd.dwFileAttributes;
}

//------------------------------------------------------------------------------
DWORD get_file_attributes(const char* path)
{
    wstr<280> wpath(path);
    return get_file_attributes(wpath.c_str());
}

//------------------------------------------------------------------------------
int get_path_type(const char* path)
{
    DWORD attr = get_file_attributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return path_type_invalid;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return path_type_dir;

    return path_type_file;
}

//------------------------------------------------------------------------------
bool is_hidden(const char* path)
{
    DWORD attr = get_file_attributes(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN));
}

//------------------------------------------------------------------------------
int get_file_size(const char* path)
{
    wstr<280> wpath(path);
    void* handle = CreateFileW(wpath.c_str(), 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        map_errno();
        return -1;
    }

    int ret = GetFileSize(handle, nullptr); // 2Gb max I suppose...
    if (ret == INVALID_FILE_SIZE)
        map_errno();
    CloseHandle(handle);
    return ret;
}

//------------------------------------------------------------------------------
void get_current_dir(str_base& out)
{
    wstr<280> wdir;
    GetCurrentDirectoryW(wdir.size(), wdir.data());
    out = wdir.c_str();
}

//------------------------------------------------------------------------------
bool set_current_dir(const char* dir)
{
    wstr<280> wdir(dir);
    if (SetCurrentDirectoryW(wdir.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool make_dir(const char* dir)
{
    int type = get_path_type(dir);
    if (type == path_type_dir)
        return true;

    str<> next;
    path::get_directory(dir, next);

    if (!next.empty() && !path::is_root(next.c_str()))
        if (!make_dir(next.c_str()))
            return false;

    if (*dir)
    {
        wstr<280> wdir(dir);
        if (CreateDirectoryW(wdir.c_str(), nullptr))
            return true;
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool remove_dir(const char* dir)
{
    wstr<280> wdir(dir);
    if (RemoveDirectoryW(wdir.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool unlink(const char* path)
{
    wstr<280> wpath(path);
    if (DeleteFileW(wpath.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool move(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    if (MoveFileW(wsrc_path.c_str(), wdest_path.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool copy(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    if (CopyFileW(wsrc_path.c_str(), wdest_path.c_str(), FALSE))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool get_temp_dir(str_base& out)
{
    wstr<280> wout;
    unsigned int size = GetTempPathW(wout.size(), wout.data());
    if (!size)
    {
error:
        map_errno();
        return false;
    }

    if (size >= wout.size())
    {
        wout.reserve(size);
        if (!GetTempPathW(wout.size(), wout.data()))
            goto error;
    }

    out = wout.c_str();
    return true;
}

//------------------------------------------------------------------------------
FILE* create_temp_file(str_base* out, const char* _prefix, const char* _ext, temp_file_mode _mode, const char* _path)
{
    if (out)
        out->clear();

    // Start with base path.
    str<> path(_path);
    if (!path.length() && !get_temp_dir(path))
        return nullptr;

    // Append up to 8 UTF32 characters from prefix.
    str<> prefix(_prefix);
    str_iter iter(prefix);
    for (int i = 8; i--; iter.next());
    prefix.truncate(static_cast<unsigned int>(iter.get_pointer() - prefix.c_str()));
    if (!prefix.length())
        prefix.copy("tmp");
    path::append(path, prefix.c_str());

    // Append process ID.
    prefix.format("_%X_", GetCurrentProcessId());
    path.concat(prefix.c_str());

    // Remember the base path and prefix length.
    wstr<> wpath(path.c_str());
    unsigned int base_len = wpath.length();

    // Open mode.
    wstr<16> mode(L"w+xT");
    if (_mode & os::binary) mode << L"b";
    if (_mode & os::delete_on_close) mode << L"D";

    // Create unique temp file, iterating if necessary.
    FILE* f = nullptr;
    errno_t err = EINVAL;
    wstr<> wunique;
    wstr<> wext(_ext);
    srand(GetTickCount());
    unsigned unique = (rand() & 0xff) + ((rand() & 0xff) << 8);
    for (unsigned attempts = 0xffff + 1; attempts--;)
    {
        wunique.format(L"%04.4X", unique);
        wpath << wunique;
        wpath << wext;

        f = _wfsopen(wpath.c_str(), mode.c_str(), _SH_DENYNO);
        if (f)
            break;
        _get_errno(&err);
        if (err == EINVAL || err == EMFILE)
            break;

        unique++;
        wpath.truncate(base_len);
    }

    if (out)
        to_utf8(*out, wstr_iter(wpath.c_str(), wpath.length()));

    if (!f)
        map_errno(ERROR_NO_MORE_FILES);

    return f;
}

//------------------------------------------------------------------------------
bool get_env(const char* name, str_base& out)
{
    wstr<32> wname(name);

    int len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (!len)
    {
        if (stricmp(name, "HOME") == 0)
        {
            str<> a;
            str<> b;
            if (get_env("HOMEDRIVE", a) && get_env("HOMEPATH", b))
            {
                out.clear();
                out << a.c_str() << b.c_str();
                return true;
            }
            else if (get_env("USERPROFILE", out))
            {
                return true;
            }
        }
        else if (stricmp(name, "ERRORLEVEL") == 0)
        {
            out.clear();
            out.format("%d", os::get_errorlevel());
            return true;
        }

        map_errno();
        return false;
    }

    wstr<> wvalue;
    wvalue.reserve(len);
    len = GetEnvironmentVariableW(wname.c_str(), wvalue.data(), wvalue.size());

    out.reserve(len);
    out = wvalue.c_str();
    return true;
}

//------------------------------------------------------------------------------
bool set_env(const char* name, const char* value)
{
    wstr<32> wname(name);

    wstr<64> wvalue;
    if (value != nullptr)
        wvalue = value;

    const wchar_t* value_arg = (value != nullptr) ? wvalue.c_str() : nullptr;
    // REVIEW: Setting the C runtime cached environment is not necessary in
    // general.  But does Lua use the C runtime cached environment when spawning
    // child processes...?  If so then none of the other environment variable
    // changes in the CMD.EXE host process will show up either...
    //_wputenv_s(wname.c_str(), wvalue.c_str());
    if (SetEnvironmentVariableW(wname.c_str(), value_arg) != 0)
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool get_alias(const char* name, str_base& out)
{
    wstr<32> alias_name;
    alias_name = name;

    // Get the alias (aka. doskey macro).
    wstr<32> buffer;
    buffer.reserve(8191);
    if (GetConsoleAliasW(alias_name.data(), buffer.data(), buffer.size(), L"cmd.exe") == 0)
    {
        map_errno();
        return false;
    }

    if (!buffer.length())
    {
        errno = 0;
        return false;
    }

    out = buffer.c_str();
    return true;
}

//------------------------------------------------------------------------------
bool get_short_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    unsigned int len = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetShortPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
            to_utf8(out, wstr_iter(wout.c_str(), wout.length()));
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_long_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    unsigned int len = GetLongPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetLongPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
            to_utf8(out, wstr_iter(wout.c_str(), wout.length()));
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_full_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    unsigned int len = GetFullPathNameW(wpath.c_str(), 0, nullptr, nullptr);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetFullPathNameW(wpath.c_str(), wout.size() - 1, wout.data(), nullptr);
        if (len)
            to_utf8(out, wstr_iter(wout.c_str(), wout.length()));
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

}; // namespace os

//------------------------------------------------------------------------------
#if defined(DEBUG)
int dbg_get_env_int(const char* name)
{
    char tmp[32];
    int len = GetEnvironmentVariableA(name, tmp, sizeof(tmp));
    int val = (len > 0 && len < sizeof(tmp)) ? atoi(tmp) : 0;
    return val;
}
#endif
