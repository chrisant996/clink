// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_screen_buffer.h"
#include "find_line.h"
#include "terminal_helpers.h"
#include "wcwidth.h"

#include <core/base.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str_iter.h>
#include <core/debugheap.h>
#include <process/process.h>

#include <assert.h>

#include <regex>

// For compatibility with Windows 8.1 SDK.
#if !defined( ENABLE_VIRTUAL_TERMINAL_PROCESSING )
# define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#elif ENABLE_VIRTUAL_TERMINAL_PROCESSING != 0x0004
# error ENABLE_VIRTUAL_TERMINAL_PROCESSING must be 0x0004
#endif

//------------------------------------------------------------------------------
static setting_enum g_terminal_emulation(
    "terminal.emulation",
    "Controls VT emulation",
    "Clink can emulate Virtual Terminal processing if the console doesn't\n"
    "natively.  When set to 'emulate' then Clink performs VT emulation and handles\n"
    "ANSI escape codes.  When 'native' then Clink passes output directly to the\n"
    "console.  Or when 'auto' then Clink performs VT emulation unless native\n"
    "terminal support is detected (such as when hosted inside ConEmu, Windows\n"
    "Terminal, WezTerm, or Windows 10 new console).",
    "native,emulate,auto",
    2);

static setting_enum g_terminal_color_emoji(
    "terminal.color_emoji",
    "Color emoji support in terminal",
    "Set this to indicate whether the terminal program draws emoji using colored\n"
    "double width characters.  This needs to be set accurately in order for Clink\n"
    "to display the input line properly when it contains emoji characters.\n"
    "When set to 'off' Clink assumes emoji are rendered using 1 character cell.\n"
    "When set to 'on' Clink assumes emoji are rendered using 2 character cells.\n"
    "When set to 'auto' (the default) Clink tries to predict how emoji will be\n"
    "rendered based on the OS version and terminal program.",
    "off,on,auto",
    2);

static setting_enum g_terminal_shell_integration(
    "terminal.shell_integration",
    "Send terminal shell integration codes",
    "Clink can send shell integration codes to the terminal, if the terminal\n"
    "supports them.\n"
    "When set to 'off' Clink never sends shell integration codes.\n"
    "When set to 'on' Clink always sends shell integration codes.\n"
    "When set to 'auto' Clink only sends shell integration codes if it detects\n"
    "a terminal that is expected to support them.",
    "off,on,auto",
    2);

//------------------------------------------------------------------------------
static ansi_handler s_native_ansi_handler = ansi_handler::unknown;
static ansi_handler s_current_ansi_handler = ansi_handler::unknown;
static bool s_has_consolev2 = false;
static const char* s_consolez_dll = nullptr;
static const char* s_found_what = nullptr;
static const char* s_ansicon_problem = nullptr;
static const char* s_in_windows_terminal = nullptr;
bool g_color_emoji = false; // Global for performance, since it's accessed in tight loops.

enum { FOUND_BY_AUTO, FOUND_BY_ENV, FOUND_BY_PROFILE };
static char s_found_by = FOUND_BY_PROFILE;

static const char* const s_handler_names[] =
{
    "unknown",
    "clink",
    "ansicon",
    "conemu",
    "winterminal",
    "wezterm",
    "winconsolev2",
    "winconsole",
};
static_assert(sizeof_array(s_handler_names) == unsigned(ansi_handler::max), "must match ansi_handler enum");

ansi_handler get_native_ansi_handler(str_base* name)
{
    if (name)
        *(name) = s_handler_names[unsigned(s_native_ansi_handler)];
    return s_native_ansi_handler;
}

const char* get_ansicon_problem()
{
    return s_ansicon_problem;
}

void make_found_ansi_handler_string(str_base& out)
{
    static const char* const s_friendly_names[] =
    {
        "Unknown",
        "Clink terminal emulation",
        "ANSICON",
        "ConEmu",
        "Windows Terminal",
        "WezTerm",
        "Console V2 (with 24 bit color)",
        "Default console (16 bit color only)",
    };
    static_assert(sizeof_array(s_friendly_names) == unsigned(ansi_handler::max), "must match ansi_handler enum");

    const char* name = s_friendly_names[unsigned(s_current_ansi_handler)];
    switch (s_found_by)
    {
    case FOUND_BY_ENV:
        out.format("%s (set by CLINK_ANSI_HOST)", name);
        break;
    case FOUND_BY_AUTO:
        if (s_found_what)
        {
            out.format("%s (auto mode found '%s')", name, s_found_what);
            break;
        }
        __fallthrough;
    default:
        out = name;
        break;
    }
}

