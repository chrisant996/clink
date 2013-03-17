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
#include "inject_args.h"
#include "shared/util.h"
#include "shared/shared_mem.h"

//------------------------------------------------------------------------------
typedef struct
{
    wchar_t*            buffer;
    int                 size;
} write_cache_t;

//------------------------------------------------------------------------------
int                     set_hook_trap();
void                    save_history();
void                    shutdown_lua();
void                    clear_to_eol();
void                    emulate_doskey(wchar_t*, unsigned);
int                     call_readline(const wchar_t*, wchar_t*, unsigned);
void                    shutdown_clink_settings();
int                     get_clink_setting_int(const char*);

inject_args_t           g_inject_args;
static int              g_write_cache_index     = 0;
static const int        g_write_cache_size      = 0xffff;      // 0x10000 - 1 !!
static write_cache_t    g_write_cache[2]        = { {NULL, 0},
                                                    {NULL, 0} };

//------------------------------------------------------------------------------
static LONG WINAPI exception_filter(EXCEPTION_POINTERS* info)
{
#if defined(_MSC_VER) && defined(CLINK_USE_SEH)
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
static int check_auto_answer(const wchar_t* prompt)
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
            flags, NULL, 0x237b, 0,
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
static BOOL WINAPI handle_single_byte_read(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    PCONSOLE_READCONSOLE_CONTROL control
)
{
    int i;
    int reply;
    write_cache_t* write_cache;

    i = (g_write_cache_index + 1) & 1;
    write_cache = g_write_cache + i;

    if (reply = check_auto_answer(write_cache->buffer))
    {
        // cmd.exe's PromptUser() method reads a character at a time until
        // it encounters a \n. The way Clink handle's this is a bit 'hacky'.
        static int visit_count = 0;

        ++visit_count;
        if (visit_count >= 2)
        {
            invalidate_cached_write(i);

            reply = '\n';
            visit_count = 0;
        }

        *buffer = reply;
        *read_in = 1;
        return TRUE;
    }

    // Default behaviour.
    dispatch_cached_write(GetStdHandle(STD_OUTPUT_HANDLE), i);
    return ReadConsoleW(input, buffer, buffer_size, read_in, control);
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

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (buffer_size == 1)
    {
        return handle_single_byte_read(
            input,
            buffer,
            buffer_size,
            read_in,
            control
        );
    }

    // Get index to last cached write. This is our prompt.
    i = (g_write_cache_index + 1) & 1;
    write_cache = g_write_cache + i;

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
void prepare_env_for_inputrc()
{
    // Give readline a chance to find the inputrc by modifying the
    // environment slightly.

    char buffer[1024];

    // HOME is where Readline will expand ~ to.
    {
        static const char home_eq[] = "HOME=";
        int size = sizeof_array(home_eq);

        strcpy(buffer, home_eq);
        get_config_dir(buffer + size - 1, sizeof_array(buffer) - size);

        putenv(buffer);
    }

    // INPUTRC is the path where looks for it's configuration file.
    {
        static const char inputrc_eq[] = "INPUTRC=";
        int size = sizeof_array(inputrc_eq);

        strcpy(buffer, inputrc_eq);
        get_dll_dir(buffer + size - 1, sizeof_array(buffer) - size);
        str_cat(buffer, "/clink_inputrc_base", sizeof_array(buffer));

        putenv(buffer);
    }
}

//------------------------------------------------------------------------------
static void failed()
{
    char buffer[1024];

    buffer[0] = '\0';
    get_config_dir(buffer, sizeof_array(buffer));

    fprintf(stderr, "Failed to load clink.\nSee log for details (%s).\n", buffer);
}

//------------------------------------------------------------------------------
static void* validate_parent_process()
{
    void* base;
    const char* name;
    char buffer[MAX_PATH];

    // Blacklist TCC which uses cmd.exe's autorun.
    if (GetModuleFileName(NULL, buffer, sizeof_array(buffer)))
    {
        const char* slash = strrchr(buffer, '\\');
        slash = slash ? slash + 1 : buffer;

        if (strnicmp(slash, "tcc", 3) == 0)
        {
            LOG_INFO("Detected unsupported TCC.");
            return NULL;
        }
    }

    name = g_inject_args.no_host_check ? NULL : "cmd.exe";

    base = (void*)GetModuleHandle(name);
    if (base == NULL)
    {
        LOG_INFO("Failed to find base address for 'cmd.exe'.");
        failed();
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
static void get_inject_args(DWORD pid)
{
    shared_mem_t* shared_mem;
    shared_mem = open_shared_mem(1, "clink", pid);
    memcpy(&g_inject_args, shared_mem->ptr, sizeof(g_inject_args));
    close_shared_mem(shared_mem);
}

//------------------------------------------------------------------------------
static void success()
{
    extern const char* g_clink_header;
    extern const char* g_clink_footer;

    if (!g_inject_args.quiet)
    {
        puts(g_clink_header);
        puts("  ** Press Alt-H to show key bindings. **\n");
        puts(g_clink_footer);
    }
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

    get_inject_args(GetCurrentProcessId());
    if (g_inject_args.profile_path[0] != '\0')
    {
        set_config_dir_override(g_inject_args.profile_path);
    }

    prepare_env_for_inputrc(instance);

    base = validate_parent_process();
    if (base == NULL)
    {
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
