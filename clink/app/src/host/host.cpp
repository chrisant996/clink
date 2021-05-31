// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host.h"
#include "host_lua.h"
#include "prompt.h"
#include "doskey.h"
#include "terminal/terminal.h"
#include "terminal/terminal_out.h"
#include "terminal/printer.h"
#include "utils/app_context.h"

#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <lib/match_generator.h>
#include <lib/line_editor.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lua/lua_match_generator.h>
#include <terminal/terminal.h>

#include <list>
#include <memory>

extern "C" {
#include <lua.h>
#include <readline/readline.h>
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_enum g_ignore_case(
    "match.ignore_case",
    "Case insensitive matching",
    "Toggles whether case is ignored when selecting matches. The 'relaxed'\n"
    "option will also consider -/_ as equal.",
    "off,on,relaxed",
    2);

static setting_bool g_fuzzy_accent(
    "match.ignore_accent",
    "Accent insensitive matching",
    "Toggles whether accents on characters are ignored when selecting matches.",
    true);

static setting_bool g_filter_prompt(
    "clink.promptfilter",
    "Enable prompt filtering by Lua scripts",
    true);

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

extern setting_bool g_classify_words;
extern setting_color g_color_prompt;

extern void start_logger();

extern bool get_sticky_search_history();
extern bool has_sticky_search_position();
extern bool get_sticky_search_add_history(const char* line);
extern void clear_sticky_search_position();
extern void reset_keyseq_to_name_map();



//------------------------------------------------------------------------------
static printer* s_printer = nullptr;
static host_lua* s_host_lua = nullptr;
extern str<> g_last_prompt;



//------------------------------------------------------------------------------
// Documented in clink_api.cpp.
int clink_print(lua_State* state)
{
    // Check we've got one argument...
    if (lua_gettop(state) != 1)
        return 0;

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
        return 0;

    const char* string = lua_tostring(state, 1);

    if (s_printer)
    {
        str<> s;
        int len = int(strlen(string));
        s.reserve(len + 2);
        s.concat(string, len);
        s.concat("\r\n");
        s_printer->print(s.c_str(), s.length());
    }
    else
    {
        printf("%s\r\n", string);
    }

    return 0;
}



//------------------------------------------------------------------------------
bool call_lua_rl_global_function(const char* func_name)
{
    return s_host_lua && s_host_lua->call_lua_rl_global_function(func_name);
}

//------------------------------------------------------------------------------
int macro_hook_func(const char* macro)
{
    bool is_luafunc = (macro && strnicmp(macro, "luafunc:", 8) == 0);

    if (is_luafunc)
    {
        str<> func_name;
        func_name = macro + 8;
        func_name.trim();

        // TODO: Ideally optimize this so that it only resets match generation if
        // the Lua function triggers completion.
        extern void reset_generate_matches();
        reset_generate_matches();

        HANDLE std_handles[2] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE) };
        DWORD prev_mode[2];
        static_assert(_countof(std_handles) == _countof(prev_mode), "array sizes much match");
        for (size_t i = 0; i < _countof(std_handles); ++i)
            GetConsoleMode(std_handles[i], &prev_mode[i]);

        if (!call_lua_rl_global_function(func_name.c_str()))
            rl_ding();

        for (size_t i = 0; i < _countof(std_handles); ++i)
            SetConsoleMode(std_handles[i], prev_mode[i]);
    }

    extern void cua_after_command(bool force_clear);
    cua_after_command(true/*force_clear*/);

    return is_luafunc;
}