bool make_ftsc(const char* code, str_base& out)
{
    const int32 mode = g_terminal_shell_integration.get();
    const bool is_cwd_code = (strcmp(code, "9;9") == 0);

    bool on = false;
    switch (mode)
    {
    default:
    case 0:
        // In 'off' mode, there's an exception:  ConEmu always supports 9;9.
        if (is_cwd_code && get_current_ansi_handler() == ansi_handler::conemu)
            on = true;
        break;
    case 1:
        // In 'on' mode, send all shell integration codes.
        on = true;
        break;
    case 2:
        // In 'auto' mode, decide based on capabilities of the current ANSI
        // handler.
        switch (get_current_ansi_handler())
        {
        case ansi_handler::wezterm:
            // WezTerm 20240203 is currently the latest release, but it
            // contains an old version of ConPTY that lacks a fix for a bug
            // that makes the cursor teleport around (see #821).  Nightly
            // builds have an updated ConPTY and work, but until a new
            // version is released Clink can't default to enabling shell
            // integration codes in WezTerm.
#if 0
            on = true;
#endif
            break;
        case ansi_handler::winterminal:
            // Windows Terminal supports 133;A, 133;B, 133;C, 133;D, and 9;9.
            on = true;
            break;
        case ansi_handler::conemu:
            // ConEmu only supports 9;9.
            on = is_cwd_code;
            break;
        }
        break;
    }

    out.clear();
    if (on)
    {
        if (is_cwd_code)
        {
            // Lets the terminal know what is the current directory.  Windows
            // Terminal uses this during Duplicate Tab to give the new tab the
            // same current directory as the original tab.
            str<> cwd;
            if (os::get_current_dir(cwd))
                out.format("\x1b]%s;%s\a", code, cwd.c_str());
        }
        else if (strcmp(code, "133;D") == 0)
        {
            // End of Command includes the exit code (errorlevel).
            out.format("\x1b]%s;%u\a", code, os::get_errorlevel());
        }
        else
        {
            out.format("\x1b]%s\a", code);
        }
    }
    return on;
}

ansi_handler get_current_ansi_handler(str_base* name)
{
    if (name)
        (*name) = s_handler_names[unsigned(s_current_ansi_handler)];
    return s_current_ansi_handler;
}

//------------------------------------------------------------------------------
static const char* s_conemu_dll = nullptr;
bool is_conemu()
{
    return !!s_conemu_dll;
}

//------------------------------------------------------------------------------
static const char* const conemu_dll_names[] =
{
    "ConEmuHk.dll",
    "ConEmuHk32.dll",
    "ConEmuHk64.dll",
    nullptr
};
static const char* const ansicon_dll_names[] =
{
    "ANSI.dll",
    "ANSI32.dll",
    "ANSI64.dll",
    nullptr
};
static const char* const consolez_dll_names[] =
{
    "ConsoleHook.dll",
    nullptr
};

//------------------------------------------------------------------------------
static const char* is_dll_loaded(const char* const* dll_names)
{
    while (*dll_names)
    {
        if (GetModuleHandle(*dll_names) != NULL)
            return *dll_names;
        dll_names++;
    }

    return nullptr;
}

#if 0
//------------------------------------------------------------------------------
static bool is_wt_session_present()
{
    str<16> wt_session;
    return os::get_env("WT_SESSION", wt_session);
}

//------------------------------------------------------------------------------
static HWND get_console_window()
{
    HWND hwnd = GetConsoleWindow();
    if (hwnd)
    {
        // Small delay to improve the likelihood that the window is fully
        // initialized.
        Sleep(5);
    }
    return hwnd;
}

//------------------------------------------------------------------------------
static bool has_owner(HWND hwnd)
{
    HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : 0;
    return !!owner;
}

//------------------------------------------------------------------------------
static const char* get_window_class(HWND hwnd, str_base& out)
{
    WCHAR className[256];
    if (hwnd && GetClassNameW(hwnd, className, _countof(className)))
        out = className;
    else
        out.clear();
    return out.c_str();
}

//------------------------------------------------------------------------------
static bool is_window_hidden(HWND hwnd)
{
    return hwnd && !IsWindowVisible(hwnd);
}
#endif

