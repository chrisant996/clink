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
#include "line_editor.h"

//------------------------------------------------------------------------------
class cwd_restorer
{
public:
                    cwd_restorer();
                    ~cwd_restorer();

private:
    wstr<MAX_PATH>  m_path;
};

//------------------------------------------------------------------------------
cwd_restorer::cwd_restorer()
{
    GetCurrentDirectoryW(m_path.size(), m_path.data());
}

//------------------------------------------------------------------------------
cwd_restorer::~cwd_restorer()
{
    SetCurrentDirectoryW(m_path.c_str());
}



//------------------------------------------------------------------------------
line_editor::line_editor(const desc& desc)
: m_terminal(desc.term)
, m_match_printer(desc.match_printer)
, m_match_generator(desc.match_generator)
{
    m_shell_name << desc.shell_name;
}

//------------------------------------------------------------------------------
bool line_editor::edit_line(const wchar_t* prompt, wchar_t* out, int out_count)
{
    cwd_restorer cwd;
    return edit_line_impl(prompt, out, out_count);
}
