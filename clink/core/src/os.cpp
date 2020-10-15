// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os.h"
#include "path.h"
#include "str.h"

namespace os
{

//------------------------------------------------------------------------------
DWORD get_file_attributes(const wchar_t* path)
{
    // Strip trailing path separators because FindFirstFile can't handle them.
    wchar_t buf[MAX_PATH * 2];
    if (*path && path::is_separator(path[wcslen(path) - 1]))
    {
        wstr_base str(buf, sizeof(buf));
        str.copy(path);
        while (str.length() && path::is_separator(str.c_str()[str.length() - 1]))
            str.truncate(str.length() - 1);
        path = buf;
    }

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
        return false;

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

}; // namespace os
