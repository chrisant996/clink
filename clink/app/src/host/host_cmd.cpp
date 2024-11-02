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
#include <core/str_compare.h>
#include <core/str_unordered_set.h>
#include <core/str_transform.h>
#include <core/settings.h>
#include <core/os.h>
#include <core/path.h>
#include <core/linear_allocator.h>
#include <core/debugheap.h>
#include <core/callstack.h>
#include <lib/doskey.h>
#include <lib/line_buffer.h>
#include <lib/line_editor.h>
#include <lib/clink_ctrlevent.h>
#include <lib/wakeup_chars.h>
#include <lib/rl_integration.h>
#include <lua/lua_script_loader.h>
#include <terminal/wcwidth.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>

#define ADMINISTRATOR_TITLE_PREFIX 0x40002748

//------------------------------------------------------------------------------
using func_SetEnvironmentVariableW_t = BOOL (WINAPI*)(LPCWSTR lpName, LPCWSTR lpValue);
using func_SetEnvironmentStringsW = BOOL (WINAPI*)(LPWSTR NewEnvironment);
using func_WriteConsoleW_t = BOOL (WINAPI*)(HANDLE hConsoleOutput, CONST VOID* lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved);
using func_WriteFile_t = BOOL (WINAPI*)(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
using func_ReadConsoleW_t = BOOL (WINAPI*)(HANDLE hConsoleInput, VOID* lpBuffer, DWORD nNumberOfCharsToRead, LPDWORD lpNumberOfCharsRead, __CONSOLE_READCONSOLE_CONTROL* pInputControl);
using func_GetEnvironmentVariableW_t = DWORD (WINAPI*)(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
using func_SetConsoleTitleW_t = BOOL (WINAPI*)(LPCWSTR lpConsoleTitle);
static func_SetEnvironmentVariableW_t __Real_SetEnvironmentVariableW = SetEnvironmentVariableW;
static func_SetEnvironmentStringsW __Real_SetEnvironmentStringsW = SetEnvironmentStringsW;
static func_WriteConsoleW_t __Real_WriteConsoleW = WriteConsoleW;
static func_WriteConsoleW_t __Detoured_WriteConsoleW = WriteConsoleW; // For debug.log_terminal setting.
static func_WriteFile_t __Detoured_WriteFile = WriteFile; // For debug.log_terminal setting.
static func_ReadConsoleW_t __Real_ReadConsoleW = ReadConsoleW;
static func_GetEnvironmentVariableW_t __Real_GetEnvironmentVariableW = GetEnvironmentVariableW;
static func_SetConsoleTitleW_t __Real_SetConsoleTitleW = SetConsoleTitleW;
static bool s_detoured_write_console = false;
static int32 s_in_read_console = 0;

//------------------------------------------------------------------------------
extern printer* g_printer;

//------------------------------------------------------------------------------
extern setting_bool g_ctrld_exits;
extern setting_bool g_debug_log_terminal;
#ifdef _MSC_VER
extern setting_bool g_debug_log_output_callstacks;
#endif

static setting_enum g_autoanswer(
    "cmd.auto_answer",
    "Auto-answer terminate prompt",
    "Automatically answers cmd.exe's 'Terminate batch job (Y/N)?' prompts.\n",
    "off,answer_yes,answer_no",
    0);

static setting_str g_admin_title_prefix(
    "cmd.admin_title_prefix",
    "Replaces the console title prefix when elevated",
    "This replaces the console title prefix when cmd.exe is elevated.",
    "");



//------------------------------------------------------------------------------
static hook_type get_hook_type()
{
    static hook_type s_hook_type = app_context::get()->is_detours() ? detour : iat;
    return s_hook_type;
}
static const char* get_kernel_module()
{
    return (get_hook_type() == iat) ? nullptr : "kernel32.dll";
}



//------------------------------------------------------------------------------
static bool get_mui_string(int32 id, wstr_base& out)
{
    DWORD flags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_IGNORE_INSERTS;
    return !!FormatMessageW(flags, nullptr, id, 0, out.data(), out.size(), nullptr);
}

//------------------------------------------------------------------------------
static char s_answered = 0;
static int32 check_auto_answer()
{
    static wstr<72> target_prompt;
    static wstr<16> no_yes;

    // Don't allow infinite loop due to unaccepted autoanswer.
    if (s_answered >= 2)
        return 0;

    // Skip the feature if it's not enabled.
    int32 setting = g_autoanswer.get();
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
static void tag_prompt()
{
    // Tag the prompt so we can detect when cmd.exe writes to the terminal.
    wchar_t buffer[256];
    buffer[0] = '\0';
    __Real_GetEnvironmentVariableW(L"prompt", buffer, sizeof_array(buffer));

    if (buffer[0])
    {
        tagged_prompt prompt;
        prompt.tag(buffer);
        __Real_SetEnvironmentVariableW(L"prompt", prompt.get());
    }
}

//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    __Real_WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
static bool is_elevated()
{
    static bool s_initialized = false;
    static bool s_elevated = false;

    if (!s_initialized)
    {
        HANDLE token = 0;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, false, &token) ||
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            DWORD size = 0;
            TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
            if (GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size))
                s_elevated = (type == TokenElevationTypeFull);
            CloseHandle(token);
        }
        s_initialized = true;
    }

    return s_elevated;
}



