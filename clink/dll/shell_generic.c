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
#include "inject_args.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
int                     generic_validate();
int                     generic_initialise(void*);
void                    generic_shutdown();
const char*             get_kernel_dll();
void*                   push_exception_filter();
void                    pop_exception_filter(void* old_filter);
int                     call_readline_w(const wchar_t*, wchar_t*, unsigned);
int                     call_readline_utf8(const char*, char*, unsigned);

extern inject_args_t    g_inject_args;
shell_t                 g_shell_generic = {
                            generic_validate,
                            generic_initialise,
                            generic_shutdown };

//------------------------------------------------------------------------------
static void append_crlf_w(wchar_t* buffer, unsigned max_size)
{
    size_t len;
    len = max_size - wcslen(buffer);
    wcsncat(buffer, L"\x0d\x0a", len);
    buffer[max_size - 1] = L'\0';
}

//------------------------------------------------------------------------------
static void append_crlf_a(char* buffer, unsigned max_size)
{
    size_t len;
    len = max_size - strlen(buffer);
    strncat(buffer, "\x0d\x0a", len);
    buffer[max_size - 1] = '\0';
}

//------------------------------------------------------------------------------
static BOOL WINAPI read_console_w(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    void* control
)
{
    int is_eof;
    void* old_exception_filter;

    old_exception_filter = push_exception_filter();
    do
    {
        is_eof = call_readline_w(NULL, buffer, buffer_size);
    }
    while (is_eof);
    pop_exception_filter(old_exception_filter);

    append_crlf_w(buffer, buffer_size);
    *read_in = (unsigned)wcslen(buffer);
    return TRUE;
}

//------------------------------------------------------------------------------
static BOOL WINAPI read_console_a(
    HANDLE input,
    char* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    void* control
)
{
    int is_eof;
    void* old_exception_filter;

    old_exception_filter = push_exception_filter();
    do
    {
        is_eof = call_readline_utf8(NULL, buffer, buffer_size);
    }
    while (is_eof);
    pop_exception_filter(old_exception_filter);

    append_crlf_a(buffer, buffer_size);
    *read_in = (unsigned)strlen(buffer);
    return TRUE;
}

//------------------------------------------------------------------------------
static int initialise(void* base, const char* read_func, void* hook_func)
{
    const char* dll = get_kernel_dll();

    hook_decl_t hooks[] = {
        { HOOK_TYPE_JMP, NULL, dll, (void*)read_func, hook_func },
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
    if (g_inject_args.ansi_mode)
    {
        return initialise(base, "ReadConsoleA", read_console_a);
    }

    return initialise(base, "ReadConsoleW", read_console_w);
}

//------------------------------------------------------------------------------
void generic_shutdown()
{
}

// vim: expandtab
