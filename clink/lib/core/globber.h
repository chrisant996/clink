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

#pragma once

#include "str.h"

#include <Windows.h>

//------------------------------------------------------------------------------
class globber
{
public:
    struct context
    {
        const char*     path;
        const char*     wildcard;
        bool            no_files;
        bool            no_directories;
        bool            no_dir_suffix;
        bool            hidden;
        bool            dots;
    };

                        globber(const context& ctx);
                        ~globber();
    bool                next(str_base& out);

private:
                        globber(const globber&) = delete;
    void                operator = (const globber&) = delete;
    void                next_file();
    WIN32_FIND_DATAW    m_data;
    HANDLE              m_handle;
    str<MAX_PATH>       m_root;
    context             m_context;
};