//------------------------------------------------------------------------------
void host_load_app_scripts(lua_state& lua)
{
    lua_load_script(lua, app, cmd);
    lua_load_script(lua, app, commands);
    lua_load_script(lua, app, config);
    lua_load_script(lua, app, dir);
    lua_load_script(lua, app, env);
    lua_load_script(lua, app, exec);
    lua_load_script(lua, app, self);
    lua_load_script(lua, app, set);

    lua_load_script(lua, app, prompt);
    lua_load_script(lua, app, suggest);
    lua_load_script(lua, app, update);
    lua_load_script(lua, app, git);
}

//------------------------------------------------------------------------------
void host_cmd_enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line)
{
    auto* const host = host_cmd::get();
    assert(host);
    if (host)
        host->enqueue_lines(lines, hide_prompt, show_line);
}

//------------------------------------------------------------------------------
void host_cleanup_after_signal()
{
    auto* const host = host_cmd::get();
    if (host)
        host->cleanup_after_signal();
}



//------------------------------------------------------------------------------
class read_console_scope
{
public:
    read_console_scope() { ++s_in_read_console; }
    ~read_console_scope() { --s_in_read_console; }
};



//------------------------------------------------------------------------------
host_cmd::host_cmd()
: host("cmd.exe")
, m_doskey(os::get_shellname())
{
}

//------------------------------------------------------------------------------
int32 host_cmd::validate()
{
    if (!is_interactive())
    {
        LOG("Host is not interactive; cancelling inject.");
        return -1;
    }

    return true;
}

//------------------------------------------------------------------------------
static BOOL WINAPI write_console_logging(HANDLE handle, const void* _chars, DWORD to_write, LPDWORD written, LPVOID reserved)
{
    if (g_debug_log_terminal.get() &&
        s_in_read_console && // Only intercept Clink writes, which only happen inside read_console().
        !suppress_implicit_write_console_logging::is_suppressed())
    {
        const char* context = nullptr;
        if (handle == GetStdHandle(STD_OUTPUT_HANDLE))
            context = "CONOUT";
        else if (handle == GetStdHandle(STD_ERROR_HANDLE))
            context = "CONERR";
        if (context)
        {
            const WCHAR* const chars = static_cast<const WCHAR*>(_chars);

            str_moveable s;
            wstr_iter iter(chars, to_write);
            to_utf8(s, iter);

            bool all_ascii = (to_write == s.length());
            if (all_ascii)
            {
                const WCHAR* const end = chars + to_write;
                for (const WCHAR* p = chars; p < end; ++p)
                {
                    if (*p < 0 || *p > 0x7e)
                    {
                        all_ascii = false;
                        break;
                    }
                }
            }

            LOGCURSORPOS(handle);
            if (all_ascii)
                LOG("%s \"%.*s\", %d", context, s.length(), s.c_str(), s.length());
            else
                LOG("%s \"%.*s\", %d utf8, %d utf16", context, s.length(), s.c_str(), s.length(), to_write);
#ifdef _MSC_VER
            if (g_debug_log_output_callstacks.get())
            {
                char stk[8192];
                format_callstack(2, 20, stk, sizeof(stk), false);
                LOG("%s", stk);
            }
#endif
        }
    }

    return __Detoured_WriteConsoleW(handle, _chars, to_write, written, reserved);
}

