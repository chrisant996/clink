// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_cmd.h"
#include "utils/app_context.h"
#include "utils/hook_setter.h"
#include "utils/seh_scope.h"

#include <core/base.h>
#include <core/log.h>
#include <core/settings.h>
#include <lib/line_editor.h>
#include <lua/lua_script_loader.h>
#include <process/hook.h>
#include <process/vm.h>

#include <Windows.h>

//------------------------------------------------------------------------------
static setting_bool g_ctrld_exits(
    "cmd.ctrld_exits",
    "Pressing Ctrl-D exits session",
    "Ctrl-D exits cmd.exe when used on an empty line.",
    true);

static setting_enum g_autoanswer(
    "cmd.auto_answer",
    "Auto-answer terminate prompt",
    "Automatically answers cmd.exe's 'Terminate batch job (Y/N)?' prompts.\n",
    "off,answer_yes,answer_no",
    0);



//------------------------------------------------------------------------------
static bool get_mui_string(int id, wstr_base& out)
{
    DWORD flags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_IGNORE_INSERTS;
    return !!FormatMessageW(flags, nullptr, id, 0, out.data(), out.size(), nullptr);
}

//------------------------------------------------------------------------------
static int check_auto_answer()
{
    static wstr<72> target_prompt;
    static wstr<16> no_yes;

    // Skip the feature if it's not enabled.
    int setting = g_autoanswer.get();
    if (setting <= 0)
        return 0;

    // Try and find the localised prompt.
    if (target_prompt.empty())
    {
        // cmd.exe's translations are stored in a message table result in
        // the cmd.exe.mui overlay.

        if (!get_mui_string(0x2328, no_yes))
            no_yes = L"ny";

        if (get_mui_string(0x237b, target_prompt))
        {
            // Strip off new line chars.
            for (wchar_t* c = target_prompt.data(); *c; ++c)
                if (*c == '\r' || *c == '\n')
                    *c = '\0';

            LOG("Auto-answer; '%ls' (%ls)", target_prompt.c_str(), no_yes.c_str());
        }
        else
        {
            target_prompt = L"Terminate batch job (Y/N)? ";
            no_yes = L"ny";
            LOG("Using fallback auto-answer prompt.");
        }
    }

    prompt prompt = prompt_utils::extract_from_console();
    if (prompt.get() != nullptr && wcsstr(prompt.get(), target_prompt.c_str()) != 0)
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
void tag_prompt()
{
    // Tag the prompt so we can detect when cmd.exe writes to the terminal.
    wchar_t buffer[256];
    buffer[0] = '\0';
    GetEnvironmentVariableW(L"prompt", buffer, sizeof_array(buffer));

    tagged_prompt prompt;
    prompt.tag(buffer[0] ? buffer : L"$p$g");
    SetEnvironmentVariableW(L"prompt", prompt.get());
}



//------------------------------------------------------------------------------
host_cmd::host_cmd()
: host("cmd.exe")
, m_doskey("cmd.exe")
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
    void* base = GetModuleHandle(nullptr);
    hook_setter hooks;

    // Hook the setting of the 'prompt' environment variable so we can tag
    // it and detect command entry via a write hook.
    tag_prompt();
    hooks.add_iat(base, "SetEnvironmentVariableW",  &host_cmd::set_env_var);
    hooks.add_iat(base, "WriteConsoleW",            &host_cmd::write_console);

    // Set a trap to get a callback when cmd.exe fetches stdin handle.
    auto get_std_handle = [] (unsigned int handle_id) -> void*
    {
        seh_scope seh;

        void* ret = GetStdHandle(handle_id);
        if (handle_id != STD_INPUT_HANDLE)
            return ret;

        void* base = GetModuleHandle(nullptr);
        hook_iat(base, nullptr, "GetStdHandle", funcptr_t(GetStdHandle), 1);

        host_cmd::get()->initialise_system();
        return ret;
    };
    auto* as_stdcall = static_cast<void* (__stdcall *)(unsigned)>(get_std_handle);
    hooks.add_iat(base, "GetStdHandle", as_stdcall);

    return (hooks.commit() == 3);
}

//------------------------------------------------------------------------------
void host_cmd::shutdown()
{
    m_doskey.remove_alias("history");
    m_doskey.remove_alias("clink");
}

//------------------------------------------------------------------------------
void host_cmd::initialise_lua(lua_state& lua)
{
    lua_load_script(lua, app, cmd);
    lua_load_script(lua, app, dir);
    lua_load_script(lua, app, env);
    lua_load_script(lua, app, exec);
    lua_load_script(lua, app, self);
    lua_load_script(lua, app, set);
}

