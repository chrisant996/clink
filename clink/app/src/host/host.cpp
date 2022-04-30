// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "host_lua.h"
#include "version.h"

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
#include <lib/intercept.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lua/lua_match_generator.h>
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

static setting_enum s_prompt_transient(
    "prompt.transient",
    "Controls when past prompts are collapsed",
    "The default is 'off' which never collapses past prompts.  Set to 'always' to\n"
    "always collapse past prompts.  Set to 'same_dir' to only collapse past prompts\n"
    "when the current working directory hasn't changed since the last prompt.",
    "off,always,same_dir",
    0);

setting_bool g_autosuggest_async(
    "autosuggest.async",
    "Enable asynchronous suggestions",
    "The default is 'true'.  When this is 'true' matches are generated\n"
    "asynchronously for suggestions.  This helps to keep typing responsive.",
    true);

static setting_bool g_autosuggest_enable(
    "autosuggest.enable",
    "Enable automatic suggestions",
    "The default is 'false'.  When this is 'true' a suggested command may appear\n"
    "in the 'color.suggestion' color after the cursor.  If the suggestion isn't\n"
    "what you want, just ignore it.  Or accept the whole suggestion with the Right\n"
    "arrow or End key, accept the next word of the suggestion with Ctrl+Right, or\n"
    "accept the next full word of the suggestion up to a space with Shift+Right.\n"
    "The 'autosuggest.strategy' setting determines how a suggestion is chosen.",
    false);

static setting_str g_autosuggest_strategy(
    "autosuggest.strategy",
    "Controls how suggestions are chosen",
    "This determines how suggestions are chosen.  The suggestion generators are\n"
    "tried in the order listed, until one provides a suggestion.  There are three\n"
    "built-in suggestion generators, and scripts can provide new ones.\n"
    "'history' chooses the most recent matching command from the history.\n"
    "'completion' chooses the first of the matching completions.\n"
    "'match_prev_cmd' chooses the most recent matching command whose preceding\n"
    "history entry matches the most recently invoked command, but only when\n"
    "the 'history.dupe_mode' setting is 'add'.",
    "match_prev_cmd history completion");

setting_bool g_save_history(
    "history.save",
    "Save history between sessions",
    "Changing this setting only takes effect for new instances.",
    true);

static setting_str g_exclude_from_history_cmds(
    "history.dont_add_to_history_cmds",
    "Commands not automatically added to the history",
    "List of commands that aren't automatically added to the history.\n"
    "Commands are separated by spaces, commas, or semicolons.  Default is\n"
    "\"exit history\", to exclude both of those commands.",
    "exit history");

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

#ifdef DEBUG
static setting_bool g_debug_heap_stats(
    "debug.heap_stats",
    "Report heap stats for each edit line",
    "At the beginning of each edit line, report the heap stats since the previous\n"
    "edit line.",
    false);
#endif

extern setting_bool g_classify_words;
extern setting_color g_color_prompt;
extern setting_bool g_prompt_async;

extern void start_logger();

extern void initialise_readline(const char* shell_name, const char* state_dir, const char* bin_dir);
extern bool clink_maybe_handle_signal();
extern bool get_sticky_search_history();
extern bool has_sticky_search_position();
extern bool get_sticky_search_add_history(const char* line);
extern void clear_sticky_search_position();
extern void reset_keyseq_to_name_map();
extern void set_prompt(const char* prompt, const char* rprompt, bool redisplay);
extern bool can_suggest(const line_state& line);

#ifdef DEBUG
extern bool g_suppress_signal_assert;
extern int clink_is_signaled();
#endif



