// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "host_lua.h"

#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <core/str_transform.h>
#include <core/log.h>
#include <core/debugheap.h>
#include <core/callstack.h>
#include <core/assert_improved.h>
#include <lib/doskey.h>
#include <lib/match_generator.h>
#include <lib/line_editor.h>
#include <lib/line_editor_integration.h>
#include <lib/intercept.h>
#include <lib/clink_ctrlevent.h>
#include <lib/clink_rl_signal.h>
#include <lib/errfile_reader.h>
#include <lib/sticky_search.h>
#include <lib/display_readline.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lua/prompt.h>
#include <lua/suggest.h>
#include <terminal/printer.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>
#include <utils/app_context.h>

#include <list>
#include <memory>
#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
}

//------------------------------------------------------------------------------
setting_enum g_ignore_case(
    "match.ignore_case",
    "Case insensitive matching",
    "Toggles whether case is ignored when selecting matches.  The 'relaxed'\n"
    "option will also consider -/_ as equal.",
    "off,on,relaxed",
    2);

setting_bool g_fuzzy_accent(
    "match.ignore_accent",
    "Accent insensitive matching",
    "Toggles whether accents on characters are ignored when selecting matches.",
    true);

static setting_bool g_filter_prompt(
    "clink.promptfilter",
    "Enable prompt filtering by Lua scripts",
    true);

enum prompt_spacing { normal, compact, sparse, MAX };
static setting_enum s_prompt_spacing(
    "prompt.spacing",
    "Controls spacing before prompt",
    "The default is 'normal' which never removes or adds blank lines.  Set to\n"
    "'compact' to remove blank lines before the prompt, or set to 'sparse' to\n"
    "remove blank lines and then add one blank line.",
    "normal,compact,sparse",
    prompt_spacing::normal);

static setting_enum s_prompt_transient(
    "prompt.transient",
    "Controls when past prompts are collapsed",
    "The default is 'off' which never collapses past prompts.  Set to 'always' to\n"
    "always collapse past prompts.  Set to 'same_dir' to only collapse past prompts\n"
    "when the current working directory hasn't changed since the last prompt.",
    "off,always,same_dir",
    0);

setting_bool g_save_history(
    "history.save",
    "Save history between sessions",
    "Changing this setting only takes effect for new instances.",
    true);

static setting_enum g_directories_dupe_mode(
    "directories.dupe_mode",
    "Controls duplicates in directory history",
    "Controls how the current directory history is updated.  A value of 'add' (the\n"
    "default) always adds the current directory to the directory history.  A value\n"
    "of 'erase_prev' will erase any previous entries for the current directory and\n"
    "then add it to the directory history.\n"
    "Note that directory history is not saved between sessions.",
    "add,erase_prev",
    0);

static setting_str g_exclude_from_history_cmds(
    "history.dont_add_to_history_cmds",
    "Commands not automatically added to the history",
    "List of commands that aren't automatically added to the history.\n"
    "Commands are separated by spaces, commas, or semicolons.  Default is\n"
    "\"exit history\", to exclude both of those commands.",
    "exit history");

setting_bool g_history_autoexpand(
    "history.auto_expand",
    "Perform history expansion automatically",
    "When enabled, history expansion is automatically performed when a command\n"
    "line is accepted (by pressing Enter).  When disabled, history expansion is\n"
    "performed only when a corresponding expansion command is used (such as\n"
    "'clink-expand-history' Alt-^, or 'clink-expand-line' Alt-Ctrl-E).",
    true);

static setting_bool g_reload_scripts(
    "lua.reload_scripts",
    "Reload scripts on every prompt",
    "When true, Lua scripts are reloaded on every prompt.  When false, Lua scripts\n"
    "are loaded once.  This setting can be changed while Clink is running and takes\n"
    "effect at the next prompt.",
    false);

static setting_bool g_get_errorlevel(
    "cmd.get_errorlevel",
    "Retrieve last exit code",
    "When this is enabled, Clink runs a hidden 'echo %errorlevel%' command before\n"
    "each interactive input prompt to retrieve the last exit code for use by Lua\n"
    "scripts.  If you experience problems, try turning this off.  This is on by\n"
    "default.",
    true);

static setting_enum g_clink_autoupdate(
    "clink.autoupdate",
    "Auto-update the Clink program files",
    "When 'off', Clink does not automatically check for updates, but you can\n"
    "use 'clink update' to check for updates.\n"
    "When 'check' (the default), Clink periodically checks for updates and\n"
    "prints a message when an update is available.\n"
    "When 'prompt', Clink periodically checks for updates and if one is\n"
    "available then it shows a window to prompt whether to install the update.\n"
    "When 'auto', Clink periodically checks for updates and also\n"
    "attempts to automatically install an update.  If elevation is needed then\n"
    "it pops up a prompt window, otherwise it automatically installs the update.",
    "off,check,prompt,auto",
    1); // WARNING: The default is duplicated in load_internal in settings.cpp.

static setting_int g_clink_update_interval(
    "clink.update_interval",
    "Days between update checks",
    "The Clink autoupdater will wait this many days between update checks.",
    5);


#ifdef DEBUG
static setting_bool g_debug_heap_stats(
    "debug.heap_stats",
    "Report heap stats for each edit line",
    "At the beginning of each edit line, report the heap stats since the previous\n"
    "edit line.",
    false);
#endif

extern setting_bool g_autosuggest_enable;
extern setting_bool g_classify_words;
extern setting_color g_color_prompt;
extern setting_bool g_prompt_async;

