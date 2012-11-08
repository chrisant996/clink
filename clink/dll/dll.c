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

#include "pch.h"
#include "shared/util.h"
#include "shared/inject_args.h"

//------------------------------------------------------------------------------
struct write_cache_
{
    wchar_t*            buffer;
    int                 size;
};

typedef struct write_cache_ write_cache_t;

//------------------------------------------------------------------------------
int                     set_hook_trap();
void                    save_history();
void                    shutdown_lua();
void                    clear_to_eol();
void                    emulate_doskey(wchar_t*, unsigned);
int                     call_readline(const wchar_t*, wchar_t*, unsigned);
void                    shutdown_clink_settings();
int                     get_clink_setting_int(const char*);

static int              g_write_cache_index     = 0;
static const int        g_write_cache_size      = 0xffff;      // 0x10000 - 1 !!
static write_cache_t    g_write_cache[2]        = { {NULL, 0},
                                                    {NULL, 0} };

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
static void invalidate_cached_write(int index)
{
    write_cache_t* cache;

    // Check bounds.
    if ((unsigned)index >= sizeof_array(g_write_cache))
    {
        return;
    }

    cache = g_write_cache + index;

    cache->size = 0;
    if (cache->buffer != NULL)
    {
        cache->buffer[0] = L'\0';
    }
}

//------------------------------------------------------------------------------
static void dispatch_cached_write(HANDLE output, int index)
{
    write_cache_t* cache;

    // Check bounds.
    if ((unsigned)index >= sizeof_array(g_write_cache))
    {
        return;
    }

    cache = g_write_cache + index;

    // Write the line to the console.
    if (cache->buffer != NULL)
    {
        DWORD j;
        WriteConsoleW(output, cache->buffer, cache->size, &j, NULL);
    }

    invalidate_cached_write(index);
}

//------------------------------------------------------------------------------
BOOL WINAPI hooked_read_console_input(
    HANDLE input,
    INPUT_RECORD* buffer,
    DWORD buffer_size,
    LPDWORD events_read
)
{
    int i;

    i = (g_write_cache_index + 1) & 1;
    dispatch_cached_write(GetStdHandle(STD_OUTPUT_HANDLE), i);

    return ReadConsoleInputA(input, buffer, buffer_size, events_read);
}

//------------------------------------------------------------------------------
BOOL WINAPI hooked_read_console(
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
    int i;
    write_cache_t* write_cache;

    i = (g_write_cache_index + 1) & 1;
    write_cache = g_write_cache + i;

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (buffer_size == 1)
    {
        dispatch_cached_write(GetStdHandle(STD_OUTPUT_HANDLE), i);
        return ReadConsoleW(input, buffer, buffer_size, read_in, control);
    }

    old_seh = SetUnhandledExceptionFilter(exception_filter);

    // Call readline.
    is_eof = call_readline(write_cache->buffer, buffer, buffer_size);
    if (is_eof && get_clink_setting_int("ctrld_exits"))
    {
        wcsncpy(buffer, L"exit", buffer_size);
    }

    invalidate_cached_write(i);

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
BOOL WINAPI hooked_write_console(
    HANDLE output,
    const wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD written,
    void* unused
)
{
    // Writes to the console are double buffered. This stops custom prompts
    // from flickering.

    static int once = 0;
    int copy_size;
    int i;
    write_cache_t* cache;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    // First establish the next buffer to use and allocate it if need be.
    i = g_write_cache_index;
    g_write_cache_index = (i + 1) & 1;

    cache = g_write_cache + i;

    if (cache->buffer == NULL)
    {
        cache->buffer = VirtualAlloc(
            NULL,
            g_write_cache_size + 1,
            MEM_COMMIT,
            PAGE_READWRITE
        );
    }

    // Copy the write request into the buffer.
    copy_size = (g_write_cache_size < buffer_size)
        ? g_write_cache_size
        : buffer_size;

    cache->size = copy_size;
    memcpy(cache->buffer, buffer, copy_size * sizeof(wchar_t));
    cache->buffer[copy_size] = L'\0';

    // Dispatch previous write call.
    dispatch_cached_write(output, g_write_cache_index);

    if (written != NULL)
    {
        *written = buffer_size;
    }

    return TRUE;
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
        static const char home_eq[] = "HOME=";

        strcpy(buffer, home_eq);
        get_config_dir(
            buffer + sizeof(home_eq) - 1,
            sizeof(buffer) - sizeof(home_eq)
        );

        slash = strstr(buffer, "\\clink");
        if (slash)
        {
            *slash = '\0';
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
int is_interactive()
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
static void load_inject_args(DWORD pid)
{
    HANDLE handle;
    char buffer[1024];

    get_inject_arg_file(pid, buffer, sizeof_array(buffer));
    handle = CreateFile(buffer, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle != INVALID_HANDLE_VALUE)
    {
        DWORD read_in;

        ReadFile(handle, &g_inject_args, sizeof(g_inject_args), &read_in, NULL);
        CloseHandle(handle);
    }
}

//------------------------------------------------------------------------------
static void success()
{
    extern const char* g_clink_header;
    extern const char* g_clink_footer;

    if (!g_inject_args.quiet)
    {
        puts(g_clink_header);
        puts(g_clink_footer);
    }
}

//------------------------------------------------------------------------------
static void failed()
{
    char buffer[1024];

    buffer[0] = '\0';
    get_config_dir(buffer, sizeof_array(buffer));

    fprintf(stderr, "Failed to load clink.\nSee log for details (%s).", buffer);
}

//------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID unused)
{
    static int running = 0;
    void* base;

    // We're only interested in when the dll is attached.
    if (reason != DLL_PROCESS_ATTACH)
    {
        if (running && reason == DLL_PROCESS_DETACH)
        {
            save_history();
            shutdown_lua();
            shutdown_clink_settings();
        }

        return TRUE;
    }

    if (!is_interactive())
    {
        return FALSE;
    }

    load_inject_args(GetCurrentProcessId());
    prepare_env_for_inputrc(instance);

    base = validate_parent_process();
    if (base == NULL)
    {
        failed();
        return FALSE;
    }

    if (!set_hook_trap())
    {
        failed();
        return FALSE;
    }

    success();
    running = 1;
    return TRUE;
}

// vim: expandtab
