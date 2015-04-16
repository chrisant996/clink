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
line_editor*    g_line_editor       = nullptr;
void*           get_kernel_dll();



//------------------------------------------------------------------------------
static void append_crlf(wchar_t* buffer, unsigned max_size)
{
    size_t len;
    len = max_size - wcslen(buffer);
    wcsncat(buffer, L"\x0d\x0a", len);
    buffer[max_size - 1] = L'\0';
}

//------------------------------------------------------------------------------
static BOOL WINAPI read_console(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_count,
    LPDWORD read_in,
    void* control
)
{
    seh_scope seh;

    while (1)
        if (!g_line_editor->edit_line(NULL, buffer, buffer_count))
            break;

    append_crlf(buffer, buffer_count);
    *read_in = (unsigned)wcslen(buffer);
    return TRUE;
}



//------------------------------------------------------------------------------
shell_ps::shell_ps(line_editor* editor)
: shell(editor)
{
    g_line_editor = editor;
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