extern bool can_suggest_internal(const line_state& line);

#ifdef DEBUG
extern bool g_suppress_signal_assert;
#endif



//------------------------------------------------------------------------------
static void get_errorlevel_tmp_name(str_base& out, const char* ext, bool wild=false)
{
    app_context::get()->get_log_path(out);
    path::to_parent(out, nullptr);
    path::append(out, "clink_errorlevel_");


    if (wild)
    {
        out << "*";
    }
    else
    {
        if (is_null_or_empty(ext))
            ext = "txt";

        str<> name;
        name.format("%X.%s", GetCurrentProcessId(), is_null_or_empty(ext) ? "txt" : ext);
        out.concat(name.c_str(), name.length());
    }
}



//------------------------------------------------------------------------------
class dir_history_entry : public no_copy
{
public:
                    dir_history_entry(const char* s);
                    dir_history_entry(dir_history_entry&& d) { dir = d.dir; d.dir = nullptr; }
                    ~dir_history_entry() { free(dir); }

    const char*     get() const { return dir; }

private:
    char* dir;
};

//------------------------------------------------------------------------------
dir_history_entry::dir_history_entry(const char* s)
{
    size_t alloc = strlen(s) + 1;
    dir = (char *)malloc(alloc);
    memcpy(dir, s, alloc);
}

//------------------------------------------------------------------------------
const int32 c_max_dir_history = 100;
static std::list<dir_history_entry> s_dir_history;

//------------------------------------------------------------------------------
static void update_dir_history()
{
    str<> cwd;
    os::get_current_dir(cwd);

    bool add = true;                    // 'add'
    switch (g_directories_dupe_mode.get())
    {
    case 1:                             // 'erase_prev'
        {
            auto iter = s_dir_history.begin();
            while (iter != s_dir_history.end())
            {
                auto next(iter);
                ++next;
                if (_stricmp(iter->get(), cwd.c_str()) == 0)
                    s_dir_history.erase(iter);
                iter = next;
            }
        }
        break;
    }

    if (!s_dir_history.size())
        add = true;
    else if (add && _stricmp(s_dir_history.back().get(), cwd.c_str()) == 0)
        add = false;

    // Add cwd to tail.
    if (add)
    {
        dbg_ignore_scope(snapshot, "History");
        s_dir_history.push_back(cwd.c_str());
    }

    // Trim overflow from head.
    while (s_dir_history.size() > c_max_dir_history)
        s_dir_history.pop_front();
}

//------------------------------------------------------------------------------
static void prev_dir_history(str_base& inout)
{
    inout.clear();

    if (s_dir_history.size() < 2)
        return;

    auto a = s_dir_history.rbegin();
    a++;

    str<> drive;
    path::get_drive(a->get(), drive);
    if (!drive.empty())
        inout.format(" %s & cd \"%s\"", drive.c_str(), a->get());
    else
        inout.format(" cd \"%s\"", a->get());
}

//------------------------------------------------------------------------------
bool host_remove_dir_history(int32 index)
{
    for (auto iter = s_dir_history.begin(); iter != s_dir_history.end(); ++iter, --index)
    {
        if (index == 0)
        {
            s_dir_history.erase(iter);
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
void host_get_app_context(int32& id, host_context& context)
{
    const auto* app = app_context::get();
    if (app)
    {
        id = app->get_id();
        app->get_binaries_dir(context.binaries);
        app->get_state_dir(context.profile);
        app->get_default_settings_file(context.default_settings);
        app->get_default_init_file(context.default_inputrc);
        app->get_settings_path(context.settings);
        app->get_script_path_readable(context.scripts);
    }
}



//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
static void adjust_prompt_spacing()
{
    assert(g_printer);

    static_assert(MAX == prompt_spacing::MAX, "ambiguous symbol");
    const int32 _spacing = s_prompt_spacing.get();
    const prompt_spacing spacing = (_spacing < normal || _spacing >= MAX) ? normal : prompt_spacing(_spacing);

    if (spacing != normal)
    {
        // Consume blank lines before the prompt.  The prompt.spacing concept
        // was inspired by Powerlevel10k, which doesn't consume blank lines,
        // but CMD causes blank lines more often than zsh does.  So to achieve
        // a similar effect it's necessary to actively consume blank lines.
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetConsoleScreenBufferInfo(h, &csbi) && csbi.dwCursorPosition.X == 0)
        {
            str<> text;
            SHORT y = csbi.dwCursorPosition.Y;
            while (y > 0)
            {
                if (!g_printer->get_line_text(y - 1, text))
                    break;
                if (!text.empty())
                    break;
                --y;
            }
            if (y < csbi.dwCursorPosition.Y)
            {
                COORD pos;
                pos.X = 0;
                pos.Y = y;
                SetConsoleCursorPosition(h, pos);
            }
        }
    }
}



//------------------------------------------------------------------------------
struct autostart_display
{
    void save();
    void restore();

    COORD m_pos = {};
    COORD m_saved = {};
    std::unique_ptr<CHAR_INFO[]> m_screen_content;
};

//------------------------------------------------------------------------------
void autostart_display::save()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return;
    if (csbi.dwCursorPosition.X != 0)
        return;

    m_pos = csbi.dwCursorPosition;
    m_saved.X = csbi.dwSize.X;
    m_saved.Y = 1;
    m_screen_content = std::unique_ptr<CHAR_INFO[]>(new CHAR_INFO[csbi.dwSize.X]);

    SMALL_RECT sr { 0, m_pos.Y, static_cast<SHORT>(csbi.dwSize.X - 1), m_pos.Y };
    if (!ReadConsoleOutput(h, m_screen_content.get(), m_saved, COORD {}, &sr))
        m_screen_content.reset();
}

//------------------------------------------------------------------------------
void autostart_display::restore()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return;
    if (csbi.dwCursorPosition.X != 0)
        return;

    if (m_pos.Y + 1 != csbi.dwCursorPosition.Y)
        return;
    if (m_saved.X != csbi.dwSize.X)
        return;

    SMALL_RECT sr { 0, m_pos.Y, static_cast<SHORT>(csbi.dwSize.X - 1), m_pos.Y };
    std::unique_ptr<CHAR_INFO[]> screen_content = std::unique_ptr<CHAR_INFO[]>(new CHAR_INFO[csbi.dwSize.X]);
    if (!ReadConsoleOutput(h, screen_content.get(), m_saved, COORD {}, &sr))
        return;

    if (memcmp(screen_content.get(), m_screen_content.get(), sizeof(CHAR_INFO) * m_saved.Y * m_saved.X) != 0)
        return;

    SetConsoleCursorPosition(h, m_pos);
}

