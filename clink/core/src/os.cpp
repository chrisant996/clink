// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os.h"
#include "path.h"
#include "str.h"
#include <locale.h>

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
DWORD get_file_attributes(const wchar_t* path)
{
    // FindFirstFileW can handle cases that GetFileAttributesW can't (e.g. files
    // open exclusively, some hidden/system files in the system root directory).
    // But it can't handle a root directory, so if the incoming path ends with a
    // separator then use GetFileAttributesW instead.
    if (*path && path::is_separator(path[wcslen(path) - 1]))
        return GetFileAttributesW(path);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return INVALID_FILE_ATTRIBUTES;

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
        return -1;

    int ret = GetFileSize(handle, nullptr); // 2Gb max I suppose...
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
    return (SetCurrentDirectoryW(wdir.c_str()) == TRUE);
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
        return (CreateDirectoryW(wdir.c_str(), nullptr) == TRUE);
    }

    return true;
}

//------------------------------------------------------------------------------
bool remove_dir(const char* dir)
{
    wstr<280> wdir(dir);
    return (RemoveDirectoryW(wdir.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool unlink(const char* path)
{
    wstr<280> wpath(path);
    return (DeleteFileW(wpath.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool move(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    return (MoveFileW(wsrc_path.c_str(), wdest_path.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool copy(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    return (CopyFileW(wsrc_path.c_str(), wdest_path.c_str(), FALSE) == TRUE);
}

//------------------------------------------------------------------------------
bool get_temp_dir(str_base& out)
{
    wstr<280> wout;
    unsigned int size = GetTempPathW(wout.size(), wout.data());
    if (!size)
        return false;

    if (size >= wout.size())
    {
        wout.reserve(size);
        if (!GetTempPathW(wout.size(), wout.data()))
            return false;
    }

    out = wout.c_str();
    return true;
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
    return (SetEnvironmentVariableW(wname.c_str(), value_arg) != 0);
}

//------------------------------------------------------------------------------
bool get_alias(const char* name, str_base& out)
{
    wstr<32> alias_name;
    alias_name = name;

    wchar_t exe_path[280];
    if (GetModuleFileNameW(NULL, exe_path, sizeof_array(exe_path)) == 0)
        return false;

    wstr<32> exe_name;
    exe_name = path::get_name(exe_path);

    // Get the alias (aka. doskey macro).
    wstr<32> buffer;
    buffer.reserve(8191);
    if (GetConsoleAliasW(alias_name.data(), buffer.data(), buffer.size(), exe_name.data()) == 0)
        return false;

    if (!buffer.length())
        return false;

    out = buffer.c_str();
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
