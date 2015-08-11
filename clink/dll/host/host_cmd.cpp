// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_cmd.h"
#include "hook_setter.h"
#include "seh_scope.h"
#include "paths.h"
#include "process/vm.h"

#include <core/base.h>
#include <core/log.h>
#include <line_editor.h>
#include <terminal.h>

#include <Windows.h>

//------------------------------------------------------------------------------
int                             get_clink_setting_int(const char*);
int                             begin_doskey(wchar_t*, unsigned);
int                             continue_doskey(wchar_t*, unsigned);



//------------------------------------------------------------------------------
static wchar_t* get_mui_string(int id)
{
    DWORD flags, ok;
    wchar_t* ret;

    flags = FORMAT_MESSAGE_ALLOCATE_BUFFER;
    flags |= FORMAT_MESSAGE_FROM_HMODULE;
    flags |= FORMAT_MESSAGE_IGNORE_INSERTS;
    ok = FormatMessageW(flags, nullptr, id, 0, (wchar_t*)(&ret), 0, nullptr);

    return ok ? ret : nullptr;
}

//------------------------------------------------------------------------------
static int check_auto_answer()
{
    static wchar_t* prompt_to_answer = (wchar_t*)1;
    static wchar_t* no_yes;
    wchar_t* c;
    int setting;

    // Skip the feature if it's not enabled.
    setting = get_clink_setting_int("terminate_autoanswer");
    if (setting <= 0)
        return 0;

    // Try and find the localised prompt.
    if (prompt_to_answer == (wchar_t*)1)
    {
        // cmd.exe's translations are stored in a message table result in
        // the cmd.exe.mui overlay.

        prompt_to_answer = get_mui_string(0x237b);
        no_yes = get_mui_string(0x2328);

        if (prompt_to_answer != nullptr)
        {
            no_yes = no_yes ? no_yes : L"ny";

            // Strip off new line chars.
            c = prompt_to_answer;
            while (*c)
            {
                if (*c == '\r' || *c == '\n')
                    *c = '\0';

                ++c;
            }

            LOG("Auto-answer prompt = '%ls' (%ls)", prompt_to_answer, no_yes);
        }
        else
        {
            prompt_to_answer = L"Terminate batch job (Y/N)? ";
            no_yes = L"ny";
            LOG("Using fallback auto-answer prompt.");
        }
    }

    prompt prompt = prompt_utils::extract_from_console();
    if (prompt.get() != nullptr && wcsstr(prompt.get(), prompt_to_answer) != 0)
        return (setting == 1) ? no_yes[1] : no_yes[0];

    return 0;
}