//------------------------------------------------------------------------------
static void move_cursor_up_one_line()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(h, &csbi))
    {
        if (csbi.dwCursorPosition.Y > 0)
            csbi.dwCursorPosition.Y--;
        SetConsoleCursorPosition(h, csbi.dwCursorPosition);
    }
}



//------------------------------------------------------------------------------
host::host(const char* name)
: m_name(name)
, m_doskey(os::get_shellname())
{
    m_terminal = terminal_create();
    m_printer = new printer(*m_terminal.out);

    assert(!get_lua_terminal_input());
    set_lua_terminal(m_terminal.in, m_terminal.out);
}

//------------------------------------------------------------------------------
host::~host()
{
    purge_old_files();

    delete m_prompt_filter;
    delete m_suggester;
    delete m_lua;
    delete m_printer;

    set_lua_terminal(nullptr, nullptr);
    terminal_destroy(m_terminal);
}

//------------------------------------------------------------------------------
void host::enqueue_lines(std::list<str_moveable>& lines, bool hide_prompt, bool show_line)
{
    // It's nonsensical to hide the prompt and show the line.
    // It's nonsensical to edit the line but not show the line.
    assert(!(hide_prompt && show_line));

    for (auto& line : lines)
    {
        dequeue_flags flags = dequeue_flags::none;
        if (hide_prompt) flags |= dequeue_flags::hide_prompt;
        if (show_line)
        {
            flags |= dequeue_flags::show_line;
            if (line.empty() || line[line.length() - 1] != '\n')
                flags |= dequeue_flags::edit_line;
        }
        m_queued_lines.emplace_back(std::move(line), flags);
    }
}

//------------------------------------------------------------------------------
bool host::dequeue_line(wstr_base& out, dequeue_flags& flags)
{
    if (m_bypass_dequeue)
    {
        m_bypass_dequeue = false;
        flags = m_bypass_flags;
        return false;
    }

    clink_maybe_handle_signal();
    if (m_queued_lines.empty())
    {
        flags = dequeue_flags::show_line|dequeue_flags::edit_line;
        return false;
    }

    const auto& front = m_queued_lines.front();
    out = front.m_line.c_str() + m_char_cursor;
    flags = front.m_flags;

    std::list<str_moveable> queue;
    if (!m_char_cursor &&
        (flags & (dequeue_flags::hide_prompt|dequeue_flags::show_line)) == dequeue_flags::none)
    {
        str<> line(front.m_line.c_str());
        while (line.length())
        {
            const char c = line[line.length() - 1];
            if (c == '\r' || c == '\n')
                line.truncate(line.length() - 1);
            else
                break;
        }
        doskey_alias alias;
        m_doskey.resolve(line.c_str(), alias);
        if (alias)
        {
            str_moveable next;
            if (alias.next(next))
                out = next.c_str();
            while (alias.next(next))
                queue.push_front(std::move(next));
        }
    }

    pop_queued_line();

    for (auto& line : queue)
        m_queued_lines.emplace_front(std::move(line), dequeue_flags::hide_prompt);

    return true;
}

//------------------------------------------------------------------------------
bool host::dequeue_char(wchar_t* out)
{
    clink_maybe_handle_signal();
    if (m_queued_lines.empty())
        return false;

    const auto& line = m_queued_lines.front().m_line;
    if (m_char_cursor >= line.length())
    {
        assert(false);
        return false;
    }

    *out = line.c_str()[m_char_cursor++];
    if (m_char_cursor >= line.length())
        pop_queued_line();
    return true;
}

//------------------------------------------------------------------------------
void host::cleanup_after_signal()
{
    m_queued_lines.clear();
    m_char_cursor = 0;
    m_skip_provide_line = true;
}

//------------------------------------------------------------------------------
void host::pop_queued_line()
{
    m_queued_lines.pop_front();
    m_char_cursor = 0;
}

//------------------------------------------------------------------------------
void host::filter_prompt()
{
    if (!g_prompt_async.get())
        return;

    dbg_ignore_scope(snapshot, "Prompt filter");

    bool ok;
    const char* rprompt = nullptr;
    const char* prompt = filter_prompt(&rprompt, ok, false/*transient*/);

    if (!ok)
    {
        // Force redisplaying the prompt, since there was a script failure and
        // the prompt is likely missing.
        set_prompt("", "", false/*redisplay*/);
    }

    set_prompt(prompt, rprompt, true/*redisplay*/);
}

