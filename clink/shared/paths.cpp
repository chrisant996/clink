/* Copyright (c) 2013 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "util.h"

#include <core/str.h>

//------------------------------------------------------------------------------
static str<256> g_config_dir_override;

//------------------------------------------------------------------------------
void normalise_path_format(str_base& buffer)
{
    GetShortPathName(buffer.data(), buffer.data(), buffer.size());

    int len = buffer.length();
    if (len && (buffer[len - 1] == '\\' || buffer[len - 1] == '/'))
        buffer.truncate(len - 1);
}

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

    normalise_path_format(buffer);
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

    normalise_path_format(buffer);
}

//------------------------------------------------------------------------------
void set_config_dir_override(const char* dir)
{
    g_config_dir_override.copy(dir);
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

        log_dir << "\\clink";
    }

    buffer << log_dir;

    // Try and create the directory if it doesn't already exist. Just this once.
    if (once)
    {
        CreateDirectory(buffer.c_str(), nullptr);
        once = 0;
    }

    normalise_path_format(buffer);
}

//------------------------------------------------------------------------------
void cpy_path_as_abs(str_base& abs, const char* rel)
{
    char* ret = _fullpath(abs.data(), rel, abs.size());
    if (ret == nullptr)
        abs.copy(rel);

    normalise_path_format(abs);
}
