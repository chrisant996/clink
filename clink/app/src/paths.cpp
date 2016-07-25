// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <core/path.h>

//------------------------------------------------------------------------------
static str<256> g_config_dir_override;

//------------------------------------------------------------------------------
void get_dll_dir(str_base& buffer)
{
    buffer.clear();

    HINSTANCE module;
    BOOL ok = GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (char*)get_dll_dir,
        &module
    );

    if (ok == FALSE)
        return;

    GetModuleFileName(module, buffer.data(), buffer.size());
    int slash = buffer.last_of('\\');
    if (slash >= 0)
        buffer.truncate(slash);

    path::clean(buffer);
}

//------------------------------------------------------------------------------
void get_config_dir(str_base& buffer)
{
    static int once = 1;

    // Maybe the user specified an alternative location?
    if (g_config_dir_override.empty())
    {
        get_dll_dir(buffer);
        buffer << ".\\profile";
    }
    else
        buffer << g_config_dir_override;

    // Try and create the directory if it doesn't already exist. Just this once.
    if (once)
    {
        CreateDirectory(buffer.c_str(), nullptr);
        once = 0;
    }

    path::clean(buffer);
}

//------------------------------------------------------------------------------
void set_config_dir_override(const char* dir)
{
#if MODE4
    g_config_dir_override.copy(dir);
#endif // MODE4
}

//------------------------------------------------------------------------------
void get_log_dir(str_base& buffer)
{
    static int once = 1;
    static str<MAX_PATH> log_dir;

    // Just the once, get user's appdata folder.
    if (once)
    {
        if (SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, nullptr, 0, log_dir.data()) != S_OK)
        {
            if (const char* str = getenv("USERPROFILE"))
                log_dir << str;
            else
                GetTempPath(log_dir.size(), log_dir.data());
        }

        log_dir << "/clink";
        path::clean(log_dir);
    }

    buffer << log_dir;

    // Try and create the directory if it doesn't already exist. Just this once.
    if (once)
    {
        CreateDirectory(buffer.c_str(), nullptr);
        once = 0;
    }

    path::clean(buffer);
}

//------------------------------------------------------------------------------
void cpy_path_as_abs(str_base& abs, const char* rel)
{
    char* ret = _fullpath(abs.data(), rel, abs.size());
    if (ret == nullptr)
        abs.copy(rel);

    path::clean(abs);
}
