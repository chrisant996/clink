// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_cmd.h"
#include "utils/app_context.h"
#include "utils/hook_setter.h"
#include "utils/seh_scope.h"
#include "utils/reset_stdio.h"

#include <core/base.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/os.h>
#include <core/path.h>
#include <lib/doskey.h>
#include <lib/line_buffer.h>
#include <lib/line_editor.h>
#include <lib/terminal_helpers.h>
#include <lua/lua_script_loader.h>

#include <Windows.h>

#if 0
#define ADMINISTRATOR_TITLE_PREFIX 10056
#endif

//------------------------------------------------------------------------------
using func_SetEnvironmentVariableW_t = BOOL (WINAPI*)(LPCWSTR lpName, LPCWSTR lpValue);
using func_WriteConsoleW_t = BOOL (WINAPI*)(HANDLE hConsoleOutput, CONST VOID* lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved);
using func_ReadConsoleW_t = BOOL (WINAPI*)(HANDLE hConsoleInput, VOID* lpBuffer, DWORD nNumberOfCharsToRead, LPDWORD lpNumberOfCharsRead, PCONSOLE_READCONSOLE_CONTROL pInputControl);
using func_GetEnvironmentVariableW_t = DWORD (WINAPI*)(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
func_SetEnvironmentVariableW_t __Real_SetEnvironmentVariableW = SetEnvironmentVariableW;
func_WriteConsoleW_t __Real_WriteConsoleW = WriteConsoleW;
func_ReadConsoleW_t __Real_ReadConsoleW = ReadConsoleW;
func_GetEnvironmentVariableW_t __Real_GetEnvironmentVariableW = GetEnvironmentVariableW;
static const char s_kernel_module[] = "kernel32.dll";

//------------------------------------------------------------------------------
extern bool is_force_reload_scripts();
extern "C" void reset_wcwidths();

//------------------------------------------------------------------------------
extern setting_bool g_ctrld_exits;

static setting_enum g_autoanswer(
    "cmd.auto_answer",
    "Auto-answer terminate prompt",
    "Automatically answers cmd.exe's 'Terminate batch job (Y/N)?' prompts.\n",
    "off,answer_yes,answer_no",
    0);

#if 0
static setting_str g_admin_title_prefix(
    "cmd.admin_title_prefix",
    "Replaces the console title prefix when elevated",
    "This replaces the console title prefix when cmd.exe is elevated.",
    "");
#endif



//------------------------------------------------------------------------------
static bool get_mui_string(int id, wstr_base& out)
{
    DWORD flags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_IGNORE_INSERTS;
    return !!FormatMessageW(flags, nullptr, id, 0, out.data(), out.size(), nullptr);
}

//------------------------------------------------------------------------------
static char s_answered = 0;
static int check_auto_answer()
{
    static wstr<72> target_prompt;
    static wstr<16> no_yes;

    // Don't allow infinite loop due to unaccepted autoanswer.
    if (s_answered >= 2)
        return 0;

    // Skip the feature if it's not enabled.
    int setting = g_autoanswer.get();
    if (setting <= 0)
        return 0;

    // Try and find the localised prompt.
    if (target_prompt.empty())
    {
        // cmd.exe's translations are stored in a message table result in
        // the cmd.exe.mui overlay.
        bool fallback = (!get_mui_string(0x2328, no_yes) ||
                         !get_mui_string(0x237b, target_prompt));

        // Strip off new line chars.
        for (wchar_t* c = target_prompt.data(); *c; ++c)
            if (*c == '\r' || *c == '\n')
                *c = '\0';

        // Log what was retrieved.
        str<72> tmp1(target_prompt.c_str());
        str<16> tmp2(no_yes.c_str());
        LOG("Auto-answer; '%s' (%s)", tmp1.c_str(), tmp2.c_str());

        // Fall back to English.
        if (fallback || target_prompt.empty() || no_yes.length() != 2)
        {
            target_prompt = L"Terminate batch job (Y/N)? ";
            no_yes = L"ny";
            LOG("Using fallback auto-answer prompt.");
        }
    }

    prompt prompt = prompt_utils::extract_from_console();
    if (prompt.get() != nullptr && wcsstr(prompt.get(), target_prompt.c_str()) != 0)
    {
        // cmd.exe's PromptUser() method reads a character at a time until
        // it encounters a \n. The way Clink handle's this is a bit 'wacky'.
        ++s_answered;
        if (s_answered >= 2)
            return '\n';

        return (setting == 1) ? no_yes[1] : no_yes[0];
    }

    return 0;
}

//------------------------------------------------------------------------------
static bool s_more_continuation = false;
static void check_more_continuation(const wchar_t* prompt, DWORD len)
{
    static wstr<72> more_prompt;

    // Try and find the localised prompt.
    if (more_prompt.empty())
    {
        // cmd.exe's translations are stored in a message table result in
        // the cmd.exe.mui overlay.
        bool fallback = (!get_mui_string(0x2532, more_prompt));

        // Strip off new line chars.
        for (wchar_t* c = more_prompt.data(); *c; ++c)
            if (*c == '\r' || *c == '\n')
                *c = '\0';

        // Fall back to English.
        if (fallback || more_prompt.empty())
            more_prompt = L"More? ";
    }

    s_more_continuation = false;
    for (const wchar_t* more = more_prompt.c_str(); true;)
    {
        if (!len && !*more)
        {
            s_more_continuation = true;
            break;
        }
        if (!len || !*more || *prompt != *more)
            break;
        len--;
        more++;
        prompt++;
    }
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
        *buffer = reply;
        *read_in = 1;

        // Echo Clink's response.
        DWORD dummy;
        __Real_WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, 1, &dummy, nullptr);
        return TRUE;
    }

    // Default behaviour.
    return __Real_ReadConsoleW(input, buffer, buffer_size, read_in, control);
}