//------------------------------------------------------------------------------
static BOOL WINAPI write_file_logging(HANDLE handle, const void* _buffer, DWORD to_write, LPDWORD written, LPOVERLAPPED overlapped)
{
    if (g_debug_log_terminal.get() &&
        s_in_read_console && // Only intercept Clink writes, which only happen inside read_console().
        !suppress_implicit_write_console_logging::is_suppressed())
    {
        const char* context = nullptr;
        if (handle == GetStdHandle(STD_OUTPUT_HANDLE))
            context = "FILESTDOUT";
        else if (handle == GetStdHandle(STD_ERROR_HANDLE))
            context = "FILESTDERR";
        if (context)
        {
            const char* const buffer = static_cast<const char*>(_buffer);
            LOGCURSORPOS(handle);
            LOG("%s \"%.*s\", %d", context, to_write, buffer, to_write);
#ifdef _MSC_VER
            if (g_debug_log_output_callstacks.get())
            {
                char stk[8192];
                format_callstack(2, 20, stk, sizeof(stk), false);
                LOG("%s", stk);
            }
#endif
        }
    }

    return __Detoured_WriteFile(handle, _buffer, to_write, written, overlapped);
}

//------------------------------------------------------------------------------
static void maybe_detour_write_console()
{
    if (s_detoured_write_console || !g_debug_log_terminal.get())
        return;

    s_detoured_write_console = true;

    if (get_hook_type() != iat)
        return;

    hook_setter hooks;
    hooks.attach(detour, "kernel32.dll", "WriteConsoleW", &write_console_logging, &__Detoured_WriteConsoleW);
    hooks.attach(detour, "kernel32.dll", "WriteFile", &write_file_logging, &__Detoured_WriteFile);
    hooks.commit();
}

//------------------------------------------------------------------------------
bool host_cmd::initialise()
{
    hook_setter hooks;
    hook_type type = get_hook_type();
    const char* module = get_kernel_module();

    // Hook the setting of the 'prompt' environment variable so we can tag
    // it and detect command entry via a write hook.
    tag_prompt();
    if (!hooks.attach(type, module, "SetEnvironmentVariableW", &host_cmd::set_env_var, &__Real_SetEnvironmentVariableW))
        return false;
    if (!hooks.attach(type, module, "SetEnvironmentStringsW", &host_cmd::set_env_strs, &__Real_SetEnvironmentStringsW))
        return false;
    if (!hooks.attach(type, module, "WriteConsoleW", &host_cmd::write_console, &__Real_WriteConsoleW))
        return false;

    // Set a trap to get a callback when cmd.exe fetches PROMPT environment
    // variable.  GetEnvironmentVariableW is always called before displaying the
    // prompt string the first time after a SET command sets an environment
    // variable, so it's a reliable spot to hook regardless how injection is
    // initiated (AutoRun, command line, etc).
    if (!hooks.attach(type, module, "GetEnvironmentVariableW", &host_cmd::get_env_var, &__Real_GetEnvironmentVariableW))
        return false;

    if (!hooks.commit())
        return false;

    maybe_detour_write_console();
    return true;
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

    clink_shutdown_ctrlevent();
}