//------------------------------------------------------------------------------
static const char* check_for_windows_terminal()
{
#if 0
    // The big problem with using GW_OWNER to detect Windows Terminal is that
    // it relies on using the console window handle and it's unclear whether a
    // Sleep() is still needed anymore.  The async client/server communication
    // for the console host architecture used to need Sleep() calls to wait
    // for the OS APIs to catch up with the actual process state, which made
    // all use of the console window handle timing dependent, and periodically
    // unreliable.

    //                  No      Default WinTerm VSCode  WezTerm Tabby   ConsoleZ
    // has WT_SESSION   *       *       1       *       *       *       *
    // has window       1       1       1       1       1       1       1
    // has owner        0       1       1       0       0       0       0
    // is hidden        0       0       0       1       1       1       1
    // class            CWC     PCW     PCW     PCW     PCW     PCW     CWC
    //
    //    * = Depends on whether it was launched from within an existing Windows
    //        Terminal session.
    //  PCW = PseudoConsoleWindow
    //  CWC = ConsoleWindowClass
#ifndef DEBUG
    if (os::get_env("CLINK_DEBUG_TERMINAL_DETECTION"))
#endif
    {
        const HWND hwndConsole = get_console_window();
        LOG("console has window?  %u", !!hwndConsole);
        if (hwndConsole)
        {
            str<> tmp;
            LOG("console has owner?  %u", has_owner(hwndConsole));
            LOG("console is hidden?  %u", is_window_hidden(hwndConsole));
            LOG("console class = '%s'", get_window_class(hwndConsole, tmp));
        }
    }
#endif

    // WT_SESSION is highly unreliable for detecting Windows Terminal:
    //  1.  Because environment variables are generally inherited by child
    //      processes.  For example, if WezTerm is launched via `start
    //      wezterm-gui.exe` from inside a Windows Terminal session, then
    //      WT_SESSION will be present inside WezTerm and all child processes
    //      inside WezTerm.
    //  2.  WT_SESSION can only be present if WindowsTerminal.exe was directly
    //      launched.  The "Startup > Default terminal application" setting
    //      bypasses launching WindowsTerminal.exe entirely, and WT_SESSION
    //      doesn't get injected into the environment.

    // Check if parent is WindowsTerminal.exe.
    int32 pid = GetCurrentProcessId();
    str<> full;
    const int32 parent = process(pid).get_parent_pid();
    if (parent)
    {
        process process(parent);
        process.get_file_name(full);
    }
    const char* name = path::get_name(full.c_str());
    if (name && _stricmp(name, "WindowsTerminal.exe") == 0)
        return "WindowsTerminal.exe";

    // Check if a child process conhost.exe has OpenConsoleProxy.dll
    // loaded.  OpenConsole.exe no longer implies Windows Terminal, so it
    // mustn't be used for detection anymore.
    std::vector<DWORD> processes;
    if (__EnumProcesses(processes))
    {
        for (const auto& inner_pid : processes)
        {
            process process(inner_pid);
            if (process.get_parent_pid() != pid)
                continue;
            if (!process.get_file_name(full))
                continue;

            name = path::get_name(full.c_str());
            if (_stricmp(name, "conhost.exe") == 0)
            {
                std::vector<HMODULE> modules;
                if (process.get_modules(modules))
                {
                    for (const auto& module : modules)
                    {
                        if (!process.get_file_name(full, module))
                            continue;

                        name = path::get_name(full.c_str());
                        if (_stricmp(name, "OpenConsoleProxy.dll") == 0)
                            return "OpenConsoleProxy.dll";
                    }
                }
            }
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static bool parse_ansi_handler(const char* env, ansi_handler& out)
{
    for (unsigned i = unsigned(ansi_handler::clink); i < _countof(s_handler_names); ++i)
    {
        if (str_icmp(env, s_handler_names[i]) == 0)
        {
            out = ansi_handler(i);
            return true;
        }
    }
    return false;
}



//------------------------------------------------------------------------------
win_screen_buffer::~win_screen_buffer()
{
    close();
    free(m_attrs);
    free(m_chars);
}

//------------------------------------------------------------------------------
void win_screen_buffer::override_handle()
{
    HANDLE hout = get_std_handle(STD_OUTPUT_HANDLE);
    if (m_handle && hout == m_handle)
        return;

    if (m_handle)
    {
        rollback<uint16> rb(m_ready, 0);
        m_handle = nullptr;
        open();
        begin();
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::open()
{
    assert(!m_handle);
    m_handle = get_std_handle(STD_OUTPUT_HANDLE);
}

//------------------------------------------------------------------------------
void win_screen_buffer::begin()
{
    if (!m_handle)
        open();

    m_ready++;
    if (m_ready > 1)
        return;

    static bool s_detect_native_ansi_handler = true;
    const bool detect_native_ansi_handler = s_detect_native_ansi_handler;

    static bool s_win10_15063 = false;
    static bool s_win11 = false;

    // One-time detection.
    if (detect_native_ansi_handler)
    {
        s_detect_native_ansi_handler = false;

        // Check for native virtual terminal support in Windows.
#pragma warning(push)
#pragma warning(disable:4996)
        OSVERSIONINFO ver = { sizeof(ver) };
        if (GetVersionEx(&ver))
        {
            s_win10_15063 = ((ver.dwMajorVersion > 10) || (ver.dwMajorVersion == 10 && ver.dwBuildNumber >= 15063));
            s_win11 = ((ver.dwMajorVersion > 10) || (ver.dwMajorVersion == 10 && ver.dwBuildNumber >= 22000));

            if (s_win10_15063)
            {
                DWORD type;
                DWORD data;
                DWORD size;
                LSTATUS status = RegGetValue(HKEY_CURRENT_USER, "Console", "ForceV2", RRF_RT_REG_DWORD, &type, &data, &size);
                s_has_consolev2 = (status != ERROR_SUCCESS ||
                                type != REG_DWORD ||
                                size != sizeof(data) ||
                                data != 0);
            }
        }
#pragma warning(pop)

        // Check whether hosted by Windows Terminal.
        s_in_windows_terminal = check_for_windows_terminal();
    }

    // Always recheck the native terminal mode.  For example, it's possible
    // for ANSICON to be loaded or unloaded after Clink is initialized.
    bool forced_ansi_host = false;
    {
        // Start with Unknown.
        s_found_what = nullptr;
        s_found_by = FOUND_BY_PROFILE;
        s_native_ansi_handler = ansi_handler::unknown;
        s_ansicon_problem = nullptr;

        const char* const ansicon_dll = is_dll_loaded(ansicon_dll_names);
        if (ansicon_dll && s_has_consolev2)
        {
            LOG("ANSICON detected (%s) -- avoid ANSICON on Windows 10 or greater; it's unnecessary, less functional, and greatly degrades performance.", ansicon_dll);
            s_ansicon_problem = ansicon_dll;
        }

        do
        {
            // Check CLINK_ANSI_HOST environment variable.
            str<> env;
            if (os::get_env("CLINK_ANSI_HOST", env))
            {
                env.trim();  // Ignore any leading/trailing whitespace.
                if (parse_ansi_handler(env.c_str(), s_native_ansi_handler))
                {
                    s_found_by = FOUND_BY_ENV;
                    forced_ansi_host = true;
                    break;
                }
            }

            // Check =clink.ansihost environment variable if =clink.id equals
            // our parent process id.  This lets check_for_windows_terminal()
            // use a single pass instead of checking the current process and
            // also its parent.
            if (os::get_env("=clink.id", env))
            {
                const uint32 env_id = atoi(env.c_str());
                if (env_id != GetCurrentProcessId())
                {
                    const int32 parent = process().get_parent_pid();
                    if (env_id == parent && os::get_env("=clink.ansihost", env))
                    {
                        env.trim();
                        if (parse_ansi_handler(env.c_str(), s_native_ansi_handler))
                        {
                            s_found_by = FOUND_BY_PROFILE;
                            forced_ansi_host = true;
                            break;
                        }
                    }
                }
            }

            // Check for ConEmu.
            s_conemu_dll = is_dll_loaded(conemu_dll_names);
            if (s_conemu_dll)
            {
                s_found_what = s_conemu_dll;
                s_native_ansi_handler = ansi_handler::conemu;
                // ConEmu has problems with surrogate pairs.  It renders them
                // as one cell but consumes two cells of width, throwing off
                // vertical alignment.
                detect_ucs2_limitation(true/*force*/);
                break;
            }

            // Check for Windows Terminal.
            if (s_in_windows_terminal)
            {
                s_found_what = s_in_windows_terminal;
                s_native_ansi_handler = ansi_handler::winterminal;
                break;
            }

            // Check for WezTerm.
            str<16> wez;
            if (os::get_env("WEZTERM_EXECUTABLE", wez) &&
                os::get_env("WEZTERM_PANE", wez))
            {
                s_found_what = "WEZTERM_EXECUTABLE and WEZTERM_PANE";
                s_native_ansi_handler = ansi_handler::wezterm;
                break;
            }

            // Other terminals encounter limitations with surrogate pairs.
            detect_ucs2_limitation(true/*force*/);

            // Check for Ansi dlls loaded.
            const char* foundwhat = ansicon_dll;
            if (foundwhat)
            {
                s_found_what = foundwhat;
                s_native_ansi_handler = ansi_handler::ansicon;
                break;
            }

            // Check for native virtual terminal support in Windows.
            if (s_has_consolev2)
            {
                s_found_what = "Windows build >= 15063, console V2";
                s_native_ansi_handler = ansi_handler::winconsolev2;
                // DON'T BREAK; CONTINUE DETECTING -- because ConsoleZ doesn't
                // provide ANSI handling, but defeats ConsoleV2 ANSI handling.
            }

            // Check for ConsoleZ dlls loaded.
            s_consolez_dll = is_dll_loaded(consolez_dll_names);
            if (s_consolez_dll && s_native_ansi_handler == ansi_handler::winconsolev2)
            {
                // Downgrade to basic support since ConsoleZ doesn't support
                // 256 color or 24 bit color.
                s_found_what = s_consolez_dll;
                s_native_ansi_handler = ansi_handler::winconsole;
                break;
            }
        }
        while (false);
    }

    // Check for color emoji width handling.
    switch (g_terminal_color_emoji.get())
    {
    default:
    case 0:
        g_color_emoji = false;
        break;
    case 1:
        g_color_emoji = true;
        break;
    case 2:
        if (s_win11)
            g_color_emoji = true;
        else if (!s_win10_15063)
            g_color_emoji = false;
        else if (forced_ansi_host)
            g_color_emoji = (s_native_ansi_handler == ansi_handler::winterminal);
        else
            g_color_emoji = s_in_windows_terminal;
        break;
    }

    if (!GetConsoleMode(m_handle, &m_prev_mode))
        assert(!m_prev_mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(m_handle, &csbi))
    {
        m_default_attr = csbi.wAttributes & attr_mask_all;
        m_bold = !!(m_default_attr & attr_mask_bold);
    }
    else
    {
        m_default_attr = 0x07;
        m_bold = false;
    }
    m_reverse = false;

    char native_vt = m_native_vt;
    ansi_handler new_handler = s_current_ansi_handler;
    const int32 mode = forced_ansi_host ? 2 : g_terminal_emulation.get();
    switch (mode)
    {
    case 0:
        native_vt = true;
        new_handler = s_native_ansi_handler;
        break;

    case 1:
        native_vt = false;
        new_handler = ansi_handler::clink;
        break;

    default:
    case 2:
        native_vt = s_native_ansi_handler >= ansi_handler::first_native;
        new_handler = native_vt ? s_native_ansi_handler : ansi_handler::clink;
        break;
    }

    if (m_native_vt != native_vt || s_current_ansi_handler != new_handler)
    {
        const char* which = native_vt ? "native" : "emulated";
        if (s_found_what && mode == 2)
            LOG("Using %s terminal support (auto mode found '%s').", which, s_found_what);
        else
            LOG("Using %s terminal support.", which);

        const char* value = nullptr;
        if (new_handler != ansi_handler::unknown)
            value = s_handler_names[unsigned(new_handler)];
        os::set_env("=clink.ansihost", value);
    }

    m_native_vt = native_vt;
    s_current_ansi_handler = new_handler;
    s_found_by = ((mode != 2) ? FOUND_BY_PROFILE :
                  forced_ansi_host ? FOUND_BY_ENV :
                  FOUND_BY_AUTO);

    if (m_native_vt)
        SetConsoleMode(m_handle, m_prev_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

//------------------------------------------------------------------------------
void win_screen_buffer::end()
{
    if (m_ready > 0)
    {
        m_ready--;
        if (!m_ready)
        {
            SetConsoleTextAttribute(m_handle, m_default_attr);
            SetConsoleMode(m_handle, m_prev_mode);
        }
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::close()
{
    m_handle = nullptr;
}

//------------------------------------------------------------------------------
void win_screen_buffer::write(const char* data, int32 length)
{
    assert(m_ready);

    str_iter iter(data, length);

#ifdef DEBUG
    const uint32 stack_buf_len = 64;
#else
    const uint32 stack_buf_len = 384;
#endif

    if (length < stack_buf_len)
    {
        wchar_t wbuf[stack_buf_len];
        int32 n = to_utf16(wbuf, sizeof_array(wbuf), iter);
        if (length && !n && !*data)
        {
            assert(false); // Very inefficient, and shouldn't be possible.
            wbuf[0] = '\0';
            n = 1;
        }

        DWORD written;
        WriteConsoleW(m_handle, data ? wbuf : nullptr, n, &written, nullptr);
    }
    else
    {
        wstr_moveable out;
        out.reserve(length);
        int32 n = to_utf16(out, iter);
        if (length && !n && !*data)
        {
            assert(false); // Very inefficient, and shouldn't be possible.
            assert(out.c_str()[0] == '\0');
            n = 1;
        }

        DWORD written;
        WriteConsoleW(m_handle, out.c_str(), n, &written, nullptr);
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::flush()
{
    // When writing to the console conhost.exe will restart the cursor blink
    // timer and hide it which can be disorientating, especially when moving
    // around a line. The below will make sure it stays visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(m_handle, &csbi))
        SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::get_columns() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return 80;
    return csbi.dwSize.X;
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::get_rows() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return 25;
    return (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::get_top() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return 0;
    return csbi.srWindow.Top;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::get_cursor(int16& x, int16& y) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return false;
    x = csbi.dwCursorPosition.X;
    y = csbi.dwCursorPosition.Y;
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::get_line_text(int32 line, str_base& out) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return false;

    if (!ensure_chars_buffer(csbi.dwSize.X))
        return false;

    COORD coord = { 0, short(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputCharacterW(m_handle, m_chars, csbi.dwSize.X, coord, &len) || !len)
        return false;

    while (len > 0 && iswspace(m_chars[len - 1]))
        len--;

    out.clear();
    wstr_iter tmpi(m_chars, len);
    to_utf8(out, tmpi);
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::has_native_vt_processing() const
{
    return m_native_vt > 0;
}

//------------------------------------------------------------------------------
void win_screen_buffer::clear(clear_type type)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    int32 width, height, count = 0;
    COORD xy;

    switch (type)
    {
    case clear_type_all:
        width = csbi.dwSize.X;
        height = (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
        xy = { 0, csbi.srWindow.Top };
        break;

    case clear_type_before:
        width = csbi.dwSize.X;
        height = csbi.dwCursorPosition.Y - csbi.srWindow.Top;
        xy = { 0, csbi.srWindow.Top };
        count = csbi.dwCursorPosition.X + 1;
        break;

    case clear_type_after:
        width = csbi.dwSize.X;
        height = csbi.srWindow.Bottom - csbi.dwCursorPosition.Y;
        xy = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
        count = width - csbi.dwCursorPosition.X;
        break;
    }

    count += width * height;

    DWORD written;
    FillConsoleOutputCharacterW(m_handle, ' ', count, xy, &written);
    FillConsoleOutputAttribute(m_handle, csbi.wAttributes, count, xy, &written);
}

//------------------------------------------------------------------------------
void win_screen_buffer::clear_line(clear_type type)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    int32 width;
    COORD xy;
    switch (type)
    {
    case clear_type_all:
        width = csbi.dwSize.X;
        xy = { 0, csbi.dwCursorPosition.Y };
        break;

    case clear_type_before:
        width = csbi.dwCursorPosition.X + 1;
        xy = { 0, csbi.dwCursorPosition.Y };
        break;

    case clear_type_after:
        width = csbi.dwSize.X - csbi.dwCursorPosition.X;
        xy = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
        break;
    }

    DWORD written;
    FillConsoleOutputCharacterW(m_handle, ' ', width, xy, &written);
    FillConsoleOutputAttribute(m_handle, csbi.wAttributes, width, xy, &written);
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_horiz_cursor(int32 column)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    const SMALL_RECT& window = csbi.srWindow;
    int32 width = (window.Right - window.Left) + 1;
    int32 height = (window.Bottom - window.Top) + 1;

    column = clamp(column, 0, width - 1);

    COORD xy = { short(window.Left + column), csbi.dwCursorPosition.Y };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_cursor(int32 column, int32 row)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    const SMALL_RECT& window = csbi.srWindow;
    int32 width = (window.Right - window.Left) + 1;
    int32 height = (window.Bottom - window.Top) + 1;

    column = clamp(column, 0, width - 1);
    row = clamp(row, 0, height - 1);

    COORD xy = { short(window.Left + column), short(window.Top + row) };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::move_cursor(int32 dx, int32 dy)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    COORD xy = {
        short(clamp(csbi.dwCursorPosition.X + dx, 0, csbi.dwSize.X - 1)),
        short(clamp(csbi.dwCursorPosition.Y + dy, 0, csbi.dwSize.Y - 1)),
    };
    SetConsoleCursorPosition(m_handle, xy);
}

//------------------------------------------------------------------------------
void win_screen_buffer::save_cursor()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    const SMALL_RECT& window = csbi.srWindow;
    int32 width = (window.Right - window.Left) + 1;
    int32 height = (window.Bottom - window.Top) + 1;

    m_saved_cursor = {
        short(clamp(csbi.dwCursorPosition.X - window.Left, 0, width)),
        short(clamp(csbi.dwCursorPosition.Y - window.Top, 0, height)),
    };
}

//------------------------------------------------------------------------------
void win_screen_buffer::restore_cursor()
{
    if (m_saved_cursor.X >= 0 && m_saved_cursor.Y >= 0)
        set_cursor(m_saved_cursor.X, m_saved_cursor.Y);
}

//------------------------------------------------------------------------------
void win_screen_buffer::insert_chars(int32 count)
{
    if (count <= 0)
        return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    SMALL_RECT rect;
    rect.Left = csbi.dwCursorPosition.X;
    rect.Right = csbi.dwSize.X;
    rect.Top = rect.Bottom = csbi.dwCursorPosition.Y;

    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = csbi.wAttributes;

    csbi.dwCursorPosition.X += count;

    ScrollConsoleScreenBuffer(m_handle, &rect, NULL, csbi.dwCursorPosition, &fill);
}

//------------------------------------------------------------------------------
void win_screen_buffer::delete_chars(int32 count)
{
    if (count <= 0)
        return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    SMALL_RECT rect;
    rect.Left = csbi.dwCursorPosition.X + count;
    rect.Right = csbi.dwSize.X - 1;
    rect.Top = rect.Bottom = csbi.dwCursorPosition.Y;

    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = csbi.wAttributes;

    ScrollConsoleScreenBuffer(m_handle, &rect, NULL, csbi.dwCursorPosition, &fill);

    int32 chars_moved = rect.Right - rect.Left + 1;
    if (chars_moved < count)
    {
        COORD xy = csbi.dwCursorPosition;
        xy.X += chars_moved;

        count -= chars_moved;

        DWORD written;
        FillConsoleOutputCharacterW(m_handle, ' ', count, xy, &written);
        FillConsoleOutputAttribute(m_handle, csbi.wAttributes, count, xy, &written);
    }
}

//------------------------------------------------------------------------------
void win_screen_buffer::set_attributes(attributes attr)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return;

    int32 out_attr = csbi.wAttributes & attr_mask_all;

    // Un-reverse so processing can operate on normalized attributes.
    if (m_reverse)
    {
        int32 fg = (out_attr & ~attr_mask_bg);
        int32 bg = (out_attr & attr_mask_bg);
        out_attr = (fg << 4) | (bg >> 4);
    }

    // Note to self; lookup table is probably much faster.
    auto swizzle = [] (int32 rgbi) {
        int32 b_r_ = ((rgbi & 0x01) << 2) | !!(rgbi & 0x04);
        return (rgbi & 0x0a) | b_r_;
    };

    // Map RGB/XTerm256 colors
    if (!find_best_palette_match(attr))
        return;

    // Bold
    bool apply_bold = false;
    if (auto bold_attr = attr.get_bold())
    {
        m_bold = !!(bold_attr.value);
        apply_bold = true;
    }

    // Underline
    if (auto underline = attr.get_underline())
    {
        if (underline.value)
            out_attr |= attr_mask_underline;
        else
            out_attr &= ~attr_mask_underline;
    }

    // Foreground color
    bool bold = m_bold;
    if (auto fg = attr.get_fg())
    {
        int32 value = fg.is_default ? m_default_attr : swizzle(fg->value);
        value &= attr_mask_fg;
        out_attr = (out_attr & ~attr_mask_fg) | value;
        bold |= (value > 7);
    }
    else
        bold |= (out_attr & attr_mask_bold) != 0;

    // Adjust intensity per bold.  Bold can add intensity.  Nobold can remove
    // intensity added by bold, but cannot remove intensity built into the
    // color number.
    //
    // In other words:
    //  - If the color is 36 (cyan) then bold can make it bright cyan.
    //  - If the color is 36 (cyan) then nobold has no visible effect.
    //  - If the color is 1;36 (bold cyan) then nobold can make it cyan.
    //  - If the color is 96 (bright cyan) then bold has no visible effect (but
    //    some terminal implementations apply a bold font with bright cyan as
    //    the color).
    //  - If the color is 96 (bright cyan) then nobold has no visible effect.
    //  - If the color is 1;96 (bold bright cyan) then nobold has no visible
    //    effect (but some terminal implementations apply a non-bold font with
    //    bright cyan as the color).
    if (apply_bold)
    {
        if (bold)
            out_attr |= attr_mask_bold;
        else
            out_attr &= ~attr_mask_bold;
    }

    // Background color
    if (auto bg = attr.get_bg())
    {
        int32 value = bg.is_default ? m_default_attr : (swizzle(bg->value) << 4);
        out_attr = (out_attr & ~attr_mask_bg) | (value & attr_mask_bg);
    }

    // Reverse video
    if (auto rev = attr.get_reverse())
        m_reverse = rev.value;

    // Apply reverse video
    if (m_reverse)
    {
        int32 fg = (out_attr & ~attr_mask_bg);
        int32 bg = (out_attr & attr_mask_bg);
        out_attr = (fg << 4) | (bg >> 4);
    }

    out_attr |= csbi.wAttributes & ~attr_mask_all;
    SetConsoleTextAttribute(m_handle, short(out_attr));
}

//------------------------------------------------------------------------------
static bool find_best_palette_match(void* handle, const RGB_t& rgb, uint8& attr)
{
    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    if (!proc)
        return false;

    CONSOLE_SCREEN_BUFFER_INFOEX infoex = { sizeof(infoex) };
    if (!GCSBIEx(proc)(handle, &infoex))
        return false;

    const RGB_t (*palette)[16] = reinterpret_cast<const RGB_t (*)[16]>(&infoex.ColorTable);
    const int32 best_idx = FindBestPaletteMatch(rgb, *palette);
    if (best_idx < 0)
        return false;

    static const int32 dos_to_ansi_order[] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    attr = (best_idx & 0x08) + dos_to_ansi_order[best_idx & 0x07];
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::find_best_palette_match(attributes& attr) const
{
    const attributes::color fg = attr.get_fg().value;
    const attributes::color bg = attr.get_bg().value;
    if (fg.is_rgb)
    {
        uint8 val;
        RGB_t rgb;
        fg.as_888(rgb);
        if (!::find_best_palette_match(m_handle, rgb, val))
            return false;
        attr.set_fg(val);
    }
    if (bg.is_rgb)
    {
        uint8 val;
        RGB_t rgb;
        bg.as_888(rgb);
        if (!::find_best_palette_match(m_handle, rgb, val))
            return false;
        attr.set_bg(val);
    }
    return true;
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::is_line_default_color(int32 line) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    int32 ret = true;
    COORD coord = { 0, short(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_handle, m_attrs, csbi.dwSize.X, coord, &len))
        return -1;
    if (len != csbi.dwSize.X)
        return -1;

    for (const WORD* attr = m_attrs; len--; attr++)
        if (*attr != m_default_attr)
            return false;

    return true;
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -1;

    if (!ensure_attrs_buffer(csbi.dwSize.X))
        return -1;

    int32 ret = true;
    COORD coord = { 0, short(line) };
    DWORD len = 0;
    if (!ReadConsoleOutputAttribute(m_handle, m_attrs, csbi.dwSize.X, coord, &len))
        return -1;
    if (len != csbi.dwSize.X)
        return -1;

    const BYTE* end_attrs = attrs + num_attrs;
    for (const WORD* attr = m_attrs; len--; attr++)
    {
        for (const BYTE* find_attr = attrs; find_attr < end_attrs; find_attr++)
            if ((BYTE(*attr) & mask) == (*find_attr & mask))
                return true;
    }

    return false;
}

//------------------------------------------------------------------------------
int32 win_screen_buffer::find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs, int32 num_attrs, BYTE mask) const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(m_handle, &csbi))
        return -2;

    if (text && !ensure_chars_buffer(csbi.dwSize.X))
        return -2;
    if (attrs && num_attrs > 0 && !ensure_attrs_buffer(csbi.dwSize.X))
        return -2;

    return ::find_line(m_handle, csbi,
                       m_chars, m_chars_capacity,
                       m_attrs, m_attrs_capacity,
                       starting_line, distance,
                       text, mode,
                       attrs, num_attrs, mask);
}

//------------------------------------------------------------------------------
bool win_screen_buffer::ensure_chars_buffer(int32 width) const
{
    if (width > m_chars_capacity)
    {
        WCHAR* chars = static_cast<WCHAR*>(realloc(m_chars, (width + 1) * sizeof(*m_chars)));
        if (!chars)
            return false;
        m_chars = chars;
        m_chars_capacity = width;
#ifdef USE_MEMORY_TRACKING
        dbgsetignore(chars);
        dbgsetlabel(chars, "win_screen_buffer::m_chars", false);
#endif
    }
    return true;
}

//------------------------------------------------------------------------------
bool win_screen_buffer::ensure_attrs_buffer(int32 width) const
{
    if (width > m_attrs_capacity)
    {
        m_attrs = static_cast<WORD*>(malloc((width + 1) * sizeof(*m_attrs)));
        if (!m_attrs)
            return false;
        m_attrs_capacity = width;
    }
    return true;
}