//------------------------------------------------------------------------------
static void get_errorlevel_tmp_name(str_base& out, bool wild=false)
{
    app_context::get()->get_log_path(out);
    path::to_parent(out, nullptr);
    path::append(out, "clink_errorlevel");

    if (wild)
    {
        // "clink_errorlevel*.txt" catches the obsolete clink_errorlevel.txt
        // file as well.
        out << "*.txt";
    }
    else
    {
        str<> name;
        name.format("_%X.txt", GetCurrentProcessId());
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
const int c_max_dir_history = 100;
static std::list<dir_history_entry> s_dir_history;

//------------------------------------------------------------------------------
static void update_dir_history()
{
    str<> cwd;
    os::get_current_dir(cwd);

    // Add cwd to tail.
    if (!s_dir_history.size() || _stricmp(s_dir_history.back().get(), cwd.c_str()) != 0)
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

    inout.format(" cd /d \"%s\"", a->get());
}



//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(handle, L"\n", 1, &written, nullptr);
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
}

//------------------------------------------------------------------------------
host::~host()
{
    purge_old_files();

    delete m_prompt_filter;
    delete m_suggester;
    delete m_lua;
    delete m_history;
    delete m_printer;
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
}

//------------------------------------------------------------------------------
void host::pop_queued_line()
{
    m_queued_lines.pop_front();
    m_char_cursor = 0;
}

//------------------------------------------------------------------------------
int host::add_history(const char* line)
{
    return !!m_history->add(line);
}

//------------------------------------------------------------------------------
int host::remove_history(int rl_history_index, const char* line)
{
    return !!m_history->remove(rl_history_index, line);
}

//------------------------------------------------------------------------------
void host::filter_prompt()
{
    if (!g_prompt_async.get())
        return;

    dbg_ignore_scope(snapshot, "Prompt filter");

    const char* rprompt = nullptr;
    const char* prompt = filter_prompt(&rprompt, false/*transient*/);
    set_prompt(prompt, rprompt, true/*redisplay*/);
}

//------------------------------------------------------------------------------
void host::filter_transient_prompt(bool final)
{
    if (!m_can_transient)
    {
        update_last_cwd();
        return;
    }

    const char* rprompt;
    const char* prompt;

    // Replace old prompt with transient prompt.
    rprompt = nullptr;
    prompt = filter_prompt(&rprompt, true/*transient*/, final);
    {
        // Make sure no mode strings in the transient prompt.
        rollback<char*> ems(_rl_emacs_mode_str, const_cast<char*>(""));
        rollback<char*> vims(_rl_vi_ins_mode_str, const_cast<char*>(""));
        rollback<char*> vcms(_rl_vi_cmd_mode_str, const_cast<char*>(""));
        rollback<int> eml(_rl_emacs_modestr_len, 0);
        rollback<int> viml(_rl_vi_ins_modestr_len, 0);
        rollback<int> vcml(_rl_vi_cmd_modestr_len, 0);
        rollback<int> mml(_rl_mark_modified_lines, 0);

        set_prompt(prompt, rprompt, true/*redisplay*/);
    }

    if (final)
        return;

    // Refilter new prompt, but don't redisplay (which would replace the prompt
    // again; not what is needed here).  Instead let the prompt get displayed
    // again naturally in due time.
    rprompt = nullptr;
    prompt = filter_prompt(&rprompt, false/*transient*/);
    set_prompt(prompt, rprompt, false/*redisplay*/);
}

//------------------------------------------------------------------------------
bool host::can_suggest(const line_state& line)
{
    return (m_suggester &&
            g_autosuggest_enable.get() &&
            ::can_suggest(line));
}

//------------------------------------------------------------------------------
bool host::suggest(const line_states& lines, matches* matches, int generation_id)
{
    if (m_suggester && g_autosuggest_enable.get())
        return m_suggester->suggest(lines, matches, generation_id);

    return false;
}

//------------------------------------------------------------------------------
void host::filter_matches(char** matches)
{
    if (m_lua)
        m_lua->call_lua_filter_matches(matches, rl_completion_type, rl_filename_completion_desired);
}

//------------------------------------------------------------------------------
bool host::call_lua_rl_global_function(const char* func_name, line_state* line)
{
    return m_lua && m_lua->call_lua_rl_global_function(func_name, line);
}

//------------------------------------------------------------------------------
const char** host::copy_dir_history(int* total)
{
    if (!s_dir_history.size())
        return nullptr;

    // Copy the directory list (just a shallow copy of the dir pointers).
    const char** history = (const char**)malloc(sizeof(*history) * s_dir_history.size());
    int i = 0;
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
void host::get_app_context(int& id, str_base& binaries, str_base& profile, str_base& scripts)
{
    const auto* context = app_context::get();

    id = context->get_id();
    context->get_binaries_dir(binaries);
    context->get_state_dir(profile);
    context->get_script_path_readable(scripts);
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
            get_errorlevel_tmp_name(tmp_errfile);

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
                FILE* f = fopen(tmp_errfile.c_str(), "r");
                if (f)
                {
                    char buffer[32];
                    memset(buffer, sizeof(buffer), 0);
                    fgets(buffer, _countof(buffer) - 1, f);
                    fclose(f);
                    _unlink(tmp_errfile.c_str());
                    os::set_errorlevel(atoi(buffer));
                }
                else
                {
                    os::set_errorlevel(0);
                }
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
    if (!local_lua)
        init_scripts = !m_lua;
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
        str_moveable bin_dir;
        app->get_binaries_dir(bin_dir);
        initialise_readline("clink", state_dir.c_str(), bin_dir.c_str());
        initialise_lua(lua);
        lua.load_scripts();
    }

    // Send oninject event; one time only.
    static bool s_injected = false;
    if (!s_injected)
    {
        s_injected = true;
        lua.send_event("oninject");
    }

    // Send onbeginedit event.
    if (send_event)
        lua.send_event("onbeginedit");

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

    line_editor::desc desc(m_terminal.in, m_terminal.out, m_printer, this);
    initialise_editor_desc(desc);

    // Filter the prompt.  Unless processing a multiline doskey macro.
    if (init_prompt)
    {
        m_prompt = prompt ? prompt : "";
        m_rprompt = rprompt ? rprompt : "";
        desc.prompt = filter_prompt(&desc.rprompt);
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

    if (init_history)
    {
        if (m_history &&
            ((g_save_history.get() != m_history->has_bank(bank_master)) ||
             m_history->is_stale_name()))
        {
            delete m_history;
            m_history = 0;
        }

        if (!m_history)
        {
            dbg_ignore_scope(snapshot, "History");
            m_history = new history_db(g_save_history.get());
        }

        if (m_history)
        {
            m_history->initialise();
            m_history->load_rl_history();
        }
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
            get_errorlevel_tmp_name(tmp_errfile);

            dbg_snapshot_heap(snapshot);
            m_pending_command = out.c_str();
            m_bypass_dequeue = true;
            m_bypass_flags = dequeue_flags::show_line;
            m_bypass_flags |= (edit ? dequeue_flags::edit_line : dequeue_flags::none);
            dbg_ignore_since_snapshot(snapshot, "command queued by get errorlevel");

            m_terminal.out->begin();
            m_terminal.out->end();
            out.format(" set clink_dummy_capture_env= & echo %%errorlevel%% 2>nul >\"%s\"", tmp_errfile.c_str());
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

        resolved = false;
        ret = editor && editor->edit(out, edit);
        if (!ret)
            break;

        // Determine whether to add the line to history.  Must happen before
        // calling expand() because that resets the history position.
        bool add_history = true;
        if (rl_has_saved_history())
        {
            // Don't add to history when operate-and-get-next was used, as
            // that would defeat the command.
            add_history = false;
        }
        else if (!out.empty() && get_sticky_search_history() && has_sticky_search_position())
        {
            // Query whether the sticky search position should be added
            // (i.e. the input line matches the history entry corresponding
            // to the sticky search history position).
            add_history = get_sticky_search_add_history(out.c_str());
        }
        if (add_history)
            clear_sticky_search_position();

        // Handle history event expansion.  expand() is a static method,
        // so can call it even when m_history is nullptr.
        if (m_history->expand(out.c_str(), out) == history_db::expand_print)
        {
            puts(out.c_str());
            out.clear();
            end_prompt(true/*crlf*/);
            continue;
        }

        // Should we skip adding certain commands?
        if (g_exclude_from_history_cmds.get() &&
            *g_exclude_from_history_cmds.get())
        {
            const char* c = out.c_str();
            while (*c == ' ' || *c == '\t')
                ++c;

            bool exclude = false;
            str<> token;
            str_tokeniser tokens(g_exclude_from_history_cmds.get(), " ,;");
            while (tokens.next(token))
            {
                if (token.length() &&
                    _strnicmp(c, token.c_str(), token.length()) == 0 &&
                    !isalnum((unsigned char)c[token.length()]) &&
                    !path::is_separator(c[token.length()]))
                {
                    exclude = true;
                    break;
                }
            }

            if (exclude)
                break;
        }

        // Add the line to the history.
        if (add_history)
            m_history->add(out.c_str());

        if (ret)
        {
            // If the line is a directory, rewrite the line to invoke the CD
            // command to change to the directory.
            intercepted = intercept_directory(out.c_str(), &out, true/*only_cd_chdir*/);
            if (intercepted != intercept_result::none)
            {
                if (intercepted == intercept_result::prev_dir)
                    prev_dir_history(out);
                resolved = true; // Don't test for a doskey alias.
            }
        }
        break;
    }

#ifdef DEBUG
    const bool was_signaled = clink_is_signaled();
#endif

    if (send_event)
    {
        lua_state& state = lua;
        lua_pushlstring(state.get_state(), out.c_str(), out.length());
        lua.send_event("onendedit", 1);
    }

    std::list<str_moveable> more_out;
    if (send_event)
        lua.send_event_cancelable_string_inout("onfilterinput", out.c_str(), out, &more_out);

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
const char* host::filter_prompt(const char** rprompt, bool transient, bool final)
{
    dbg_ignore_scope(snapshot, "Prompt filter");

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
        m_prompt_filter->filter(p,
                                rp,
                                m_filtered_prompt,
                                m_filtered_rprompt,
                                transient,
                                final);
    }
    else
    {
        m_filtered_prompt = m_prompt;
        m_filtered_rprompt = m_rprompt;
    }
    if (rprompt)
        *rprompt = m_filtered_rprompt.length() ? m_filtered_rprompt.c_str() : nullptr;
    return m_filtered_prompt.c_str();
}

//------------------------------------------------------------------------------
void host::purge_old_files()
{
    str<> tmp;
    get_errorlevel_tmp_name(tmp, true/*wild*/);

    // Purge orphaned clink_errorlevel temporary files older than 30 minutes.
    const int seconds = 30 * 60/*seconds per minute*/;

    globber i(tmp.c_str());
    i.older_than(seconds);
    while (i.next(tmp))
        _unlink(tmp.c_str());
}

//------------------------------------------------------------------------------
void host::update_last_cwd()
{
    int when = s_prompt_transient.get();

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