//------------------------------------------------------------------------------
void host_cmd::initialise_lua(lua_state& lua)
{
    host_load_app_scripts(lua);
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
        case 'r': return false; // A little-known synonym for /c.
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
void host_cmd::edit_line(wchar_t* chars, int32 max_chars, bool edit)
{
    // If debug.log_terminal is set, ensure detour for logging is installed.
    maybe_detour_write_console();

    // Exiting a nested CMD will remove the aliases, so re-add them if missing.
    // But don't overwrite them if they already exist: let the user override
    // them if so desired.
    add_aliases(false/*force*/);

    m_command_tokeniser.begin_line();

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
            out = chars;
            const char* const prompt = utf8_prompt.empty() ? nullptr : utf8_prompt.c_str();
            const char* const rprompt = utf8_rprompt.empty() ? nullptr : utf8_rprompt.c_str();
            if (host::edit_line(prompt, rprompt, out, edit))
            {
                to_utf16(chars, max_chars, out.c_str());
                break;
            }

            if (is_force_reload_scripts())
            {
            }
            else if (g_ctrld_exits.get())
            {
                wstr_base(chars, max_chars) = L"exit 0";
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
#ifdef DEBUG
bool g_suppress_signal_assert = false;
#endif
BOOL WINAPI host_cmd::read_console(
    HANDLE input,
    void* _chars,
    DWORD max_chars,
    LPDWORD read_in,
    __CONSOLE_READCONSOLE_CONTROL* __control)
{
#if defined(__MINGW32__) || defined(__MINGW64__)
    const CONSOLE_READCONSOLE_CONTROL* const control = reinterpret_cast<CONSOLE_READCONSOLE_CONTROL*>(__control);
#else
    const CONSOLE_READCONSOLE_CONTROL* const control = __control;
#endif

    seh_scope seh;
    read_console_scope reading;

    wchar_t* const chars = reinterpret_cast<wchar_t*>(_chars);

    const bool more_continuation = s_more_continuation;
    s_more_continuation = false;

    // if the input handle isn't a console handle then go the default route.
    if (GetFileType(input) != FILE_TYPE_CHAR)
        return __Real_ReadConsoleW(input, chars, max_chars, read_in, __control);

    host_cmd* const hc = host_cmd::get();

    // clink_maybe_handle_signal() needs g_printer for output.
    dbg_snapshot_heap(prt_ignore);
    auto prt = hc->make_printer_context();
    dbg_ignore_since_snapshot(prt_ignore, "read_console");

    // Always dequeue if queued lines are present:  the More? continuation
    // prompt, a Yes/No/All prompt, an edit line prompt, etc -- in Conhost's
    // implementation of ReadConsoleW all of them return the next $T segment
    // from an expanded doskey macro.

    // If cmd.exe is asking for one character at a time, use the original path
    // It does this to handle y/n/all prompts which isn't an compatible use-
    // case for readline.
    if (max_chars == 1)
    {
        int32 reply;

        if (reply = check_auto_answer())
        {
            *chars = reply;
            *read_in = 1;

            // Echo Clink's response.
            DWORD dummy;
            __Real_WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), chars, 1, &dummy, nullptr);
            return TRUE;
        }

        // Default behaviour.
        if (hc->dequeue_char(chars))
            return TRUE;
        return __Real_ReadConsoleW(input, chars, max_chars, read_in, __control);
    }

    s_answered = 0;

    // Mimic ReadConsole when nInitialChars is out of range; ReadConsole doesn't
    // use SetLastError(), so neither will we.
    if (control && control->nInitialChars >= max_chars)
        return false;

    // Initialize the output buffer.
    wstr_base line(chars, max_chars);
    if (max_chars)
    {
        // Already verified in range, further above.
        assert(!control || control->nInitialChars < max_chars);
        if (control)
            chars[control->nInitialChars] = '\0';
        else
            chars[0] = '\0';
    }

    set_ctrl_wakeup_mask(control ? control->dwCtrlWakeupMask : 0);

    // THEORETICAL:
    //  Clink could maybe intercept ReadConsoleW even when echo is off, if it
    //  used RtlCaptureStackBackTrace to observe the first three frames in the
    //  backtrace.  Once Clink has ever seen a tagged prompt, then it can know
    //  the exact stack frames that correspond to reading the input line.
    //  When ReadConsoleW is called but Clink hasn't seen a tagged prompt,
    //  then Clink could inspect the stack frames to deduce when the call is
    //  for the input line.
    // WHY NOT TO DO THAT:
    //  RtlCaptureStackBackTrace is not reliable for use in non-debug code.

    // Cmd.exe can want line input for reasons other than command entry.
    const wchar_t* prompt = host_cmd::get()->m_prompt.get();
    dequeue_flags flags = dequeue_flags::none;
    if (more_continuation || prompt == nullptr || *prompt == L'\0')
    {
        if (hc->dequeue_line(line, flags))
        {
            if (more_continuation || (flags & dequeue_flags::show_line) != dequeue_flags::none)
            {
                DWORD written;
                HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
                __Real_WriteConsoleW(h, line.c_str(), line.length(), &written, nullptr);
                __Real_WriteConsoleW(h, L"\r\n", 2, &written, nullptr);
            }
            // When the edit_line flag is set the line should be editable.
            // But when not using the Readline editor, give up and treat the
            // line as though it ended with a newline.
            return true;
        }
        return __Real_ReadConsoleW(input, chars, max_chars, read_in, __control);
    }

    // Respond with a queued line, if available.
    if (hc->dequeue_line(line, flags))
    {
        // Trim the newline, since it gets added again further below.
        while (line.length() && line.c_str()[line.length() - 1] == '\n')
            line.truncate(line.length() - 1);

        if (check_dequeue_flag(flags, dequeue_flags::show_line))
        {
            // Need to show the line, which will also show the prompt, so no
            // action is needed here.
        }
        else if (!check_dequeue_flag(flags, dequeue_flags::hide_prompt))
        {
            // Show the most recent prompt.  But first strip any 0x01 and 0x02
            // characters that may be present.
            str<> prompt(get_last_prompt());
            char const* read = prompt.c_str();
            char* write = prompt.data();
            while (*read)
            {
                if (*read != 0x01 && *read != 0x02)
                {
                    *write = *read;
                    ++write;
                }
                ++read;
            }
            *write = '\0';

            g_printer->print(prompt.c_str(), prompt.length());
            // Add a newline so that output always starts on the line after the
            // prompt.  Conhost starts output on the prompt line, making the
            // output look as though it's what was typed as input.  Clink
            // attempts to clear up the confusion.
            g_printer->print("\n");
        }
    }

    if (check_dequeue_flag(flags, dequeue_flags::show_line))
    {
        // Redirection in CMD can change CMD's STD handles, causing Clink's C
        // runtime to have stale handles.  Check and reset them if necessary.
        reset_stdio_handles();

        // Surround the entire edit_line() scope, otherwise any time Clink
        // doesn't read input fast enough the OS can handle processed input
        // while it's enabled between ReadConsoleInputW calls.
        {
            console_config cc(input, true/*accept_mouse_input*/);
            reset_wcwidths();
            hc->edit_line(chars, max_chars, check_dequeue_flag(flags, dequeue_flags::edit_line));

            // Strip any wake characters, since Clink handles completion itself
            // and isn't designed to support wake characters.
            strip_wakeup_chars(chars, max_chars);
        }

        // There's a race condition where this assert can fire, and that's fine.
        // The point is to make sure it generally doesn't bleed through to here.
#ifdef DEBUG
        assert(g_suppress_signal_assert || !clink_is_signaled());
        g_suppress_signal_assert = false;
#endif
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
#ifndef CAPTURE_PUSHD_STACK
static void update_pushd_depth(const wchar_t* chars, int32 char_count)
{
    int32 depth = -1;

    str<> var;
    if (os::get_env("prompt", var) && strstr(var.c_str(), "$+"))
    {
        str<> expanded;
        str<> captured;
        {
            wstr_iter iter(chars, char_count); // Because MINGW can't handle it inline.
            to_utf8(captured, iter);
        }
        if (prompt_utils::expand_prompt_codes(var.c_str(), expanded, expand_prompt_flags::omit_pushd) &&
            expanded.length() <= captured.length())
        {
            // Find point where the strings don't match.
            const char* e = expanded.c_str();
            const char* c = captured.c_str();
            while (*e && *e == *c)
                ++e, ++c;

            // If the strings differ and the captured string has a + then
            // compare the rest of the expanded string with the end of the
            // captured string.
            if (*c == '+')
            {
                const uint32 e_len = uint32(strlen(e));
                const uint32 c_len = uint32(strlen(c));
                if (strcmp(e, c + c_len - e_len) == 0)
                {
                    // If the end of the strings match except for a run of +
                    // characters, then the depth is the difference between
                    // the two strings.
                    depth = c_len - e_len;
                    for (uint32 num = depth; num--; ++c)
                    {
                        if (*c != '+')
                        {
                            depth = -1;
                            break;
                        }
                    }
                }
            }
            else if (!*e && !*c)
            {
                depth = 0;
            }
        }
    }

    os::set_pushd_depth(depth);
}
#endif

//------------------------------------------------------------------------------
bool host_cmd::capture_prompt(const wchar_t* chars, int32 char_count)
{
    // Clink tags the prompt so that it can be detected when cmd.exe
    // writes it to the console.

#ifndef CAPTURE_PUSHD_STACK
    update_pushd_depth(chars, char_count);
#endif

    m_prompt.set(chars, char_count);
    if (!m_prompt.get())
        return false;

#ifdef USE_MEMORY_TRACKING
    dbgsetignore(m_prompt.get(), true);
#endif
    return true;
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
BOOL WINAPI host_cmd::set_env_strs(wchar_t* enviro)
{
    seh_scope seh;

    const BOOL ok = __Real_SetEnvironmentStringsW(enviro);
    tag_prompt();

    return ok;
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
        unhook.detach(get_hook_type(), get_kernel_module(), "GetEnvironmentVariableW", &__Real_GetEnvironmentVariableW, get_env_var);
        unhook.commit();

        host_cmd::get()->initialise_system();
    }
    return ret;
}

//------------------------------------------------------------------------------
static wstr_unordered_set s_old_prefixes;
static linear_allocator s_old_prefix_store(1024);
static bool s_ever_prefix = false;
BOOL WINAPI host_cmd::set_console_title(LPCWSTR lpConsoleTitle)
{
    wstr<> clink_prefix;

    if (is_elevated())
    {
        str<> tmp;
        wstr<280> cmd_prefix;
        g_admin_title_prefix.get(tmp);
        if ((tmp.length() || s_ever_prefix) && get_mui_string(ADMINISTRATOR_TITLE_PREFIX, cmd_prefix))
        {
            s_ever_prefix = true;
            clink_prefix = tmp.c_str();

            // Strip recognized administrator prefixes.
            str_compare_scope _(str_compare_scope::caseless, false);
            while (true)
            {
                const LPCWSTR orig = lpConsoleTitle;
                if (cmd_prefix.length() && str_compare(lpConsoleTitle, cmd_prefix.c_str()) == cmd_prefix.length())
                    lpConsoleTitle += cmd_prefix.length();
                if (clink_prefix.length() && str_compare(lpConsoleTitle, clink_prefix.c_str()) == clink_prefix.length())
                    lpConsoleTitle += clink_prefix.length();
                for (auto& old : s_old_prefixes)
                {
                    const int32 len = str_compare(lpConsoleTitle, old);
                    if (len > 0 && old[len] == '\0')
                        lpConsoleTitle += len;
                }
                if (orig == lpConsoleTitle)
                    break;
            }

            // Remember prefix.
            if (clink_prefix.length() && s_old_prefixes.find(clink_prefix.c_str()) == s_old_prefixes.end())
            {
                const uint32 cb = (clink_prefix.length() + 1) * sizeof(*clink_prefix.c_str());
                wchar_t* ptr = (wchar_t*)s_old_prefix_store.alloc(cb);
                memcpy(ptr, clink_prefix.c_str(), cb);
                s_old_prefixes.emplace(ptr);
            }

            // Concatenate the preferred prefix and the rest of the title.
            wstr_base* title = clink_prefix.length() ? static_cast<wstr_base*>(&clink_prefix) : static_cast<wstr_base*>(&cmd_prefix);
            title->concat(lpConsoleTitle);
            lpConsoleTitle = title->c_str();
        }
    }

    return __Real_SetConsoleTitleW(lpConsoleTitle);
}

//------------------------------------------------------------------------------
bool host_cmd::initialise_system()
{
    {
        hook_type type = get_hook_type();
        const char* module = get_kernel_module();

        // ReadConsoleW is required.
        {
            hook_setter hooks;
            hooks.attach(type, module, "ReadConsoleW", &host_cmd::read_console, &__Real_ReadConsoleW);
            if (!hooks.commit())
                return false;
        }

        // Hook SetConsoleTitleW in order to replace the "Administrator: "
        // prefix, but ignore failure since it's just a minor convenience.
        {
            hook_setter hooks;
            hooks.attach(type, module, "SetConsoleTitleW", &host_cmd::set_console_title, &__Real_SetConsoleTitleW);
            hooks.commit();
        }
    }

    // Add an alias to Clink so it can be run from anywhere. Similar to adding
    // it to the path but this way we can add other arguments if needed.
    add_aliases(true/*force*/);

    // Tag the prompt again just incase it got unset by something like
    // setlocal/endlocal in a boot Batch script.
    tag_prompt();

    return true;
}
