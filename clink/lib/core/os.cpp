/* Copyright (c) 2015 Martin Ridgers
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
#include "os.h"
#include "path.h"
#include "str.h"

//------------------------------------------------------------------------------
int os::get_path_type(const char* path)
{
    wstr<MAX_PATH> wpath = path;
    DWORD attr = GetFileAttributesW(wpath.c_str());
    if (attr == ~0)
        return path_type_invalid;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return path_type_dir;

    return path_type_file;
}

//------------------------------------------------------------------------------
bool os::change_dir(const char* dir)
{
    wstr<MAX_PATH> wdir(dir);
    return (SetCurrentDirectoryW(wdir.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool os::make_dir(const char* dir)
{
    str<> next;
    path::get_directory(dir, next);

    if (next.length())
        if (!make_dir(next.c_str()))
            return false;

    if (*dir)
    {
        int type = os::get_path_type(dir);
        if (type == path_type_dir)
            return true;

        wstr<MAX_PATH> wdir(dir);
        return (CreateDirectoryW(wdir.c_str(), nullptr) == TRUE);
    }

    return true;
}

//------------------------------------------------------------------------------
bool os::remove_dir(const char* dir)
{
    wstr<MAX_PATH> wdir(dir);
    return (RemoveDirectoryW(wdir.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool os::unlink(const char* path)
{
    wstr<MAX_PATH> wpath(path);
    return (DeleteFileW(wpath.c_str()) == TRUE);
}

//------------------------------------------------------------------------------
bool os::get_temp_dir(str_base& out)
{
    return get_env("tmp", out) || get_env("temp", out);
}

//------------------------------------------------------------------------------
bool os::get_env(const char* name, str_base& out)
{
    wstr<48> wname(name);
    wstr<> wvalue;
    int len = GetEnvironmentVariableW(wname.c_str(), wvalue.data(), wvalue.size());
    if (!len || len >= int(wvalue.size()))
        return false;

    out = wvalue.c_str();
    return true;
}