//------------------------------------------------------------------------------
void host::filter_transient_prompt(bool final)
{
    if (!m_can_transient)
    {
cant:
        update_last_cwd();
        return;
    }

    const char* rprompt;
    const char* prompt;
    bool ok;                    // Not used for transient prompt.

    // Replace old prompt with transient prompt.
    rprompt = nullptr;
    prompt = filter_prompt(&rprompt, ok, true/*transient*/, final);
    if (!prompt)
        goto cant;

    set_prompt(prompt, rprompt, true/*redisplay*/, true/*transient*/);

    if (final)
        return;

    // Refilter new prompt, but don't redisplay (which would replace the prompt
    // again; not what is needed here).  Instead let the prompt get displayed
    // again naturally in due time.
    rprompt = nullptr;
    prompt = filter_prompt(&rprompt, ok, false/*transient*/);
    set_prompt(prompt, rprompt, false/*redisplay*/);
}

//------------------------------------------------------------------------------
bool host::can_suggest(const line_state& line)
{
    return (m_suggester &&
            g_autosuggest_enable.get() &&
            ::can_suggest_internal(line));
}

//------------------------------------------------------------------------------
bool host::suggest(const line_states& lines, matches* matches, int32 generation_id)
{
    return (m_suggester &&
            g_autosuggest_enable.get() &&
            m_suggester->suggest(lines, matches, generation_id));
}

//------------------------------------------------------------------------------
bool host::filter_matches(char** matches)
{
    return m_lua && m_lua->call_lua_filter_matches(matches, rl_completion_type, rl_filename_completion_desired);
}

//------------------------------------------------------------------------------
bool host::call_lua_rl_global_function(const char* func_name, const line_state* line)
{
    return m_lua && m_lua->call_lua_rl_global_function(func_name, line);
}

//------------------------------------------------------------------------------
const char** host::copy_dir_history(int32* total)
{
    if (!s_dir_history.size())
        return nullptr;

    // Copy the directory list (just a shallow copy of the dir pointers).
    const char** history = (const char**)malloc(sizeof(*history) * s_dir_history.size());
    int32 i = 0;
    for (auto const& it : s_dir_history)
        history[i++] = it.get();

    *total = i;
    return history;
}

//------------------------------------------------------------------------------
void host::send_event(const char* event_name)
{
    if (m_lua)
        m_lua->send_event(event_name);
}

//------------------------------------------------------------------------------
void host::send_oncommand_event(line_state& line, const char* command, bool quoted, recognition recog, const char* file)
{
    if (m_lua)
        m_lua->send_oncommand_event(line, command, quoted, recog, file);
}

//------------------------------------------------------------------------------
void host::send_oninputlinechanged_event(const char* line)
{
    if (m_lua)
        m_lua->send_oninputlinechanged_event(line);
}

//------------------------------------------------------------------------------
bool host::has_event_handler(const char* event_name)
{
    if (!m_lua)
        return false;

    lua_state& lua = *m_lua;
    lua_State* state = lua.get_state();

    save_stack_top ss(state);

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_has_event_callbacks");
    lua_rawget(state, -2);

    lua_pushstring(state, event_name);

    if (lua.pcall(1, 1) != 0)
        return false;

    return lua_toboolean(state, -1) != false;
}

//------------------------------------------------------------------------------
std::unique_ptr<printer_context> host::make_printer_context()
{
    return std::make_unique<printer_context>(m_terminal.out, m_printer);
}