//------------------------------------------------------------------------------
static BOOL WINAPI single_char_read(
    HANDLE input,
    wchar_t* buffer,
    DWORD buffer_size,
    LPDWORD read_in,
    CONSOLE_READCONSOLE_CONTROL* control)
{
    int reply;

    if (reply = check_auto_answer())
    {
        // cmd.exe's PromptUser() method reads a character at a time until
        // it encounters a \n. The way Clink handle's this is a bit 'wacky'.
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
host_cmd::host_cmd(line_editor* editor)
: host(editor)
{
}

//------------------------------------------------------------------------------
host_cmd::~host_cmd()
{
}

//------------------------------------------------------------------------------
bool host_cmd::validate()
{
    if (!is_interactive())
        return false;

    return true;
}

//------------------------------------------------------------------------------
bool host_cmd::initialise()
{
    // Find the correct module that exports ReadConsoleW by finding the base
    // address of the virtual memory block where the function is.
    void* kernel_module = vm_region(ReadConsoleW).get_parent().get_base();
    if (kernel_module == nullptr)
        return false;

    // Set a trap to get a callback when cmd.exe fetches a environment variable.
    hook_setter hook;
    hook.add_trap(kernel_module, "GetEnvironmentVariableW", hook_trap);
    if (hook.commit() == 0)
        return false;

    // Add an alias to Clink so it can be run from anywhere. Similar to adding
    // it to the path but this way we can add the config path too.
    str<256> path;
    GetModuleFileName(nullptr, path.data(), path.size());
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        str<256> dll_path;
        get_dll_dir(dll_path);

        str<256> cfg_path;
        get_config_dir(cfg_path);

        str<512> buffer;
        buffer << "\"" << dll_path;
        buffer << "/clink_" AS_STR(PLATFORM) ".exe\" --cfgdir \"";
        buffer << cfg_path << "\" $*";

        const char* slash = strrchr(path.c_str(), '\\');
        const char* host_name = (slash != nullptr) ? slash + 1 : path.c_str();
        AddConsoleAlias("clink", buffer.data(), (char*)host_name);
    }

    return true;
}

//------------------------------------------------------------------------------
void host_cmd::shutdown()
{
}

//------------------------------------------------------------------------------
bool host_cmd::is_interactive() const
{
    // Check the command line for '/c' and don't load if it's present. There's
    // no point loading clink if cmd.exe is running a command and then exiting.

    // Check the host is cmd.exe.
    if (GetModuleHandle("cmd.exe") == nullptr)
        return false;

    // Get the command line.
    wchar_t* args = GetCommandLineW();
    if (args == nullptr)
        return false;

    // Cmd.exe's argument parsing is basic, simply searching for '/' characters
    // and checking the following character.
    while (1)
    {
        args = wcschr(args, L'/');
        if (args == nullptr)
            break;

        ++args;
        int i = tolower(*args);
        switch (i)
        {
        case 'c':
        case 'k':
            return (i == 'k');
        }
    }

    return true;
}

//------------------------------------------------------------------------------
void host_cmd::edit_line(const wchar_t* prompt, wchar_t* chars, int max_chars)
{
    // Doskey is implemented on the server side of a ReadConsoleW() call (i.e.
    // in conhost.exe). Commands separated by a "$T" are returned one command
    // at a time through successive calls to ReadConsoleW().
    if (continue_doskey(chars, max_chars))
        return;

    // Call readline.
    while (1)
    {
        line_editor* editor = get_line_editor();
        const wchar_t* prompt = m_prompt.get();
        int is_eof = editor->edit_line(prompt, chars, max_chars);
        if (!is_eof)
            break;

        if (get_clink_setting_int("ctrld_exits"))
        {
            wcsncpy(chars, L"exit", max_chars);
            break;
        }

        terminal* term = editor->get_terminal();
        term->write(L"\r\n", 2);
        term->flush();
    }

    begin_doskey(chars, max_chars);
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::read_console(
    HANDLE input,
    wchar_t* chars,
    DWORD max_chars,
    LPDWORD read_in,
    CONSOLE_READCONSOLE_CONTROL* control)
{
    struct console_mode_scope
    {
        console_mode_scope(HANDLE h)
        : handle(h)
        {
            GetConsoleMode(handle, &mode);
        }

        ~console_mode_scope()
        {
            SetConsoleMode(handle, mode);
        }

        HANDLE  m_handle;
        DWORD   m_mode;
    };

    console_mode_scope stdout_mode_scope(GetStdHandle(STD_OUTPUT_HANDLE));
    console_mode_scope stdin_mode_scope(GetStdHandle(STD_INPUT_HANDLE));
    seh_scope seh;

    // If the file past in isn't a console handle then go the default route.
    if (GetFileType(input) != FILE_TYPE_CHAR)
        return ReadConsoleW(input, chars, max_chars, read_in, control);

    // if the input handle isn't a console handle then go the default route.
    if (GetFileType(input) != FILE_TYPE_CHAR)
        return ReadConsoleW(input, chars, max_chars, read_in, control);

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (max_chars == 1)
        return single_char_read(input, chars, max_chars, read_in, control);

    // Sometimes cmd.exe wants line input for reasons other than command entry.
    const wchar_t* prompt = host_cmd::get()->m_prompt.get();
    if (prompt == nullptr || *prompt == L'\0')
        return ReadConsoleW(input, chars, max_chars, read_in, control);

    host_cmd::get()->edit_line(prompt, chars, max_chars);

    // ReadConsole will also include the CRLF of the line that was input.
    size_t len = max_chars - wcslen(chars);
    wcsncat(chars, L"\x0d\x0a", len);
    chars[max_chars - 1] = L'\0';

    if (read_in != nullptr)
        *read_in = (unsigned)wcslen(chars);

    return TRUE;
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::write_console(
    HANDLE output,
    const wchar_t* chars,
    DWORD to_write,
    LPDWORD written,
    LPVOID unused)
{
    seh_scope seh;

    // if the output handle isn't a console handle then go the default route.
    if (GetFileType(output) != FILE_TYPE_CHAR)
        return WriteConsoleW(output, chars, to_write, written, unused);

    if (host_cmd::get()->capture_prompt(chars, to_write))
    {
        // Convince caller (cmd.exe) that we wrote something to the console.
        if (written != nullptr)
            *written = to_write;

        return TRUE;
    }

    return WriteConsoleW(output, chars, to_write, written, unused);
}

//------------------------------------------------------------------------------
bool host_cmd::capture_prompt(const wchar_t* chars, int char_count)
{
    // Clink tags the prompt so that it can be detected when cmd.exe writes it
    // to the console.

    m_prompt.set(chars, char_count);
    return (m_prompt.get() != nullptr);
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::set_env_var(const wchar_t* name, const wchar_t* value)
{
    seh_scope seh;

    if (_wcsicmp(name, L"prompt") != 0)
        return SetEnvironmentVariableW(name, value);

    tagged_prompt prompt;
    prompt.set(value);
    return SetEnvironmentVariableW(name, prompt.get());
}

//------------------------------------------------------------------------------
bool host_cmd::hook_trap()
{
    // Tag the prompt so we can detect when cmd.exe writes to the terminal.
    wchar_t buffer[256];
    buffer[0] = '\0';
    GetEnvironmentVariableW(L"prompt", buffer, sizeof_array(buffer));

    tagged_prompt prompt;
    prompt.tag(buffer[0] ? buffer : L"$p$g");
    SetEnvironmentVariableW(L"prompt", prompt.get());

    // Get the base address of module that exports ReadConsoleW.
    void* kernel_module = vm_region(ReadConsoleW).get_parent().get_base();
    if (kernel_module == nullptr)
        return false;

    void* base = GetModuleHandle(nullptr);
    hook_setter hooks;
    hooks.add_jmp(kernel_module, "ReadConsoleW",            &host_cmd::read_console);
    hooks.add_iat(base,          "WriteConsoleW",           &host_cmd::write_console);
    hooks.add_iat(base,          "SetEnvironmentVariableW", &host_cmd::set_env_var);
    return (hooks.commit() == 3);
}