//------------------------------------------------------------------------------
void host_cmd::initialise_editor_desc(line_editor::desc& desc)
{
    desc.quote_pair = "\"";
    desc.command_delims = "&|";
    desc.word_delims = " \t<>=;";
    desc.auto_quote_chars = " %=;&^";
}

//------------------------------------------------------------------------------
bool host_cmd::is_interactive() const
{
    // Check the command line for '/c' and don't load if it's present. There's
    // no point loading clink if cmd.exe is running a command and then exiting.
    // Cmd.exe's argument parsing is basic, simply searching for '/' characters
    // and checking the following character.

    const wchar_t* args = GetCommandLineW();
    while (args != nullptr && (args = wcschr(args, '/')))
    {
        ++args;
        switch (tolower(*args))
        {
        case 'c': return false;
        case 'k': args = nullptr; break;
        }
    }

    // Also check that IO is a character device (i.e. a console).
    HANDLE handles[] = {
        GetStdHandle(STD_INPUT_HANDLE),
        GetStdHandle(STD_OUTPUT_HANDLE),
    };

    for (auto handle : handles)
        if (GetFileType(handle) != FILE_TYPE_CHAR)
            return false;

    return true;
}

//------------------------------------------------------------------------------
void host_cmd::edit_line(const wchar_t* prompt, wchar_t* chars, int max_chars)
{
    // Doskey is implemented on the server side of a ReadConsoleW() call (i.e.
    // in conhost.exe). Commands separated by a "$T" are returned one command
    // at a time through successive calls to ReadConsoleW().
    wstr_base out(chars, max_chars);
    if (m_doskey_alias.next(out))
        return;

    // Convert the prompt to Utf8 and parse backspaces in the string.
    str<128> utf8_prompt(m_prompt.get());

    char* write = utf8_prompt.data();
    char* read = write;
    while (char c = *read++)
        if (c != '\b')
            *write++ = c;
        else if (write > utf8_prompt.c_str())
            --write;
    *write = '\0';

    // Call readline.
    while (1)
    {
        str<4096> out;
        bool ok = host::edit_line(utf8_prompt.c_str(), out);
        if (ok)
        {
            to_utf16(chars, max_chars, out.c_str());
            break;
        }

        if (g_ctrld_exits.get())
        {
            wstr_base(chars, max_chars) = L"exit";
            break;
        }

        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written;
        WriteConsole(handle, L"\n", 1, &written, nullptr);
    }

    m_doskey.resolve(chars, m_doskey_alias);
    m_doskey_alias.next(out);
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::read_console(
    HANDLE input,
    wchar_t* chars,
    DWORD max_chars,
    LPDWORD read_in,
    CONSOLE_READCONSOLE_CONTROL* control)
{
    seh_scope seh;

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
    // Clink tags the prompt so that it can be detected when cmd.exe
    // writes it to the console.

    m_prompt.set(chars, char_count);
    return (m_prompt.get() != nullptr);
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::set_env_var(const wchar_t* name, const wchar_t* value)
{
    seh_scope seh;

    if (value == nullptr || _wcsicmp(name, L"prompt") != 0)
        return SetEnvironmentVariableW(name, value);

    tagged_prompt prompt;
    prompt.tag(value);
    return SetEnvironmentVariableW(name, prompt.get());
}

//------------------------------------------------------------------------------
bool host_cmd::initialise_system()
{
    // Get the base address of module that exports ReadConsoleW.
    void* kernel_module = vm().get_alloc_base(ReadConsoleW);
    if (kernel_module == nullptr)
        return false;

    // Add an alias to Clink so it can be run from anywhere. Similar to adding
    // it to the path but this way we can add the config path too.
    {
        str<280> dll_path;
        app_context::get()->get_binaries_dir(dll_path);

        str<560> buffer;
        buffer << "\"" << dll_path;
        buffer << "/" CLINK_EXE "\" $*";
        m_doskey.add_alias("clink", buffer.c_str());
    
        // Add an alias to operate on the command history.
        buffer.clear();
        buffer << "\"" << dll_path;
        buffer << "/" CLINK_EXE "\" history $*";
        m_doskey.add_alias("history", buffer.c_str());
    }
    
    // Tag the prompt again just incase it got unset by by something like
    // setlocal/endlocal in a boot Batch script.
    tag_prompt();

    hook_setter hooks;
    hooks.add_jmp(kernel_module, "ReadConsoleW",    &host_cmd::read_console);
    return (hooks.commit() == 1);
}