//------------------------------------------------------------------------------
void tag_prompt()
{
    // Tag the prompt so we can detect when cmd.exe writes to the terminal.
    wchar_t buffer[256];
    buffer[0] = '\0';
    __Real_GetEnvironmentVariableW(L"prompt", buffer, sizeof_array(buffer));

    // This MUST NOT call the hooked version, or infinite recursion will happen.
    tagged_prompt prompt;
    prompt.tag(buffer[0] ? buffer : L"$p$g");
    __Real_SetEnvironmentVariableW(L"prompt", prompt.get());
}

//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    __Real_WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}



//------------------------------------------------------------------------------
void host_cmd_enqueue_lines(std::list<str_moveable>& lines)
{
    host_cmd::get()->enqueue_lines(lines);
}



//------------------------------------------------------------------------------
host_cmd::host_cmd()
: host("cmd.exe")
, m_doskey(os::get_shellname())
{
}

//------------------------------------------------------------------------------
int host_cmd::validate()
{
    if (!is_interactive())
    {
        LOG("Host is not interactive; cancelling inject.");
        return -1;
    }

    return true;
}

//------------------------------------------------------------------------------
bool host_cmd::initialise()
{
    hook_setter hooks;

    // Hook the setting of the 'prompt' environment variable so we can tag
    // it and detect command entry via a write hook.
    tag_prompt();
    if (!hooks.attach(s_kernel_module, "SetEnvironmentVariableW", &host_cmd::set_env_var, &__Real_SetEnvironmentVariableW))
        return false;
    if (!hooks.attach(s_kernel_module, "WriteConsoleW", &host_cmd::write_console, &__Real_WriteConsoleW))
        return false;

    // Set a trap to get a callback when cmd.exe fetches PROMPT environment
    // variable.  GetEnvironmentVariableW is always called before displaying the
    // prompt string, so it's a reliable spot to hook regardless how injection
    // is initiated (AutoRun, command line, etc).
    if (!hooks.attach(s_kernel_module, "GetEnvironmentVariableW", &host_cmd::get_env_var, &__Real_GetEnvironmentVariableW))
        return false;

    return hooks.commit();
}

//------------------------------------------------------------------------------
void host_cmd::shutdown()
{
    str<> clink;
    str<> history;
    make_aliases(clink, history);

    // Only remove the aliases if they match what this instance would add.
    str<> tmp;
    if (os::get_alias("history", tmp) && tmp.equals(history.c_str()))
        m_doskey.remove_alias("history");
    if (os::get_alias("clink", tmp) && tmp.equals(clink.c_str()))
        m_doskey.remove_alias("clink");
}

//------------------------------------------------------------------------------
void host_cmd::initialise_lua(lua_state& lua)
{
    lua_load_script(lua, app, cmd);
    lua_load_script(lua, app, commands);
    lua_load_script(lua, app, dir);
    lua_load_script(lua, app, env);
    lua_load_script(lua, app, exec);
    lua_load_script(lua, app, self);
    lua_load_script(lua, app, set);
}