//------------------------------------------------------------------------------
bool host::edit_line(const char* prompt, const char* rprompt, str_base& out, bool edit)
{
    assert(!m_prompt); // Reentrancy not supported!

    const app_context* app = app_context::get();
    bool reset = app->update_env();

#ifdef DEBUG
    {
        str<> tmp;
        os::get_env("CLINK_VERBOSE_INPUT", tmp);
        set_verbose_input(atoi(tmp.c_str()));
    }
#endif

    path::refresh_pathext();

    os::cwd_restorer cwd;

    // Load Clink's settings.  The load function handles deferred load for
    // settings declared in scripts.
    str<288> settings_file;
    str<288> default_settings_file;
    str<288> state_dir;
    app->get_settings_path(settings_file);
    app->get_default_settings_file(default_settings_file);
    app->get_state_dir(state_dir);
    settings::load(settings_file.c_str(), default_settings_file.c_str());
    reset_keyseq_to_name_map();

    // Set up the string comparison mode.
    static_assert(str_compare_scope::exact == 0, "g_ignore_case values must match str_compare_scope values");
    static_assert(str_compare_scope::caseless == 1, "g_ignore_case values must match str_compare_scope values");
    static_assert(str_compare_scope::relaxed == 2, "g_ignore_case values must match str_compare_scope values");
    str_compare_scope compare(g_ignore_case.get(), g_fuzzy_accent.get());

    // Run clinkstart.cmd on inject, if present.
    static bool s_autostart = true;
    static std::unique_ptr<autostart_display> s_autostart_display;
    str_moveable autostart;
    bool interactive = ((!edit) || // Not-edit means show and return.
                        (m_queued_lines.size() == 0) ||
                        (m_queued_lines.size() == 1 &&
                         (m_queued_lines.front().m_line.length() == 0 ||
                          m_queued_lines.front().m_line.c_str()[m_queued_lines.front().m_line.length() - 1] != '\n')));
    if (interactive && s_autostart)
    {
        s_autostart = false;
        app->get_autostart_command(autostart);
        interactive = autostart.empty();
    }

    // Run " echo %ERRORLEVEL% >tmpfile 2>nul" before every interactive prompt.
    static bool s_inspect_errorlevel = true;
    bool inspect_errorlevel = false;
    if (g_get_errorlevel.get())
    {
        if (interactive)
        {
            str<> tmp_errfile;
            get_errorlevel_tmp_name(tmp_errfile, "txt");

            if (s_inspect_errorlevel)
            {
                // Make sure the errorlevel tmp file can be written.  If not
                // then skip interrogating it.  Otherwise a confusing error
                // message may appear.  For example if the profile directory
                // points at a file by mistake, or access is denied, or etc.
                {
                    wstr<> wtmp_errfile(tmp_errfile.c_str());
                    DWORD share_flags = FILE_SHARE_READ|FILE_SHARE_WRITE;
                    HANDLE errfile = CreateFileW(wtmp_errfile.c_str(), GENERIC_READ|GENERIC_WRITE,
                                                 share_flags, nullptr, CREATE_ALWAYS, 0, nullptr);
                    if (errfile == INVALID_HANDLE_VALUE)
                    {
                        ERR("Unable to create errorlevel tmp file '%s'", tmp_errfile.c_str());
                        goto skip_errorlevel;
                    }
                    CloseHandle(errfile);
                }

                inspect_errorlevel = true;
                interactive = false;
            }
            else
            {
#ifdef CAPTURE_PUSHD_STACK
                dbg_ignore_scope(snapshot, "pushd stack");
                std::vector<str_moveable> stack;
#endif

                int32 errorlevel = 0;

                errfile_reader reader;
                if (reader.open(tmp_errfile.c_str()))
                {
                    str<> s;
                    // First line is the exit code.
                    if (reader.next(s))
                        errorlevel = atoi(s.c_str());
#ifdef CAPTURE_PUSHD_STACK
                    // Subsequent lines are the directory stack entries.
                    while (reader.next(s) && !s.empty())
                    {
                        // This avoids std::move to reduce allocations and
                        // total footprint.
                        stack.emplace_back(s.c_str());
                    }
#endif
                }

                os::set_errorlevel(errorlevel);
#ifdef CAPTURE_PUSHD_STACK
                os::set_pushd_stack(stack);
#endif
            }
            s_inspect_errorlevel = !s_inspect_errorlevel;
        }
    }
    else
    {
skip_errorlevel:
        s_inspect_errorlevel = true;
    }

    // Improve performance while replaying doskey macros by not loading scripts
    // or history, since they aren't used.
    bool init_scripts = reset || interactive;
    bool send_event = interactive;
    bool init_prompt = interactive;
    bool init_editor = interactive;
    bool init_history = reset || (interactive && !rl_has_saved_history());

    // Update last cwd and whether transient prompt can be applied later.
    if (init_editor)
        update_last_cwd();

    // Set up Lua.
    bool local_lua = g_reload_scripts.get();
    bool reload_lua = local_lua || (m_lua && m_lua->is_script_path_changed());
    std::unique_ptr<host_lua> tmp_lua;
    std::unique_ptr<prompt_filter> tmp_prompt_filter;
    if (reload_lua || local_lua)
    {
        // Chicken and egg problem:
        //  1.  Must load settings to know whether to delete Lua.
        //  2.  Must delete Lua before loading settings so that the loaded map
        //      can contain deferred setting values for settings defined by Lua
        //      scripts.
        // Reloading settings again after deleting Lua resolves the problem.
        const bool reload_settings = !!m_lua;
        delete m_prompt_filter;
        delete m_suggester;
        delete m_lua;
        m_prompt_filter = nullptr;
        m_suggester = nullptr;
        m_lua = nullptr;
        if (reload_settings)
            settings::load(settings_file.c_str(), default_settings_file.c_str());
    }
    if (!local_lua && m_lua)
        init_scripts = false;
    {
        dbg_ignore_scope(snapshot, "Initialization overhead");
        if (!m_lua)
            m_lua = new host_lua;
        if (!m_prompt_filter)
            m_prompt_filter = new prompt_filter(*m_lua);
        if (!m_suggester)
            m_suggester = new suggester(*m_lua);
    }
    host_lua& lua = *m_lua;

    // Load scripts.
    if (init_scripts)
    {
        // Load inputrc before loading scripts.  Config settings in inputrc can
        // affect Lua scripts (e.g. completion-case-map affects '-' and '_' in
        // command names in argmatchers).
        str_moveable default_inputrc;
        app->get_default_init_file(default_inputrc);
        extern void initialise_readline(const char* shell_name, const char* state_dir, const char* default_inputrc, bool no_user=false);
        initialise_readline("clink", state_dir.c_str(), default_inputrc.c_str());
        initialise_lua(lua);
        lua.load_scripts();
    }

    // Detect light vs dark console theme.
    if (send_event)
    {
        extern void detect_console_theme();
        detect_console_theme();
    }

    // Send oninject event; one time only.
    static bool s_injected = false;
    if (send_event && !s_injected)
    {
        s_injected = true;
        lua.send_event("oninject");
    }

    // Send onbeginedit event.
    if (send_event)
    {
        adjust_prompt_spacing();

        lua.send_event("onbeginedit");

        // Terminal shell integration.  Happens after any Lua scripts so that
        // it corresponds most cleanly to end of command output.
        terminal_end_command();
    }

    // Send onprovideline event.
    bool skip_editor = false;
    if (send_event)
    {
        if (!m_skip_provide_line && !clink_is_signaled())
        {
            str<> tmp;
            lua.send_event_string_out("onprovideline", tmp);
            if (tmp.length())
            {
                LOG("ONPROVIDELINE: %s", tmp.c_str());
                out = tmp.c_str();
                init_editor = false;
                init_prompt = false;
                skip_editor = true;
            }
        }
        m_skip_provide_line = false;
    }

    // Reset input idle.  Must happen before filtering the prompt, so that the
    // wake event is available.
    if (init_editor || init_prompt)
        static_cast<input_idle*>(lua)->reset();

#ifdef USE_MEMORY_TRACKING
    if (init_editor)
    {
        static size_t s_since = 0;
        static size_t s_prev = 0;
        if (!s_since)
        {
            s_since = dbggetallocnumber();
            dbgignoresince(0, nullptr, "Initialization overhead", true/*all_threads*/);
        }
        else if (g_debug_heap_stats.get())
        {
            lua.force_gc(); // So Lua can release native refs.
            if (prompt) dbgmarkmem(prompt);
            if (rprompt) dbgmarkmem(rprompt);
            dbgsetreference(s_prev, " ---- Previous edit_line()");
            dbgchecksince(s_prev, true/*include_all*/);
        }
        s_prev = dbggetallocnumber();
    }
#endif

    // Initialize history before filtering the prompt, so that the Lua history
    // APIs can work.
    history_database* history = history_database::get();
    if (init_history)
    {
        if (history)
        {
            str<> history_path;
            app->get_history_path(history_path);
            if (g_save_history.get() != history->has_bank(bank_master) ||
                history->is_stale_name(history_path.c_str()))
            {
                delete history;
                history = nullptr;
            }
        }

        if (!history)
        {
            dbg_ignore_scope(snapshot, "History");
            str<> history_path;
            app->get_history_path(history_path);
            history = new history_database(history_path.c_str(), app->get_id(), g_save_history.get());
        }

        if (history)
        {
            history->initialise();
            history->load_rl_history();
        }
    }

    // Filter the prompt.  Unless processing a multiline doskey macro.
    line_editor::desc desc(m_terminal.in, m_terminal.out, m_printer, this);
    initialise_editor_desc(desc);
    if (init_prompt)
    {
        bool ok; // Not needed for the initial filter call.
        m_prompt = prompt ? prompt : "";
        m_rprompt = rprompt ? rprompt : "";
        desc.prompt = filter_prompt(&desc.rprompt, ok);
    }

    // Create the editor and add components to it.
    line_editor* editor = nullptr;
    if (init_editor)
    {
        editor = line_editor_create(desc);
        editor->set_generator(lua);
        if (g_classify_words.get())
            editor->set_classifier(lua);
        editor->set_input_idle(lua);
    }

    bool resolved = false;
    intercept_result intercepted = intercept_result::none;
    bool ret = false;
    while (1)
    {
        // Auto-run clinkstart.cmd the first time the edit prompt is invoked.
        if (autostart.length())
        {
            // Remember the original position to be able to restore the cursor
            // there if the autostart command doesn't output anything.
            s_autostart_display = std::make_unique<autostart_display>();
            s_autostart_display->save();

            m_terminal.out->begin();
            m_terminal.out->end();
            out = autostart.c_str();
            resolved = true;
            ret = true;
            break;
        }

        // Adjust the cursor position if possible, to make the initial prompt
        // appear on the same line it would have if no autostart script ran.
        if (s_autostart_display)
        {
            s_autostart_display->restore();
            s_autostart_display.reset();
        }

        // Before each interactive prompt, run an echo command to interrogate
        // CMD's internal %ERRORLEVEL% variable.
        if (inspect_errorlevel)
        {
            str<> tmp_errfile;
            get_errorlevel_tmp_name(tmp_errfile, "txt");

            dbg_snapshot_heap(snapshot);
            m_pending_command = out.c_str();
            m_bypass_dequeue = true;
            m_bypass_flags = dequeue_flags::show_line;
            m_bypass_flags |= (edit ? dequeue_flags::edit_line : dequeue_flags::none);
            dbg_ignore_since_snapshot(snapshot, "command queued by get errorlevel");

            m_terminal.out->begin();
            m_terminal.out->end();

#ifdef CAPTURE_PUSHD_STACK
            bool wrote = false;
            FILE* file = nullptr;
            str<> tmp_errscript;
            get_errorlevel_tmp_name(tmp_errscript, "bat");
            {
                wstr_moveable wacp_filename(tmp_errscript.c_str());
                str_moveable acp_filename;
                DWORD cch = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wacp_filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (cch && acp_filename.reserve(cch))
                {
                    cch = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wacp_filename.c_str(), -1, acp_filename.data(), cch, nullptr, nullptr);
                    if (cch)
                        file = fopen(tmp_errscript.c_str(), "w");
                }
            }
            if (file)
            {
                // What this script is doing:
                //  1.  "@echo off" prevents echoing the commands to stdout.
                //  2.  "setlocal" prevents clink_dummy_capture_env and
                //      clink_exit_code from staying in the environment.
                //  3.  "set clink_dummy_capture_env=" updates CMD's cached
                //      environment so that later on Readline can get
                //      up-to-date values for %LINES% and %COLUMNS%.
                //  4.  "echo %%errorlevel%%" prints the last exit code into
                //      the temporary file.
                //  5.  "set clink_exit_code=%%errorlevel%%" remembers the
                //      last exit code so it can be used in the "exit /b"
                //      call.
                //  6.  "pushd" prints the directory stack, if any, into the
                //      temporary file.
                //  7.  "endlocal" restores the environment.
                //  8.  "setlocal enableextensions" enables support for "exit
                //      /b" command, which internally invokes "goto :eof",
                //      which requires that command extensions are enabled.
                //  9.  "exit /b %%clink_exit_code%%" restores the last exit
                //      code, which got cleared by "pushd".
                // NOTE:  %errorlevel% will expand to 0 unless command
                // extensions were enabled at the time of the error.
                str<280> script;
                script.format("@echo off& setlocal& set clink_dummy_capture_env=& echo %%errorlevel%% 2>nul >\"%s\"& set clink_exit_code=%%errorlevel%%& pushd 2>nul >>\"%s\"&\n"
                              "endlocal& setlocal enableextensions& exit /b %%clink_exit_code%%", tmp_errfile.c_str(), tmp_errfile.c_str());
                wrote = (fputs(script.c_str(), file) != EOF);
                fclose(file);
                file = nullptr;
                out.format(" 2>nul \"%s\"", tmp_errscript.c_str());
            }
            if (!wrote)
#endif
            {
                // What this command line is doing:
                //  1.  " " (space) prevents doskey expansion and prevents the
                //      command from being saved in the history.
                //  2.  "set clink_dummy_capture_env=" updates CMD's cached
                //      environment so that later on Readline can get
                //      up-to-date values for %LINES% and %COLUMNS%.
                //  3.  "echo %%errorlevel%%" prints the last exit code into
                //      the temporary file.
                // NOTE:  %errorlevel% will expand to 0 unless command
                // extensions were enabled at the time of the error.
                out.format(" set clink_dummy_capture_env= & echo %%errorlevel%% 2>nul >\"%s\"", tmp_errfile.c_str());
            }
            resolved = true;
            ret = true;
            move_cursor_up_one_line();
            break;
        }
        if (m_pending_command.length())
        {
            out = m_pending_command.c_str();
            m_pending_command.free();
        }

        // Give the directory history queue a crack at the current directory.
        update_dir_history();

        // Call the editor to accept a line of input.
        ret = skip_editor || (editor && editor->edit(out, edit));
        if (!ret)
            break;

        // Determine whether to add the line to history.  Must happen before
        // calling expand() because that resets the history position.
        bool add_history = true;
        if (skip_editor || rl_has_saved_history())
        {
            // Don't add to history when the onprovideline event returned a
            // string.  Don't add to history when the operate-and-get-next
            // command was used, as that would rearrange history and interfere
            // with the command.
            add_history = false;
        }
        else if (!out.empty() && !can_sticky_search_add_history(out.c_str()))
        {
            // Don't add to history when stick search is active and the input
            // line matches the history entry corresponding to the sticky
            // search history position.
            add_history = false;
        }
        if (add_history)
            clear_sticky_search_position();

        // Handle history event expansion.  expand() is a static method,
        // so can call it even when m_history is nullptr.
        if (g_history_autoexpand.get() &&
            history->expand(out.c_str(), out) == history_db::expand_print)
        {
            puts(out.c_str());
            out.clear();
            if (!skip_editor)
            {
                end_prompt(true/*crlf*/);
                continue;
            }
        }

        // Should we skip adding certain commands?
        if (add_history &&
            g_exclude_from_history_cmds.get() &&
            *g_exclude_from_history_cmds.get())
        {
            const char* c = out.c_str();
            while (*c == ' ' || *c == '\t')
                ++c;

            str<> token;
            str_tokeniser tokens(g_exclude_from_history_cmds.get(), " ,;");
            while (tokens.next(token))
            {
                if (token.length() &&
                    _strnicmp(c, token.c_str(), token.length()) == 0 &&
                    !isalnum(uint8(c[token.length()])) &&
                    !path::is_separator(c[token.length()]))
                {
                    add_history = false;
                    break;
                }
            }
        }

        // Let Lua scripts suppress adding the line to history.
        if (add_history && history && send_event)
        {
            lua_state& state = lua;
            lua_pushlstring(state.get_state(), out.c_str(), out.length());
            add_history = !lua.send_event_cancelable("onhistory", 1);
        }

        // Add the line to the history.
        assert(history);
        if (add_history && history)
            history->add(out.c_str());
        break;
    }

    if (ret && !resolved)
    {
        // If the line is a "cd -" or "chdir -" command, rewrite the line to
        // invoke the CD command to change to the directory.
        intercepted = intercept_directory(out.c_str(), &out, true/*only_cd_chdir*/);
        if (intercepted != intercept_result::none)
        {
            if (intercepted == intercept_result::prev_dir)
                prev_dir_history(out);
            resolved = true; // Don't test for a doskey alias.
        }
    }

