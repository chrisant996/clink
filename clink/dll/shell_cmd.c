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
int                 get_clink_setting_int(const char*);
void*               push_exception_filter();
void                pop_exception_filter(void* old_filter);
int                 call_readline_w(const wchar_t*, wchar_t*, unsigned);
void                emulate_doskey(wchar_t*, unsigned);
wchar_t*            detect_tagged_prompt_w(const wchar_t*, int);
void                free_prompt(void*);
static int          cmd_validate();
static int          cmd_initialise(void*);
static void         cmd_shutdown();

extern const char   g_prompt_tag_hidden[];
static wchar_t*     g_prompt_w;
shell_t             g_shell_cmd = {
                        cmd_validate,
                        cmd_initialise,
                        cmd_shutdown
                    };

//------------------------------------------------------------------------------
static int is_interactive()
{
    // Check the command line for '/c' and don't load if it's present. There's
    // no point loading clink if cmd.exe is running a command and then exiting.

    wchar_t* args;

    // Check the host is cmd.exe.
    if (GetModuleHandle("cmd.exe") == NULL)
    {
        return 0;
    }

    // Get the command line.
    args = GetCommandLineW();
    if (args == NULL)
    {
        return 0;
    }

    // Cmd.exe's argument parsing is basic, simply searching for '/' characters
    // and checking the following character.
    while (1)
    {
        int i;

        args = wcschr(args, L'/');
        if (args == NULL)
        {
            break;
        }

        i = tolower(*++args);
        switch (i)
        {
        case 'c':
        case 'k':
            return (i == 'k');
        }
    }

    return 1;
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
static void append_crlf(wchar_t* buffer, unsigned max_size)
{
    // Cmd.exe expects a CRLF combo at the end of the string, otherwise it
    // thinks the line is part of a multi-line command.

    size_t len;

    len = max_size - wcslen(buffer);
    wcsncat(buffer, L"\x0d\x0a", len);
    buffer[max_size - 1] = L'\0';
}

//------------------------------------------------------------------------------
static BOOL WINAPI single_char_read(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    PCONSOLE_READCONSOLE_CONTROL control
)
{
    int reply;

    if (reply = check_auto_answer(L""))
    {
        // cmd.exe's PromptUser() method reads a character at a time until
        // it encounters a \n. The way Clink handle's this is a bit 'hacky'.
        static int visit_count = 0;

        ++visit_count;
        if (visit_count >= 2)
        {
            reply = '\n';
            visit_count = 0;
        }

        *buffer = reply;
        *read_in = 1;
        return TRUE;
    }

    // Default behaviour.
    return ReadConsoleW(input, buffer, buffer_size, read_in, control);
}

//------------------------------------------------------------------------------
static BOOL WINAPI read_console(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    PCONSOLE_READCONSOLE_CONTROL control
)
{
    int is_eof;
    void* old_exception_filter;

    // If the file past in isn't a console handle then go the default route.
    if (GetFileType(input) != FILE_TYPE_CHAR)
    {
        return ReadConsoleW(input, buffer, buffer_size, read_in, control);
    }

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (buffer_size == 1)
    {
        return single_char_read(input, buffer, buffer_size, read_in, control);
    }

    // Sometimes cmd.exe wants line input for reasons other than command entry.
    if (g_prompt_w == NULL || *g_prompt_w == L'\0')
    {
        return ReadConsoleW(input, buffer, buffer_size, read_in, control);
    }

    old_exception_filter = push_exception_filter();

    // Call readline.
    is_eof = call_readline_w(g_prompt_w, buffer, buffer_size);
    if (is_eof && get_clink_setting_int("ctrld_exits"))
    {
        wcsncpy(buffer, L"exit", buffer_size);
    }

    emulate_doskey(buffer, buffer_size);
    append_crlf(buffer, buffer_size);

    pop_exception_filter(old_exception_filter);

    *read_in = (unsigned)wcslen(buffer);
    return TRUE;
}

//------------------------------------------------------------------------------
static BOOL WINAPI write_console(
    HANDLE handle,
    const wchar_t* buffer,
    DWORD to_write,
    LPDWORD written,
    LPVOID unused
)
{
    // Clink tags the prompt so that it can be detected when cmd.exe writes it
    // to the console.

    const wchar_t* prompt = detect_tagged_prompt_w(buffer, to_write);
    if (prompt != NULL)
    {
        // Copy the prompt.
        free_prompt(g_prompt_w);
        g_prompt_w = prompt;

        // Convince caller (cmd.exe) that we wrote something to the console.
        if (written != NULL)
        {
            *written = to_write;
        }

        return TRUE;
    }
    else if (g_prompt_w != NULL)
    {
        g_prompt_w[0] = L'\0';
    }

    return WriteConsoleW(handle, buffer, to_write, written, unused);
}

//------------------------------------------------------------------------------
static void tag_prompt()
{
    // Prefixes the 'prompt' environment variable with a known tag so that Clink
    // can identify console writes that are the prompt.

    static const char* name = "prompt";
    static const int buffer_size = 0x10000;

    int tag_size;
    char* buffer;

    buffer = malloc(buffer_size);
    tag_size = strlen(g_prompt_tag_hidden);

    strcpy(buffer, g_prompt_tag_hidden);
    GetEnvironmentVariableA(name, buffer + tag_size, buffer_size - tag_size);
    SetEnvironmentVariableA(name, buffer);

    free(buffer);
}

//------------------------------------------------------------------------------
static BOOL WINAPI set_env_var(
    const wchar_t* name,
    const wchar_t* value
)
{
    BOOL ret = SetEnvironmentVariableW(name, value);

    if (_wcsicmp(name, L"prompt") == 0)
    {
        tag_prompt();
    }

    return ret;
}

//------------------------------------------------------------------------------
const char* get_kernel_dll()
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
        { HOOK_TYPE_JMP,         NULL, dll,  "ReadConsoleW",            read_console },
        { HOOK_TYPE_IAT_BY_NAME, base, NULL, "WriteConsoleW",           write_console },
        { HOOK_TYPE_IAT_BY_NAME, base, NULL, "SetEnvironmentVariableW", set_env_var },
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
static int cmd_initialise(void* base)
{
    const char* dll = get_kernel_dll();
    const char* func_name = "GetCurrentDirectoryW";

    if (!set_hook_trap(dll, func_name, hook_trap))
    {
        return 0;
    }

    tag_prompt();

    // Add an alias to Clink so it can be run from anywhere. Similar to adding
    // it to the path but this way we can add the config path too.
    {
        #define BUF_SIZE 512

        char dll_path[BUF_SIZE];
        char cfg_path[BUF_SIZE];
        char buffer[BUF_SIZE];

        get_dll_dir(dll_path, BUF_SIZE);
        get_config_dir(cfg_path, BUF_SIZE);

        strcpy(buffer, "\"");
        str_cat(buffer, dll_path, BUF_SIZE);
        str_cat(buffer, "/clink_" AS_STR(PLATFORM) ".exe\" --cfgdir \"", BUF_SIZE);
        str_cat(buffer, cfg_path, BUF_SIZE);
        str_cat(buffer, "\" $*", BUF_SIZE);

        AddConsoleAlias("clink", buffer, rl_readline_name);

        #undef BUF_SIZE
    }

    return 1;
}

//------------------------------------------------------------------------------
static void cmd_shutdown()
{
}

// vim: expandtab
