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
#include "shell_ps.h"
#include "hook_setter.h"
#include "seh_scope.h"
#include "shared/util.h"
#include "line_editor.h"

//------------------------------------------------------------------------------
void*           get_kernel_dll();



//------------------------------------------------------------------------------
shell_ps::shell_ps(line_editor* editor)
: shell(editor)
{
}

//------------------------------------------------------------------------------
shell_ps::~shell_ps()
{
}

//------------------------------------------------------------------------------
bool shell_ps::validate()
{
    return true;
}

//------------------------------------------------------------------------------
bool shell_ps::initialise()
{
    hook_setter hooks;
    hooks.add_jmp(get_kernel_dll(), "ReadConsoleW", read_console);
    return (hooks.commit() == 1);
}

//------------------------------------------------------------------------------
void shell_ps::shutdown()
{
}

//------------------------------------------------------------------------------
BOOL WINAPI shell_ps::read_console(
    HANDLE input,
    wchar_t* chars,
    DWORD max_chars,
    LPDWORD read_in,
    void* control)
{
    seh_scope seh;

    *chars = '\0';
    shell_ps::get()->edit_line(chars, max_chars);

    size_t len = max_chars - wcslen(chars);
    wcsncat(chars, L"\x0d\x0a", len);
    chars[max_chars - 1] = '\0';

    if (read_in != nullptr)
        *read_in = (DWORD)wcslen(chars);

    return TRUE;
}

//------------------------------------------------------------------------------
void shell_ps::edit_line(wchar_t* chars, int max_chars)
{
    while (1)
        if (!get_line_editor()->edit_line(NULL, chars, max_chars))
            break;
}