#ifdef DEBUG
    const bool was_signaled = clink_is_signaled();
#endif

    std::list<str_moveable> more_out;
    {
        if (send_event)
        {
            // Terminal shell integration.  Happens before any Lua scripts so
            // that it corresponds most cleanly to beginning of command output.
            terminal_begin_command();
        }

        // Temporarily install ctrlevent handler so Lua scripts have the option
        // to detect Ctrl-Break with os.issignaled() and respond accordingly.
        clink_install_ctrlevent();

        if (send_event)
        {
            lua_state& state = lua;
            lua_pushlstring(state.get_state(), out.c_str(), out.length());
            lua.send_event("onendedit", 1);
        }

        if (send_event)
            lua.send_event_cancelable_string_inout("onfilterinput", out.c_str(), out, &more_out);

        clink_shutdown_ctrlevent();
    }

    std::list<queued_line> queue;

    if (!resolved)
    {
        str_moveable next;
        doskey_alias alias;

        // Doskey is implemented on the server side of a ReadConsoleW() call
        // (i.e. in conhost.exe).  Commands separated by a "$T" are returned
        // one command at a time through successive calls to ReadConsoleW().
        {
            m_doskey.resolve(out.c_str(), alias);
            if (alias)
                alias.next(out); // First line goes into OUT to be returned.
            while (alias.next(next))
                queue.emplace_back(std::move(next), dequeue_flags::none);
        }

        for (auto& another : more_out)
        {
            m_doskey.resolve(another.c_str(), alias);
            if (!alias)
                queue.emplace_back(std::move(another), dequeue_flags::hide_prompt);
            while (alias.next(next))
                queue.emplace_back(std::move(next), dequeue_flags::hide_prompt);
        }
    }

    // If the line is a directory, rewrite the line to invoke the CD command to
    // change to the directory.
    if (ret && autostart.empty() && intercepted == intercept_result::none)
    {
        if (intercept_directory(out.c_str(), &out) == intercept_result::prev_dir)
            prev_dir_history(out);

        for (auto& queued : queue)
        {
            if (intercept_directory(queued.m_line.c_str(), &queued.m_line) == intercept_result::prev_dir)
                prev_dir_history(queued.m_line);
        }

    }

    // Insert the lines in reverse order at the front of the queue, to execute
    // them in the original order.
    for (std::list<queued_line>::reverse_iterator it = queue.rbegin(); it != queue.rend(); ++it)
        m_queued_lines.emplace_front(std::move(*it));

    line_editor_destroy(editor);

    if (local_lua)
    {
        delete m_prompt_filter;
        delete m_suggester;
        delete m_lua;
        m_prompt_filter = nullptr;
        m_suggester = nullptr;
        m_lua = nullptr;
    }

    m_prompt = nullptr;
    m_rprompt = nullptr;

