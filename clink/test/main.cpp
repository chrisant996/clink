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

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static const char* test_fs[] = {
    "file1",
    "file2",
    "case_map-1",
    "case_map_2",
    "dir1/only",
    "dir1/file1",
    "dir1/file2",
    "dir2/",
};

struct test_env
{
    str<> m_root;

    test_env()
    {
        os::get_env("tmp", m_root);
        m_root << "/clink_test";

        os::make_dir(m_root.c_str());
        os::change_dir(m_root.c_str());

        for (int i = 0; i < sizeof_array(test_fs); ++i)
        {
            const char* file = test_fs[i];

            str<> dir;
            path::get_directory(file, dir);
            os::make_dir(dir.c_str());

            if (FILE* f = fopen(file, "wt"))
                fclose(f);
        }
    }

    ~test_env()
    {
        os::change_dir(m_root.c_str());
        for (int i = 0; i < sizeof_array(test_fs); ++i)
            unlink(test_fs[i]);

        for (int i = sizeof_array(test_fs) - 1; i >= 0; --i)
        {
            str<> dir;
            path::get_directory(test_fs[i], dir);
            os::remove_dir(dir.c_str());
        }

        os::remove_dir(m_root.c_str());
    }
};

int main(int argc, char** argv)
{
    test_env env;
    int result = Catch::Session().run(argc, argv);
    return result;
}