//------------------------------------------------------------------------------
int filter_matches(char** matches)
{
    if (s_host_lua)
        s_host_lua->call_lua_filter_matches(matches, rl_completion_type, rl_filename_completion_desired);
    return 0;
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
        s_dir_history.push_back(cwd.c_str());

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
const char** host_copy_dir_history(int* total)
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
static history_db* s_history_db = nullptr;
void host_add_history(int, const char* line)
{
    if (s_history_db)
        s_history_db->add(line);
}
void host_remove_history(int rl_history_index, const char* line)
{
    if (s_history_db)
        s_history_db->remove(rl_history_index, line);
}



//------------------------------------------------------------------------------
static host* s_host = nullptr;
void host_filter_prompt()
{
    if (!s_host)
        return;

    const char* prompt = s_host->filter_prompt();

    void set_prompt(const char* prompt);
    set_prompt(prompt);
}



//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
static bool parse_line_token(str_base& out, const char* line)
{
    out.clear();

    // Skip leading whitespace.
    while (*line == ' ' || *line == '\t')
        line++;

    // Parse the line text.
    bool first_component = true;
    for (bool quoted = false; true; line++)
    {
        // Spaces are acceptable when quoted, otherwise it's the end of the
        // token and any subsequent text defeats the directory shortcut feature.
        if (*line == ' ' || *line == '\t')
        {
            if (!quoted)
            {
                // Skip trailing whitespace.
                while (*line == ' ' || *line == '\t')
                    line++;
                // Parse fails if input is more than a single token.
                if (*line)
                    return false;
            }
        }

        // Parse succeeds if input is one token.
        if (!*line)
            return out.length();

        switch (*line)
        {
            // These characters defeat the directory shortcut feature.
        case '^':
        case '<':
        case '|':
        case '>':
        case '%':
            return false;

            // These characters are acceptable when quoted.
        case '@':
        case '(':
        case ')':
        case '&':
        case '+':
        case '=':
        case ';':
        case ',':
            if (!quoted)
                return false;
            break;

            // Quotes toggle quote mode.
        case '"':
            first_component = false;
            quoted = !quoted;
            continue;

            // These characters end a component.
        case '.':
        case '/':
        case '\\':
            if (first_component)
            {
                // Some commands are special and defeat the directory shortcut
                // feature even if they're legitimately part of an actual path,
                // unless they are quoted.
                static const char* const c_commands[] = { "call", "cd", "chdir", "dir", "echo", "md", "mkdir", "popd", "pushd" };
                for (const char* name : c_commands)
                    if (out.iequals(name))
                        return false;
                first_component = false;
            }
            break;
        }

        out.concat(line, 1);
    }
}

//------------------------------------------------------------------------------
static bool intercept_directory(str_base& inout)
{
    const char* line = inout.c_str();

    // Check for '-' (etc) to change to previous directory.
    if (strcmp(line, "-") == 0 ||
        _strcmpi(line, "cd -") == 0 ||
        _strcmpi(line, "chdir -") == 0)
    {
        prev_dir_history(inout);
        return true;
    }

    // Parse the input for a single token.
    str<> tmp;
    if (!parse_line_token(tmp, line))
        return false;

    // If all dots, convert into valid path syntax moving N-1 levels.
    // Examples:
    //  - "..." becomes "..\..\"
    //  - "...." becomes "..\..\..\"
    int num_dots = 0;
    for (const char* p = tmp.c_str(); *p; ++p, ++num_dots)
    {
        if (*p != '.')
        {
            if (!path::is_separator(p[0]) || p[1]) // Allow "...\"
                num_dots = -1;
            break;
        }
    }
    if (num_dots >= 2)
    {
        tmp.clear();
        while (num_dots > 1)
        {
            tmp.concat("..\\");
            --num_dots;
        }
    }

    // If the input doesn't end with a separator, don't handle it.  Otherwise
    // it would interfere with launching something found on the PATH but with
    // the same name as a subdirectory of the current working directory.
    if (!path::is_separator(tmp.c_str()[tmp.length() - 1]))
    {
        // But allow a special case for "..\.." and "..\..\..", etc.
        const char* p = tmp.c_str();
        while (true)
        {
            if (p[0] != '.' || p[1] != '.')
                return false;
            if (p[2] == '\0')
            {
                tmp.concat("\\");
                break;
            }
            if (!path::is_separator(p[2]))
                return false;
            p += 3;
        }
    }

    // Tilde expansion.
    if (tmp.first_of('~') >= 0)
    {
        char* expanded_path = tilde_expand(tmp.c_str());
        if (expanded_path)
        {
            if (!tmp.equals(expanded_path))
                tmp = expanded_path;
            free(expanded_path);
        }
    }

    if (os::get_path_type(tmp.c_str()) != os::path_type_dir)
        return false;

    // Normalize to system path separator, since `cd /d "/foo/"` fails because
    // the `/d` flag disables `cd` accepting forward slashes in paths.
    path::normalise_separators(tmp);

    inout.format(" cd /d \"%s\"", tmp.c_str());
    return true;
}



//------------------------------------------------------------------------------
struct cwd_restorer
{
    cwd_restorer() { os::get_current_dir(m_path); }
    ~cwd_restorer() { os::set_current_dir(m_path.c_str()); }
    str<288> m_path;
};

//------------------------------------------------------------------------------
class printer_context
{
public:
    printer_context(terminal& terminal, printer* printer)
    : m_terminal(terminal)
    , m_rb_printer(s_printer, printer)
    {
        m_terminal.out->open();
        m_terminal.out->begin();
        s_printer = printer;
    }

    ~printer_context()
    {
        m_terminal.out->end();
        m_terminal.out->close();
    }

private:
    const terminal& m_terminal;
    rollback<printer*> m_rb_printer;
};



//------------------------------------------------------------------------------
host::host(const char* name)
: m_name(name)
, m_doskey("cmd.exe")
{
    m_terminal = terminal_create();
    m_printer = new printer(*m_terminal.out);
}

//------------------------------------------------------------------------------
host::~host()
{
    delete m_prompt_filter;
    delete m_lua;
    delete m_history;
    delete m_printer;
    terminal_destroy(m_terminal);
}

//------------------------------------------------------------------------------
void host::enqueue_lines(std::list<str_moveable>& lines)
{
    for (auto& line : lines)
        m_queued_lines.emplace_back(std::move(line));
}

//------------------------------------------------------------------------------
bool host::edit_line(const char* prompt, str_base& out)
{
    assert(!m_prompt); // Reentrancy not supported!

    const app_context* app = app_context::get();
    bool reset = app->update_env();

    path::refresh_pathext();

    cwd_restorer cwd;
    printer_context prt(m_terminal, m_printer);

    // Load Clink's settings.  The load function handles deferred load for
    // settings declared in scripts.
    str<288> settings_file;
    str<288> state_dir;
    app->get_settings_path(settings_file);
    app->get_state_dir(state_dir);
    settings::load(settings_file.c_str());
    reset_keyseq_to_name_map();

    // Set up the string comparison mode.
    static_assert(str_compare_scope::exact == 0, "g_ignore_case values must match str_compare_scope values");
    static_assert(str_compare_scope::caseless == 1, "g_ignore_case values must match str_compare_scope values");
    static_assert(str_compare_scope::relaxed == 2, "g_ignore_case values must match str_compare_scope values");
    str_compare_scope compare(g_ignore_case.get(), g_fuzzy_accent.get());

    // Improve performance while replaying doskey macros by not loading scripts
    // or history, since they aren't used.
    bool interactive = !m_doskey_alias && ((m_queued_lines.size() == 0) ||
                                           (m_queued_lines.size() == 1 &&
                                            (m_queued_lines.front().length() == 0 ||
                                             m_queued_lines.front().c_str()[m_queued_lines.front().length() - 1] != '\n')));
    bool init_scripts = reset || interactive;
    bool send_event = interactive;
    bool init_prompt = interactive;
    bool init_editor = interactive;
    bool init_history = reset || (interactive && !rl_has_saved_history());

    // Set up Lua.
    bool local_lua = g_reload_scripts.get();
    bool reload_lua = local_lua || (m_lua && m_lua->is_script_path_changed());
    std::unique_ptr<host_lua> tmp_lua;
    std::unique_ptr<prompt_filter> tmp_prompt_filter;
    if (reload_lua || local_lua)
    {
        delete m_prompt_filter;
        delete m_lua;
        m_prompt_filter = nullptr;
        m_lua = nullptr;
    }
    if (!local_lua)
    {
        init_scripts = !m_lua;
        send_event |= init_scripts;
    }
    if (!m_lua)
        m_lua = new host_lua;
    if (!m_prompt_filter)
        m_prompt_filter = new prompt_filter(*m_lua);
    host_lua& lua = *m_lua;
    prompt_filter& prompt_filter = *m_prompt_filter;

    rollback<host*> rb_host(s_host, this);
    rollback<host_lua*> rb_lua(s_host_lua, &lua);

    // Load scripts.
    if (init_scripts)
    {
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

    line_editor::desc desc(m_terminal.in, m_terminal.out, m_printer);
    initialise_editor_desc(desc);
    desc.state_dir = state_dir.c_str();

    // Filter the prompt.  Unless processing a multiline doskey macro.
    str<256> filtered_prompt;
    if (init_prompt)
    {
        m_prompt = prompt;
        desc.prompt = filter_prompt();
    }

    // Create the editor and add components to it.
    line_editor* editor = line_editor_create(desc);

    if (init_editor)
    {
        editor->add_generator(lua);
        editor->add_generator(file_match_generator());
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
            m_history = new history_db(g_save_history.get());

        if (m_history)
        {
            m_history->initialise();
            m_history->load_rl_history();
        }
    }

    s_history_db = m_history;

    bool resolved = false;
    bool ret = false;
    while (1)
    {
        // Give the directory history queue a crack at the current directory.
        update_dir_history();

        // Doskey is implemented on the server side of a ReadConsoleW() call
        // (i.e. in conhost.exe). Commands separated by a "$T" are returned one
        // command at a time through successive calls to ReadConsoleW().
        resolved = false;
        if (m_doskey_alias.next(out))
        {
            m_terminal.out->begin();
            m_terminal.out->end();
            resolved = true;
            ret = true;
        }
        else
        {
            bool edit = true;
            if (!m_queued_lines.empty())
            {
                out.concat(m_queued_lines.front().c_str());
                m_queued_lines.pop_front();

                unsigned int len = out.length();
                while (len && out.c_str()[len - 1] == '\n')
                {
                    out.truncate(--len);
                    edit = false;
                }
            }

            if (!edit)
            {
                char const* read = g_last_prompt.c_str();
                char* write = g_last_prompt.data();
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

                m_printer->print(g_last_prompt.c_str(), g_last_prompt.length());
                m_printer->print(out.c_str(), out.length());
                m_printer->print("\n");
                ret = true;
            }
            else
            {
                ret = editor->edit(out);
                if (!ret)
                    break;
            }

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
        }
        break;
    }

    if (!resolved && send_event)
        lua.send_event_cancelable_string_inout("onendedit", out.c_str(), out);

    if (!resolved)
    {
        m_doskey.resolve(out.c_str(), m_doskey_alias);
        if (m_doskey_alias)
            out.clear();
    }

    if (ret)
    {
        // If the line is a directory, rewrite the line to invoke the CD command
        // to change to the directory.
        intercept_directory(out);
    }

    s_history_db = nullptr;

    line_editor_destroy(editor);

    if (local_lua)
    {
        delete m_prompt_filter;
        delete m_lua;
        m_prompt_filter = nullptr;
        m_lua = nullptr;
    }

    m_prompt = nullptr;

    return ret;
}

//------------------------------------------------------------------------------
const char* host::filter_prompt()
{
    m_filtered_prompt.clear();
    if (g_filter_prompt.get() && m_prompt_filter && m_prompt)
        m_prompt_filter->filter(m_prompt, m_filtered_prompt);
    return m_filtered_prompt.c_str();
}
