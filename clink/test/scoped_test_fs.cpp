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

#include "scoped_test_fs.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>

//------------------------------------------------------------------------------
static const char* g_default_fs[] = {
    "file1",
    "file2",
    "case_map-1",
    "case_map_2",
    "dir1/only",
    "dir1/file1",
    "dir1/file2",
    "dir2/",
    nullptr,
};

//------------------------------------------------------------------------------
scoped_test_fs::scoped_test_fs(const char** fs)
{
    os::get_env("tmp", m_root);
    m_root << "/clink_test";

    os::make_dir(m_root.c_str());
    os::change_dir(m_root.c_str());

    if (fs == nullptr)
        fs = g_default_fs;

    const char** item = fs;
    while (const char* file = *item++)
    {
        str<> dir;
        path::get_directory(file, dir);
        os::make_dir(dir.c_str());

        if (FILE* f = fopen(file, "wt"))
            fclose(f);
    }

    m_fs = fs;
}

//------------------------------------------------------------------------------
scoped_test_fs::~scoped_test_fs()
{
    os::change_dir(m_root.c_str());

    const char** item = m_fs;
    while (const char* file = *item)
    {
        unlink(file);
        ++item;
    }

    --item;
    while (item >= m_fs)
    {
        str<> dir;
        path::get_directory(*item, dir);
        os::remove_dir(dir.c_str());
        --item;
    }

    os::remove_dir(m_root.c_str());
}
