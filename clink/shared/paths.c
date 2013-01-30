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

//------------------------------------------------------------------------------
static const char* g_config_dir_override = NULL;

//------------------------------------------------------------------------------
void normalise_path_format(char* buffer, int size)
{
    char* slash;

    GetShortPathName(buffer, buffer, size);

    slash = strrchr(buffer, '\\');
    if (slash != NULL && slash[1] == '\0')
    {
        *slash = '\0';
    }
}

//------------------------------------------------------------------------------
void get_dll_dir(char* buffer, int size)
{
    BOOL ok;
    HINSTANCE module;
    char* slash;

    buffer[0] = '\0';

    ok = GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (char*)get_dll_dir,
        &module
    );

    if (ok == FALSE)
    {
        return;
    }

    GetModuleFileName(module, buffer, size);
    slash = strrchr(buffer, '\\');
    if (slash != NULL)
    {
        *slash = '\0';
    }

    normalise_path_format(buffer, size);
}

//------------------------------------------------------------------------------
void get_config_dir(char* buffer, int size)
{
    static int once = 1;

    // Maybe the user specified an alternative location?
    if (g_config_dir_override != NULL)
    {
        str_cpy(buffer, g_config_dir_override, size);
    }
    else
    {
        get_dll_dir(buffer, size);
        str_cat(buffer, ".\\profile", size);
    }

    // Try and create the directory if it doesn't already exist. Just this once.
    if (once)
    {
        CreateDirectory(buffer, NULL);
        once = 0;
    }

    normalise_path_format(buffer, size);
}

//------------------------------------------------------------------------------
void set_config_dir_override(const char* dir)
{
    g_config_dir_override = dir;
}

//------------------------------------------------------------------------------
void get_log_dir(char* buffer, int size)
{
    static once = 1;
    static char log_dir[MAX_PATH] = { 1 };

    // Just the once, get user's appdata folder.
    if (log_dir[0] == 1)
    {
        if (SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, NULL, 0, log_dir) != S_OK)
        {
            const char* str;
            if ((str = getenv("USERPROFILE")) == NULL)
            {
                GetTempPath(sizeof_array(log_dir), log_dir);
            }

            str_cpy(log_dir, str, sizeof_array(log_dir));
        }

        str_cat(log_dir, "./clink", sizeof_array(log_dir));
    }

    str_cpy(buffer, log_dir, size);

    // Try and create the directory if it doesn't already exist. Just this once.
    if (once)
    {
        CreateDirectory(buffer, NULL);
        once = 0;
    }

    normalise_path_format(buffer, size);
}

//------------------------------------------------------------------------------
void cpy_path_as_abs(char* abs, const char* rel, int abs_size)
{
    char* ret;

    ret = _fullpath(abs, rel, abs_size);
    if (ret == NULL)
    {
        str_cpy(abs, rel, abs_size);
    }

    normalise_path_format(abs, abs_size);
}

// vim: expandtab
