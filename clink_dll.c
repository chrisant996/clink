/* Copyright (c) 2012 Martin Ridgers
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

#include "clink_pch.h"
#include "clink.h"
#include "clink_util.h"

//------------------------------------------------------------------------------
void                    save_history();
void                    shutdown_lua();
void                    clear_to_eol();
int                     call_readline(const wchar_t*, wchar_t*, unsigned);
int                     hook_iat(void*, const char*, const char*, void*);
int                     hook_jmp(const char*, const char*, void*);

extern int              clink_opt_ctrl_d_exit;
int                     clink_opt_alt_hooking   = 0;
static wchar_t*         g_write_cache           = NULL;
static const int        g_write_cache_size      = 0xffff;  // 0x10000 - 1 !!

//------------------------------------------------------------------------------
static const char* g_header = 
                                                                            "\n"
    "--- clink v" CLINK_VERSION " (c) 2012 Martin Ridgers"                  "\n"
    "--- http://code.google.com/p/clink"                                    "\n"
    "---"                                                                   "\n"
    "--- Copyright (c) 1994-2012 Lua.org, PUC-Rio"                          "\n"
    "--- Copyright (c) 1987-2010 Free Software Foundation, Inc."            "\n"
                                                                            "\n"
    ;

//------------------------------------------------------------------------------
static LONG WINAPI exception_filter(EXCEPTION_POINTERS* info)
{
#if defined(_MSC_VER)
    MINIDUMP_EXCEPTION_INFORMATION mdei = { GetCurrentThreadId(), info, FALSE };
    DWORD pid;
    HANDLE process;
    HANDLE file;
    char file_name[1024];

    get_config_dir(file_name, sizeof(file_name));
    str_cat(file_name, "/mini_dump.dmp", sizeof(file_name));

    fputs("\n!!! CLINK'S CRASHED!", stderr);
    fputs("\n!!! Something went wrong.", stderr);
    fputs("\n!!! Writing mini dump file to: ", stderr);
    fputs(file_name, stderr);
    fputs("\n", stderr);

    file = CreateFile(file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        pid = GetCurrentProcessId();
        process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (process != NULL)
        {
            MiniDumpWriteDump(process, pid, file, MiniDumpNormal, &mdei, NULL, NULL);
        }
        CloseHandle(process);
    }
    CloseHandle(file);
#endif // _MSC_VER

    // Would be awesome if we could unhook ourself, unload, and allow cmd.exe
    // to continue!

    return EXCEPTION_EXECUTE_HANDLER;
}

//------------------------------------------------------------------------------
static void emulate_doskey(wchar_t* buffer, DWORD max_size)
{
    // ReadConsoleW() will postprocess what the user enters, resolving any
    // aliases that may be registered (aka doskey macros). As we've skipped this
    // step, we have to resolve aliases ourselves.

    wchar_t exe_buf[MAX_PATH];
    wchar_t* exe;
    wchar_t* slash;

    GetModuleFileNameW(NULL, exe_buf, sizeof(exe_buf));
    slash = wcsrchr(exe_buf, L'\\');
    exe = (slash != NULL) ? (slash + 1) : exe_buf;

    GetConsoleAliasW(buffer, buffer, max_size, exe);
}

//------------------------------------------------------------------------------
static void append_crlf(wchar_t* buffer, DWORD max_size)
{
    // Cmd.exe expects a CRLF combo at the end of the string, otherwise it
    // thinks the line is part of a multi-line command.

    size_t len;

    len = max_size - wcslen(buffer);
    wcsncat(buffer, L"\x0d\x0a", len);
    buffer[max_size - 1] = L'\0';
}

//------------------------------------------------------------------------------
static BOOL WINAPI hooked_read_console(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    PCONSOLE_READCONSOLE_CONTROL control
)
{
    const wchar_t* prompt;
    int is_eof;
    LPTOP_LEVEL_EXCEPTION_FILTER old_seh;

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline. This saves hacking around it...
    if (buffer_size == 1)
    {
        return ReadConsoleW(input, buffer, buffer_size, read_in, control);
    }

    old_seh = SetUnhandledExceptionFilter(exception_filter);

    // In multi-line prompt situations, we're only interested in the last line.
    prompt = wcsrchr(g_write_cache, '\n');
    prompt = (prompt != NULL) ? (prompt + 1) : g_write_cache;

    // Call readline.
    is_eof = call_readline(prompt, buffer, buffer_size);
    if (is_eof && clink_opt_ctrl_d_exit)
    {
        wcsncpy(buffer, L"exit", buffer_size);
    }
    g_write_cache[0] = L'\0';

    // Check for control codes and convert them.
    if (buffer[0] == L'\x03')
    {
        SetUnhandledExceptionFilter(old_seh);

        // Fire a Ctrl-C exception. Cmd.exe sets a global variable (CtrlCSeen)
        // and ReadConsole() would normally set error code 0x3e3. Sleep() is to
        // yield the thread so the global gets set (guess work...).
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        SetLastError(0x3e3);
        Sleep(0);

        buffer[0] = '\0';
        old_seh = SetUnhandledExceptionFilter(exception_filter);
    }
    else
    {
        emulate_doskey(buffer, buffer_size);
        append_crlf(buffer, buffer_size);
    }

    SetUnhandledExceptionFilter(old_seh);

    *read_in = (unsigned)wcslen(buffer);
    return TRUE;
}

//------------------------------------------------------------------------------
static void print_header()
{
    clear_to_eol();
    hooked_fprintf(NULL, "%s", g_header);
}

//------------------------------------------------------------------------------
static BOOL WINAPI hooked_write_console(
    HANDLE output,
    const wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD written,
    void* unused
)
{
    static int once = 0;
    int copy_size;

    if (!once)
    {
        print_header();

        g_write_cache = VirtualAlloc(
            NULL,
            g_write_cache_size + 1,
            MEM_COMMIT,
            PAGE_READWRITE
        );

        once = 1;
    }

    copy_size = (g_write_cache_size < buffer_size)
        ? g_write_cache_size
        : buffer_size;
    memcpy(g_write_cache, buffer, copy_size * sizeof(wchar_t));
    g_write_cache[copy_size] = L'\0';

    return WriteConsoleW(output, buffer, buffer_size, written, unused);
}

//------------------------------------------------------------------------------
static void prepare_env_for_inputrc(HINSTANCE instance)
{
    // Give readline a chance to find the inputrc by modifying the
    // environment slightly.

    static const char inputrc_eq[] = "INPUTRC=";
    char* slash;
    char buffer[1024];

    // Unless set to something else, set the environment variable "HOME"
    // to the user's profile folder.
    if (getenv("HOME") == NULL)
    {
        const char* user_profile = getenv("APPDATA");
        if (user_profile != NULL)
        {
            strcpy(buffer, "HOME=");
            str_cat(buffer, user_profile, sizeof(buffer));
        }

        putenv(buffer);
    }

    // Set INPUTRC variable to be a .inputrc file that's alongside this DLL.
    strcpy(buffer, inputrc_eq);
    get_dll_dir(
        buffer + sizeof(inputrc_eq) - 1,
        sizeof(buffer) - sizeof(inputrc_eq)
    );

    str_cat(buffer, "/clink_inputrc", sizeof(buffer));

    putenv(buffer);
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
static int apply_hooks(void* base)
{
    int i;

    struct hook_t
    {
        const char* dlls;
        const char* func_name;
        void* hook;
    };

    struct hook_t hooks[] = {
        { get_kernel_dll(), "ReadConsoleW", hooked_read_console },
        { get_kernel_dll(), "WriteConsoleW", hooked_write_console }
    };

    for (i = 0; i < sizeof_array(hooks); ++i)
    {
        struct hook_t* hook = hooks + i;
        int hook_ok = 0;

        LOG_INFO("----------------------");
        LOG_INFO("Hooking '%s' in '%s'", hook->func_name, hook->dlls);

        // Hook method 1.
        if (!clink_opt_alt_hooking)
        {
            hook_ok = hook_iat(base, hook->dlls, hook->func_name, hook->hook);
            if (hook_ok)
            {
                continue;
            }

            LOG_INFO("Unable to hook IAT. Trying fallback method.\n");
        }

        // Hook method 2.
        hook_ok = hook_jmp(hook->dlls, hook->func_name, hook->hook);
        if (!hook_ok)
        {
            LOG_ERROR("Failed to hook '%s'", hook->func_name);
            return 0;
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
static void* validate_parent_process()
{
    void* base;
     
    base = (void*)GetModuleHandle("cmd.exe");
    if (base == NULL)
    {
        LOG_INFO("Failed to find base address for 'cmd.exe'.");
    }
    else
    {
        LOG_INFO("Found base address for executable at %p.", base);
    }

    return base;
}

//------------------------------------------------------------------------------
static void failed()
{
    char buffer[1024];

    get_config_dir(buffer, sizeof(buffer));
    printf(
        "%s\n"
        "--- !!!\n"
        "--- !!! Sadly, clink failed to load.\n"
        "--- !!! Log file; %s\\clink.log\n"
        "--- !!!\n",
        g_header,
        buffer
    );
}

//------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID unused)
{
    void* base;

    // We're only interested in when the dll is attached.
    if (reason != DLL_PROCESS_ATTACH)
    {
        if (reason == DLL_PROCESS_DETACH)
        {
            save_history();
            shutdown_lua();
        }

        return TRUE;
    }

    prepare_env_for_inputrc(instance);

    base = validate_parent_process();
    if (base == NULL)
    {
        failed();
        return FALSE;
    }

    if (!apply_hooks(base))
    {
        failed();
        return FALSE;
    }

    return TRUE;
}
