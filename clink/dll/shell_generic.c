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
#include "shell.h"
#include "dll_hooks.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
int             generic_validate();
int             generic_initialise(void*);
void            generic_shutdown();
const char*     get_kernel_dll();
void*           push_exception_filter();
void            pop_exception_filter(void* old_filter);
int             call_readline_w(const wchar_t*, wchar_t*, unsigned);
void            append_crlf(wchar_t*, unsigned);

shell_t         g_shell_generic = {
                    generic_validate,
                    generic_initialise,
                    generic_shutdown };

//------------------------------------------------------------------------------
static BOOL WINAPI read_console_w(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    PCONSOLE_READCONSOLE_CONTROL control
)
{
    void* old_exception_filter;

    old_exception_filter = push_exception_filter();
    call_readline_w(NULL, buffer, buffer_size);
    append_crlf(buffer, buffer_size);
    pop_exception_filter(old_exception_filter);

    *read_in = (unsigned)wcslen(buffer);
    return TRUE;
}

//------------------------------------------------------------------------------
static int initialise_w(void* base)
{
    const char* dll = get_kernel_dll();

    hook_decl_t hooks[] = {
        { HOOK_TYPE_JMP,         NULL, dll,  "ReadConsoleW",    read_console_w },
        //{ HOOK_TYPE_IAT_BY_NAME, base, NULL, "WriteConsoleW",  write_console_w },
    };

    return apply_hooks(hooks, sizeof_array(hooks));
}

//------------------------------------------------------------------------------
int generic_validate()
{
    return 1;
}

//------------------------------------------------------------------------------
int generic_initialise(void* base)
{
    return initialise_w(base);
}

//------------------------------------------------------------------------------
void generic_shutdown()
{
}

// vim: expandtab