//------------------------------------------------------------------------------
void host_cmd::initialise_editor_desc(line_editor::desc& desc)
{
    desc.reset_quote_pair();
    desc.command_tokeniser = &m_command_tokeniser;
    desc.word_tokeniser = &m_word_tokeniser;
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
void host_cmd::make_aliases(str_base& clink, str_base& history)
{
    str<280> dll_path;
    app_context::get()->get_binaries_dir(dll_path);

    // Alias to invoke clink.
    clink.clear();
    clink << "\"" << dll_path << "\\" CLINK_EXE "\" $*";

    // Alias to operate on the command history.
    history.clear();
    history << "\"" << dll_path << "\\" CLINK_EXE "\" " << "history $*";
}

//------------------------------------------------------------------------------
void host_cmd::add_aliases(bool force)
{
    str<> clink;
    str<> history;
    bool need_clink = force || !os::get_alias("clink", clink);
    bool need_history = force || !os::get_alias("history", history);
    if (need_clink || need_history)
    {
        make_aliases(clink, history);
        if (need_clink)
            m_doskey.add_alias("clink", clink.c_str());
        if (need_history)
            m_doskey.add_alias("history", history.c_str());
    }
}

//------------------------------------------------------------------------------
void host_cmd::edit_line(wchar_t* chars, int max_chars)
{
    // Exiting a nested CMD will remove the aliases, so re-add them if missing.
    // But don't overwrite them if they already exist: let the user override
    // them if so desired.
    add_aliases(false/*force*/);

    bool resolved = false;
    wstr_base wout(chars, max_chars);

    // Convert the prompt to Utf8 and parse backspaces in the string.
    // BUGBUG: This mishandles multi-byte characters!
    // BUGBUG: This mishandles surrogate pairs and combining characters!
    // BUGBUG: This mishandles backspaces inside envvars expanded by OSC codes!
    str_moveable utf8_prompt(m_prompt.get());
    str_moveable utf8_rprompt;
    prompt_utils::get_rprompt(utf8_rprompt);

    char* write = utf8_prompt.data();
    char* read = write;
    while (char c = *read++)
        if (c != '\b')
            *write++ = c;
        else if (write > utf8_prompt.c_str())
            --write;
    *write = '\0';

    // Call readline.
    {
        str<1024> out;
        while (1)
        {
            // WARNING:  Settings are not valid here; they are not loaded until
            // inside of host::edit_line().
            out.clear();
            if (host::edit_line(utf8_prompt.c_str(), utf8_rprompt.c_str(), out))
            {
                to_utf16(chars, max_chars, out.c_str());
                break;
            }

            if (is_force_reload_scripts())
            {
            }
            else if (g_ctrld_exits.get())
            {
                wstr_base(chars, max_chars) = L"exit";
                break;
            }

            write_line_feed();
        }
    }

    // Reset the captured prompt so that `set /p VAR=""` works properly.  It's a
    // degenerate case where ReadConsoleW is called again without an intervening
    // call to WriteConsoleW.  It's incorrect to rely on WriteConsoleW for
    // clearing the captured prompt.
    m_prompt.clear();
}

//------------------------------------------------------------------------------
BOOL WINAPI host_cmd::read_console(
    HANDLE input,
    void* _chars,
    DWORD max_chars,
    LPDWORD read_in,
    CONSOLE_READCONSOLE_CONTROL* control)
{
    seh_scope seh;
    wchar_t* const chars = reinterpret_cast<wchar_t*>(_chars);

    const bool more_continuation = s_more_continuation;
    s_more_continuation = false;

    // if the input handle isn't a console handle then go the default route.
    if (GetFileType(input) != FILE_TYPE_CHAR)
        return __Real_ReadConsoleW(input, chars, max_chars, read_in, control);

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (max_chars == 1)
        return single_char_read(input, chars, max_chars, read_in, control);

    s_answered = 0;

    wstr_base line(chars, max_chars);
    if (more_continuation && host_cmd::get()->dequeue_line(line))
    {
        // Respond with the dequeued line.
        DWORD written;
        while (line.length() && line.c_str()[line.length() - 1] == '\n')
            line.truncate(line.length() - 1);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        __Real_WriteConsoleW(h, line.c_str(), line.length(), &written, nullptr);
        __Real_WriteConsoleW(h, L"\r\n", 2, &written, nullptr);
    }
    else
    {
        // Cmd.exe can want line input for reasons other than command entry.
        const wchar_t* prompt = host_cmd::get()->m_prompt.get();
        if (prompt == nullptr || *prompt == L'\0')
            return __Real_ReadConsoleW(input, chars, max_chars, read_in, control);

        // Redirection in CMD can change CMD's STD handles, causing Clink's C
        // runtime to have stale handles.  Check and reset them if necessary.
        reset_stdio_handles();

        // Surround the entire edit_line() scope, otherwise any time Clink
        // doesn't read input fast enough the OS can handle processed input
        // while it's enabled between ReadConsoleInputW calls.
        {
            console_config cc(input);
            reset_wcwidths();
            host_cmd::get()->edit_line(chars, max_chars);
        }
    }

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
    const void* _chars,
    DWORD to_write,
    LPDWORD written,
    LPVOID unused)
{
    seh_scope seh;
    const wchar_t* const chars = reinterpret_cast<const wchar_t*>(_chars);

    // If the output handle is a console handle then handle prompts.
    if (GetFileType(output) == FILE_TYPE_CHAR)
    {
        // Check if the output looks like the More? continuation prompt.
        check_more_continuation(chars, to_write);

        // If it's the tagged prompt then convince caller (cmd.exe) that we
        // wrote something to the console, and return without writing anything.
        // The ReadConsoleW hook will handle displaying the prompt and accepting
        // an input line.
        if (host_cmd::get()->capture_prompt(chars, to_write))
        {
            // Convince caller (cmd.exe) that we wrote something to the console.
            if (written != nullptr)
                *written = to_write;

            return TRUE;
        }
    }

    return __Real_WriteConsoleW(output, chars, to_write, written, unused);
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
        return __Real_SetEnvironmentVariableW(name, value);

    tagged_prompt prompt;
    prompt.tag(value);
    return __Real_SetEnvironmentVariableW(name, prompt.get());
}

//------------------------------------------------------------------------------
static bool s_initialised_system = false;
DWORD WINAPI host_cmd::get_env_var(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize)
{
    seh_scope seh;

    DWORD ret = __Real_GetEnvironmentVariableW(lpName, lpBuffer, nSize);

    if (!s_initialised_system) // In case detaching fails.
    {
        s_initialised_system = true;

        hook_setter unhook;
        unhook.detach(&__Real_GetEnvironmentVariableW, get_env_var);
        unhook.commit();

        host_cmd::get()->initialise_system();
    }
    return ret;
}

//------------------------------------------------------------------------------
#if 0
DWORD WINAPI host_cmd::format_message(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, wchar_t* buffer, DWORD size, va_list* arguments)
{
    if ((flags & FORMAT_MESSAGE_FROM_HMODULE) &&
        source == nullptr &&
        (messageId & 0x0fffffff) == ADMINISTRATOR_TITLE_PREFIX)
    {
        wstr<> prefix(g_admin_title_prefix.get());
        if (prefix.length())
        {
            if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
            {
                size = prefix.length() + 1;
                wchar_t* alloced = static_cast<wchar_t*>(LocalAlloc(LMEM_FIXED, size));
                if (!alloced)
                    return 0;
                *((LPWSTR*)buffer) = alloced;
                buffer = alloced;
            }

            wstr_base out(buffer, size);
            out.copy(prefix.c_str());
            return out.length();
        }
    }

    return FormatMessageW(flags, source, messageId, languageId, buffer, size, arguments);
}
#endif

//------------------------------------------------------------------------------
bool host_cmd::initialise_system()
{
    {
        // ReadConsoleW is required.
        {
            hook_setter hooks;
            hooks.attach(s_kernel_module, "ReadConsoleW", &host_cmd::read_console, &__Real_ReadConsoleW);
            if (!hooks.commit())
                return false;
        }

#if 0
        // Try to hook FormatMessageW in order to replace the "Administrator: "
        // prefix, but ignore failure since it's just a minor convenience.
        {
            hook_setter hooks;
            hooks.add(s_kernel_module, "FormatMessageW", &host_cmd::format_message, &__imp_FormatMessageW);
            hooks.commit();
        }
#endif
    }

    // Add an alias to Clink so it can be run from anywhere. Similar to adding
    // it to the path but this way we can add other arguments if needed.
    add_aliases(true/*force*/);

    // Tag the prompt again just incase it got unset by something like
    // setlocal/endlocal in a boot Batch script.
    tag_prompt();

    return true;
}