#ifdef DEBUG
    if (!was_signaled && clink_is_signaled())
        g_suppress_signal_assert = true;
#endif

    return ret;
}

//------------------------------------------------------------------------------
bool g_filtering_in_progress = false;
const char* host::filter_prompt(const char** rprompt, bool& ok, bool transient, bool final)
{
    dbg_ignore_scope(snapshot, "Prompt filter");

    ok = true;

    if (!g_filtering_in_progress)
    {
        rollback<bool> rg_fip(g_filtering_in_progress, true);

        m_filtered_prompt.clear();
        m_filtered_rprompt.clear();
        if (g_filter_prompt.get() && m_prompt_filter)
        {
            str_moveable tmp;
            str_moveable rtmp;
            const char* p;
            const char* rp;
            if (transient)
            {
                prompt_utils::get_transient_prompt(tmp);
                prompt_utils::get_transient_rprompt(rtmp);
                p = tmp.c_str();
                rp = rtmp.c_str();
            }
            else
            {
                p = m_prompt ? m_prompt : "";
                rp = m_rprompt ? m_rprompt : "";
            }

            ok = m_prompt_filter->filter(p, rp,
                                         m_filtered_prompt, m_filtered_rprompt,
                                         transient, final);

            if (!transient && s_prompt_spacing.get() == sparse)
            {
                tmp = "\n";
                tmp.concat(m_filtered_prompt.c_str(), m_filtered_prompt.length());
                m_filtered_prompt = tmp.c_str();
            }

            if (transient && !ok)
            {
                ok = true;
                if (rprompt)
                    *rprompt = nullptr;
                return nullptr;
            }
        }
        else
        {
            m_filtered_prompt = m_prompt;
            m_filtered_rprompt = m_rprompt;
        }
    }

    if (rprompt)
        *rprompt = m_filtered_rprompt.length() ? m_filtered_rprompt.c_str() : nullptr;
    return m_filtered_prompt.c_str();
}

//------------------------------------------------------------------------------
void host::purge_old_files()
{
    str<> tmp;
    get_errorlevel_tmp_name(tmp, nullptr, true/*wild*/);

    // Purge orphaned clink_errorlevel temporary files older than 30 minutes.
    const int32 seconds = 30 * 60/*seconds per minute*/;

    globber i(tmp.c_str());
    i.older_than(seconds);
    while (i.next(tmp))
        _unlink(tmp.c_str());
}

//------------------------------------------------------------------------------
void host::update_last_cwd()
{
    int32 when = s_prompt_transient.get();

    str<> cwd;
    os::get_current_dir(cwd);

    dbg_ignore_scope(snapshot, "History");

    wstr_moveable wcwd(cwd.c_str());
    if (wcwd.iequals(m_last_cwd.c_str()))
    {
        m_can_transient = (when != 0);  // Same dir collapses if not 'off'.
    }
    else
    {
        m_can_transient = (when == 1);  // Otherwise only collapse if 'always'.
        m_last_cwd = std::move(wcwd);
    }
}
