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
#include "host_ps.h"
#include "hook_setter.h"
#include "process/vm.h"
#include "prompt.h"
#include "seh_scope.h"

#include <line_editor.h>

#include <Windows.h>

//------------------------------------------------------------------------------
host_ps::host_ps(line_editor* editor)
: host(editor)
{
}

//------------------------------------------------------------------------------
host_ps::~host_ps()
{
}

//------------------------------------------------------------------------------
bool host_ps::validate()
{
    return true;
}

//------------------------------------------------------------------------------
bool host_ps::initialise()
{
    void* read_console_module = get_alloc_base(ReadConsoleW);
    if (read_console_module == nullptr)
        return false;

    hook_setter hooks;
    hooks.add_jmp(read_console_module, "ReadConsoleW", read_console);
    return (hooks.commit() == 1);
}

//------------------------------------------------------------------------------
void host_ps::shutdown()
{
}

//------------------------------------------------------------------------------
BOOL WINAPI host_ps::read_console(
    HANDLE input,
    wchar_t* chars,
    DWORD max_chars,
    LPDWORD read_in,
    void* control)
{
    seh_scope seh;

    // Extract the prompt and reset the cursor to the beinging of the line
    // because it will get printed again.
    prompt prompt = prompt_utils::extract_from_console();

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);
    csbi.dwCursorPosition.X = 0;
    SetConsoleCursorPosition(handle, csbi.dwCursorPosition);

    // Edit and add the CRLF that ReadConsole() adds when called.
    *chars = '\0';
    host_ps::get()->edit_line(prompt.get(), chars, max_chars);

    size_t len = max_chars - wcslen(chars);
    wcsncat(chars, L"\x0d\x0a", len);
    chars[max_chars - 1] = '\0';

    // Done!
    if (read_in != nullptr)
        *read_in = (DWORD)wcslen(chars);

    return TRUE;
}

//------------------------------------------------------------------------------
void host_ps::edit_line(const wchar_t* prompt, wchar_t* chars, int max_chars)
{
    get_line_editor()->edit_line(prompt, chars, max_chars);
}
