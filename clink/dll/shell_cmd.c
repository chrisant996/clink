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
int             get_clink_setting_int(const char*);
static int      cmd_validate();
static int      cmd_initialise();
static void     cmd_shutdown();

BOOL WINAPI     hooked_read_console(HANDLE, wchar_t*, DWORD, LPDWORD, PCONSOLE_READCONSOLE_CONTROL);
BOOL WINAPI     hooked_write_console(HANDLE, const wchar_t*, DWORD, LPDWORD, void*);
BOOL WINAPI     hooked_read_console_input(HANDLE, INPUT_RECORD*, DWORD, LPDWORD);

shell_t         g_shell_cmd = { cmd_validate, cmd_initialise, cmd_shutdown };

//------------------------------------------------------------------------------
static int is_interactive()
{
    // Check the command line for '/c' and don't load if it's present. There's
    // no point loading clink if cmd.exe is running a command and then exiting.

    void* base;
    wchar_t** argv;
    int argc;
    int i;
    int ret;

    base = GetModuleHandle("cmd.exe");
    if (base == NULL)
    {
        return 1;
    }

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL)
    {
        return 1;
    }

    ret = 1;
    for (i = 0; i < argc; ++i)
    {
        if (wcsicmp(argv[i], L"/c") == 0)
        {
            ret = 0;
            break;
        }
    }

    LocalFree(argv);
    return ret;
}

//------------------------------------------------------------------------------
int check_auto_answer(const wchar_t* prompt)
{
    static wchar_t* prompt_to_answer = (wchar_t*)1;

    if (prompt == NULL || prompt[0] == L'\0')
    {
        return 0;
    }

    // Try and find the localised prompt.
    if (prompt_to_answer == (wchar_t*)1)
    {
        static wchar_t* fallback = L"Terminate batch job (Y/N)? ";
        static int string_id = 0x237b;

        DWORD flags;
        DWORD ok;

        flags = FORMAT_MESSAGE_ALLOCATE_BUFFER;
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
        flags |= FORMAT_MESSAGE_IGNORE_INSERTS;
        ok = FormatMessageW(
            flags, NULL, string_id, 0,
            (void*)&prompt_to_answer, 0, NULL
        );
        if (ok)
        {
            wchar_t* c = prompt_to_answer;
            while (*c)
            {
                // Strip off new line chars.
                if (*c == '\r' || *c == '\n')
                {
                    *c = '\0';
                }

                ++c;
            }

            LOG_INFO("Auto-answer prompt = '%ls'", prompt_to_answer);
        }
        else
        {
            prompt_to_answer = fallback;
            LOG_INFO("Using fallback auto-answer prompt.");
        }
    }

    if (wcscmp(prompt, prompt_to_answer) == 0)
    {
        int setting = get_clink_setting_int("terminate_autoanswer");
        if (setting > 0)
        {
            return (setting == 1) ? 'y' : 'n';
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static const char* get_kernel_dll()
{
    // We're going to use a different DLL for Win8 (and onwards).

    OSVERSIONINFOEX osvi;
    DWORDLONG mask = 0;
    int op=VER_GREATER_EQUAL;

    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2;

    VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);

    if (VerifyVersionInfo(&osvi, VER_MAJORVERSION|VER_MINORVERSION, mask))
    {
        return "kernelbase.dll";
    }
   
    return "kernel32.dll";
}

//------------------------------------------------------------------------------
static int hook_trap()
{
    void* base = GetModuleHandle(NULL);
    const char* dll = get_kernel_dll();

    hook_decl_t hooks[] = {
        { HOOK_TYPE_IAT_BY_NAME, base, NULL, "WriteConsoleW",     hooked_write_console },
        { HOOK_TYPE_JMP,         NULL, dll,  "ReadConsoleW",      hooked_read_console },
        { HOOK_TYPE_JMP,         NULL, dll,  "ReadConsoleInputA", hooked_read_console_input },
    };

    return apply_hooks(hooks, sizeof_array(hooks));
}

//------------------------------------------------------------------------------
static int cmd_validate()
{
    if (!is_interactive())
    {
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
static int cmd_initialise()
{
    const char* dll = get_kernel_dll();
    const char* func_name = "GetCurrentDirectoryW";

    if (!set_hook_trap(dll, func_name, hook_trap))
    {
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
static void cmd_shutdown()
{
}
