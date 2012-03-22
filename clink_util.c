/* Copyright (c) 2012 Martin Ridgers
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

#include "clink_pch.h"

//------------------------------------------------------------------------------
void str_cat(char* dest, const char* src, int n)
{
    int m = n - (int)strlen(dest);
    if (m > 0)
    {
        strncat(dest, src, m);
    }
    dest[n - 1] = '\0';
}

//------------------------------------------------------------------------------
void get_config_dir(char* buffer, int size)
{
    static int once = 1;
    const char* app_dir;

    buffer[0] = '\0';

    app_dir = getenv("ALLUSERSPROFILE");
    if (app_dir == NULL)
    {
        return;
    }

    str_cat(buffer, app_dir, size);
    str_cat(buffer, "/clink", size);

    if (once)
    {
        CreateDirectory(buffer, NULL);
        once = 0;
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
}
