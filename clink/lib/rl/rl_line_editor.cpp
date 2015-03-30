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
#include "rl_line_editor.h"

int call_readline_w(const wchar_t*, wchar_t*, unsigned);

extern "C" {

//------------------------------------------------------------------------------
line_editor_t* initialise_rl_line_editor()
{
    return (line_editor_t*)(new rl_line_editor());
}

//------------------------------------------------------------------------------
void shutdown_rl_line_editor(line_editor_t* line_editor)
{
    delete (rl_line_editor*)line_editor;
}

} // extern "C"



//------------------------------------------------------------------------------
rl_line_editor::rl_line_editor()
{
}

//------------------------------------------------------------------------------
rl_line_editor::~rl_line_editor()
{
}

//------------------------------------------------------------------------------
bool rl_line_editor::edit_line(const wchar_t* prompt, wchar_t* out, int out_size)
{
    return (call_readline_w(prompt, out, out_size) != 0);
}

//------------------------------------------------------------------------------
const char* rl_line_editor::get_shell_name() const
{
    return rl_readline_name;
}

//------------------------------------------------------------------------------
void rl_line_editor::set_shell_name(const char* name)
{
    rl_readline_name = name;
}

// vim: expandtab
