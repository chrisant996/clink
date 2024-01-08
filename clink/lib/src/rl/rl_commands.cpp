// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "word_collector.h"
#include "popup.h"
#include "editor_module.h"
#include "rl_commands.h"
#include "doskey.h"
#include "textlist_impl.h"
#include "history_db.h"
#include "ellipsify.h"
#include "host_callbacks.h"
#include "display_readline.h"
#include "recognizer.h"
#include "wakeup_chars.h"
#include "clink_rl_signal.h"
#include "rl_integration.h"
#include "line_editor_integration.h"

#include "rl_suggestions.h"

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/debugheap.h>
#include <terminal/wcwidth.h>
#include <terminal/printer.h>
#include <terminal/scroll.h>
#include <terminal/screen_buffer.h>
#include <terminal/terminal_helpers.h>
#include <terminal/ecma48_iter.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
#include <readline/history.h>
extern int32 find_streqn (const char *a, const char *b, int32 n);
extern void rl_replace_from_history(HIST_ENTRY *entry, int flags);
}

#include <list>
#include <unordered_set>
#include <signal.h>

#include "../../../clink/app/src/version.h" // Ugh.

extern "C" const int32 c_clink_version = CLINK_VERSION_ENCODED;



//------------------------------------------------------------------------------
// Internal ConHost system menu command IDs.
#define ID_CONSOLE_COPY         0xFFF0
#define ID_CONSOLE_PASTE        0xFFF1
#define ID_CONSOLE_MARK         0xFFF2
#define ID_CONSOLE_SCROLL       0xFFF3
#define ID_CONSOLE_FIND         0xFFF4
#define ID_CONSOLE_SELECTALL    0xFFF5
#define ID_CONSOLE_EDIT         0xFFF6
#define ID_CONSOLE_CONTROL      0xFFF7
#define ID_CONSOLE_DEFAULTS     0xFFF8



//------------------------------------------------------------------------------
enum { paste_crlf_delete, paste_crlf_space, paste_crlf_ampersand, paste_crlf_crlf };
static setting_enum g_paste_crlf(
    "clink.paste_crlf",
    "Strips CR and LF chars on paste",
    "Setting this to 'space' makes Clink strip CR and LF characters from text\n"
    "pasted into the current line.  Set this to 'delete' to strip all newline\n"
    "characters to replace them with a space.  Set this to 'ampersand' to replace\n"
    "all newline characters with an ampersand.  Or set this to 'crlf' to paste all\n"
    "newline characters as-is (executing commands that end with newline).",
    "delete,space,ampersand,crlf",
    paste_crlf_crlf);

extern setting_bool g_adjust_cursor_style;
extern setting_bool g_match_wild;
extern setting_bool g_autosuggest_enable;



//------------------------------------------------------------------------------
extern line_buffer* g_rl_buffer;
extern word_collector* g_word_collector;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
bool expand_history(const char* in, str_base& out)
{
    return history_db::expand(in, out) >= history_db::expand_result::expand_ok;
}

//------------------------------------------------------------------------------
static void strip_crlf(char* line, std::list<str_moveable>& overflow, int32 setting, bool* _done)
{
    bool has_overflow = false;
    int32 prev_was_crlf = 0;
    char* write = line;
    const char* read = line;
    bool done = false;
    while (*read)
    {
        char c = *read;
        if (c != '\n' && c != '\r')
        {
            prev_was_crlf = 0;
            *write = c;
            ++write;
        }
        else if (!prev_was_crlf)
        {
            switch (setting)
            {
            default:
                assert(false);
                // fall through
            case paste_crlf_delete:
                break;
            case paste_crlf_space:
                prev_was_crlf = 1;
                *write = ' ';
                ++write;
                break;
            case paste_crlf_ampersand:
                prev_was_crlf = 1;
                *write = '&';
                ++write;
                break;
            case paste_crlf_crlf:
                has_overflow = true;
                if (c == '\n')
                {
                    *write = '\n';
                    ++write;
                }
                break;
            }
        }

        ++read;
    }

    *write = '\0';

    if (has_overflow)
    {
        bool first = true;
        char* start = line;
        while (*start)
        {
            char* end = start;
            while (*end)
            {
                char c = *end;
                ++end;
                if (c == '\n')
                {
                    done = true;
                    if (first)
                        *(end - 1) = '\0';
                    break;
                }
            }

            if (first)
            {
                first = false;
            }
            else
            {
                uint32 len = (uint32)(end - start);
                overflow.emplace_back();
                str_moveable& back = overflow.back();
                back.reserve(len);
                back.concat(start, len);
            }

            start = end;
        }
    }

    if (_done)
        *_done = done;
}

//------------------------------------------------------------------------------
static void get_word_bounds(const line_buffer& buffer, int32* left, int32* right)
{
    const char* str = buffer.get_buffer();
    uint32 cursor = buffer.get_cursor();

    // Determine the word delimiter depending on whether the word's quoted.
    int32 delim = 0;
    for (uint32 i = 0; i < cursor; ++i)
    {
        char c = str[i];
        delim += (c == '\"');
    }

    // Search outwards from the cursor for the delimiter.
    delim = (delim & 1) ? '\"' : ' ';
    *left = 0;
    for (int32 i = cursor - 1; i >= 0; --i)
    {
        char c = str[i];
        if (c == delim)
        {
            *left = i + 1;
            break;
        }
    }

    const char* post = strchr(str + cursor, delim);
    if (post != nullptr)
        *right = int32(post - str);
    else
        *right = int32(strlen(str));
}



//------------------------------------------------------------------------------
int32 host_add_history(int32, const char* line)
{
    // NOTE:  This intentionally does not send the "onhistory" Lua event.
    // Since this command explicitly manipulates the history it's reasonable
    // for it to override scripts.

    history_database* h = history_database::get();
    return h && h->add(line);
}

//------------------------------------------------------------------------------
int32 host_remove_history(int32 rl_history_index, const char* line)
{
    history_database* h = history_database::get();
    return h && h->remove(rl_history_index, line);
}



//------------------------------------------------------------------------------
static int32 s_cua_anchor = -1;

//------------------------------------------------------------------------------
class cua_selection_manager
{
public:
    cua_selection_manager()
    : m_anchor(s_cua_anchor)
    , m_point(rl_point)
    {
        if (s_cua_anchor < 0)
            s_cua_anchor = rl_point;
    }

    ~cua_selection_manager()
    {
        if (s_cua_anchor >= 0)
            clear_suggestion();
        if (g_rl_buffer && (m_anchor != s_cua_anchor || m_point != rl_point))
            g_rl_buffer->set_need_draw();
    }

private:
    int32 m_anchor;
    int32 m_point;
};

//------------------------------------------------------------------------------
static void cua_delete()
{
    if (s_cua_anchor >= 0)
    {
        if (g_rl_buffer)
        {
            // Make sure rl_point is lower so it ends up in the right place.
            if (s_cua_anchor < rl_point)
                SWAP(s_cua_anchor, rl_point);
            g_rl_buffer->remove(s_cua_anchor, rl_point);
        }
        cua_clear_selection();
    }
}



//------------------------------------------------------------------------------
int32 clink_reload(int32 count, int32 invoking_key)
{
    assert(g_result);
    return force_reload_scripts();
}

//------------------------------------------------------------------------------
int32 clink_reset_line(int32 count, int32 invoking_key)
{
    using_history();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;
    clear_suggestion();

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_exit(int32 count, int32 invoking_key)
{
    clink_reset_line(1, 0);
    g_rl_buffer->insert("exit 0");
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_ctrl_c(int32 count, int32 invoking_key)
{
    if (s_cua_anchor >= 0)
    {
        cua_selection_manager mgr;
        cua_copy(count, invoking_key);
        cua_clear_selection();
        return 0;
    }

    clink_sighandler(SIGINT);

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_paste(int32 count, int32 invoking_key)
{
    str<1024> utf8;
    if (!os::get_clipboard_text(utf8))
        return 0;

    dbg_ignore_scope(snapshot, "clink_paste");

    bool done = false;
    bool sel = (s_cua_anchor >= 0);
    std::list<str_moveable> overflow;
    strip_crlf(utf8.data(), overflow, g_paste_crlf.get(), &done);
    strip_wakeup_chars(utf8);
    if (sel)
    {
        g_rl_buffer->begin_undo_group();
        cua_delete();
    }
    _rl_set_mark_at_pos(g_rl_buffer->get_cursor());
    g_rl_buffer->insert(utf8.c_str());
    if (sel)
        g_rl_buffer->end_undo_group();
    host_cmd_enqueue_lines(overflow, false, true);
    if (done)
    {
        (*rl_redisplay_function)();
        rl_newline(1, invoking_key);
    }

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_copy_line(int32 count, int32 invoking_key)
{
    os::set_clipboard_text(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_copy_word(int32 count, int32 invoking_key)
{
    if (count < 0 || !g_rl_buffer)
    {
Nope:
        rl_ding();
        return 0;
    }

    std::vector<word> words;
    if (!collect_words(*g_rl_buffer, words, collect_words_mode::whole_command))
        goto Nope;
    if (words.empty())
        goto Nope;

    if (!rl_explicit_arg)
    {
        uint32 line_cursor = g_rl_buffer->get_cursor();
        for (auto const& word : words)
        {
            if (line_cursor >= word.offset &&
                line_cursor <= word.offset + word.length)
            {
                os::set_clipboard_text(g_rl_buffer->get_buffer() + word.offset, word.length);
                return 0;
            }
        }
    }
    else
    {
        for (auto const& word : words)
        {
            if (count-- == 0)
            {
                os::set_clipboard_text(g_rl_buffer->get_buffer() + word.offset, word.length);
                return 0;
            }
        }
    }

    goto Nope;
}

//------------------------------------------------------------------------------
int32 clink_copy_cwd(int32 count, int32 invoking_key)
{
    wstr<270> cwd;
    uint32 length = GetCurrentDirectoryW(cwd.size(), cwd.data());
    if (length < cwd.size())
    {
        str<> tmp;
        to_utf8(tmp, cwd.c_str());
        tmp << PATH_SEP;
        path::normalise(tmp);
        os::set_clipboard_text(tmp.c_str(), tmp.length());
    }

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_expand_env_var(int32 count, int32 invoking_key)
{
    // Extract the word under the cursor.
    int32 word_left, word_right;
    get_word_bounds(*g_rl_buffer, &word_left, &word_right);

    str<1024> in;
    in.concat(g_rl_buffer->get_buffer() + word_left, word_right - word_left);

    str<> out;
    os::expand_env(in.c_str(), in.length(), out);

    // Update Readline with the resulting expansion.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(word_left, word_right);
    g_rl_buffer->set_cursor(word_left);
    g_rl_buffer->insert(out.c_str());
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
enum { el_alias = 1, el_envvar = 2, el_history = 4 };
static int32 do_expand_line(int32 flags)
{
    bool expanded = false;
    str<> in;
    str<> out;
    int32 point = rl_point;

    in = g_rl_buffer->get_buffer();

    if (flags & el_history)
    {
        if (expand_history(in.c_str(), out))
        {
            in = out.c_str();
            point = -1;
            expanded = true;
        }
    }

    if (flags & el_alias)
    {
        doskey_alias alias;
        doskey doskey("cmd.exe");
        doskey.resolve(in.c_str(), alias, point < 0 ? nullptr : &point);
        if (alias)
        {
            alias.next(out);
            in = out.c_str();
            expanded = true;
        }
    }

    if (flags & el_envvar)
    {
        if (os::expand_env(in.c_str(), in.length(), out, point < 0 ? nullptr : &point))
        {
            in = out.c_str();
            expanded = true;
        }
    }

    if (!expanded)
    {
        rl_ding();
        return 0;
    }

    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;
    if (!out.empty())
        g_rl_buffer->insert(out.c_str());
    if (point >= 0 && point <= rl_end)
        g_rl_buffer->set_cursor(point);
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
// Expands a doskey alias (but only the first line, if $T is present).
int32 clink_expand_doskey_alias(int32 count, int32 invoking_key)
{
    return do_expand_line(el_alias);
}

//------------------------------------------------------------------------------
// Performs history expansion.
int32 clink_expand_history(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history);
}

//------------------------------------------------------------------------------
// Performs history and doskey alias expansion.
int32 clink_expand_history_and_alias(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history|el_alias);
}

//------------------------------------------------------------------------------
// Performs history, doskey alias, and environment variable expansion.
int32 clink_expand_line(int32 count, int32 invoking_key)
{
    return do_expand_line(el_history|el_alias|el_envvar);
}

//------------------------------------------------------------------------------
int32 clink_up_directory(int32 count, int32 invoking_key)
{
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, ~0u);
    g_rl_buffer->insert(" cd ..");
    g_rl_buffer->end_undo_group();
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_insert_dot_dot(int32 count, int32 invoking_key)
{
    str<> str;

    if (uint32 cursor = g_rl_buffer->get_cursor())
    {
        char last_char = g_rl_buffer->get_buffer()[cursor - 1];
        if (last_char != ' ' && !path::is_separator(last_char))
            str << PATH_SEP;
    }

    str << ".." << PATH_SEP;

    g_rl_buffer->insert(str.c_str());

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_shift_space(int32 count, int32 invoking_key)
{
    return _rl_dispatch(' ', _rl_keymap);
}

//------------------------------------------------------------------------------
int32 clink_magic_suggest_space(int32 count, int32 invoking_key)
{
    insert_suggestion(suggestion_action::insert_next_full_word);
    g_rl_buffer->insert(" ");
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_scroll_line_up(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_line_down(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_page_up(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_page_down(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_top(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_TOEND);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_scroll_bottom(int32 count, int32 invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_TOEND);
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_find_conhost(int32 count, int32 invoking_key)
{
    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        rl_ding();
        return 0;
    }

    // Invoke conhost's Find command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_FIND, 0);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_mark_conhost(int32 count, int32 invoking_key)
{
    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        rl_ding();
        return 0;
    }

    // Conhost's Mark command is asynchronous and saves/restores the cursor info
    // and position.  So we need to trick the cursor into being visible, so that
    // it gets restored as visible since that's the state Readline will be in
    // after the Mark command finishes.
    show_cursor(true);

    // Invoke conhost's Mark command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_MARK, 0);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_selectall_conhost(int32 count, int32 invoking_key)
{
    bool has_begin = (s_cua_anchor == 0 || rl_point == 0);
    bool has_end = (s_cua_anchor == rl_end || rl_point == rl_end);
    if (!has_begin || !has_end)
        return cua_select_all(0, invoking_key);

    HWND hwndConsole = GetConsoleWindow();
    if (!hwndConsole)
    {
        rl_ding();
        return 0;
    }

    if (rl_point == 0 && s_cua_anchor == rl_end)
    {
        s_cua_anchor = 0;
        rl_point = rl_end;
        (*rl_redisplay_function)();
    }

    // Invoke conhost's Select All command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_SELECTALL, 0);
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_popup_directories(int32 count, int32 invoking_key)
{
    // Copy the directory list (just a shallow copy of the dir pointers).
    int32 total = 0;
    const char** history = host_copy_dir_history(&total);
    if (!history || !total)
    {
        free(history);
        rl_ding();
        return 0;
    }

    // Popup list.
    const popup_results results = activate_directories_text_list(history, total);

    // Handle results.
    switch (results.m_result)
    {
    case popup_result::cancel:
        break;
    case popup_result::error:
        rl_ding();
        break;
    case popup_result::select:
    case popup_result::use:
        {
            bool end_sep = (results.m_text.c_str()[0] &&
                            path::is_separator(results.m_text.c_str()[results.m_text.length() - 1]));

            char qs[2] = {};
            if (rl_basic_quote_characters &&
                rl_basic_quote_characters[0] &&
                rl_filename_quote_characters &&
                _rl_strpbrk(results.m_text.c_str(), rl_filename_quote_characters) != 0)
            {
                qs[0] = rl_basic_quote_characters[0];
            }

            str<> dir;
            dir.format("%s%s%s", qs, results.m_text.c_str(), qs);

            bool use = (results.m_result == popup_result::use);
            rl_begin_undo_group();
            if (use)
            {
                if (!end_sep)
                    dir.concat(PATH_SEP);
                rl_replace_line(dir.c_str(), 0);
                rl_point = rl_end;
            }
            else
            {
                rl_insert_text(dir.c_str());
            }
            rl_end_undo_group();
            (*rl_redisplay_function)();
            if (use)
                rl_newline(1, invoking_key);
        }
        break;
    }

    free(history);

    return 0;
}



//------------------------------------------------------------------------------
int32 clink_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_menu_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._menu_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_menu_complete_numbers_backward(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._menu_complete_numbers_backward"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_old_menu_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._old_menu_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_old_menu_complete_numbers_backward(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._old_menu_complete_numbers_backward"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_complete_numbers(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._popup_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_show_help(int32 count, int32 invoking_key)
{
    if (!host_call_lua_rl_global_function("clink._popup_show_help"))
        rl_ding();
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_select_complete(int32 count, int32 invoking_key)
{
    if (RL_ISSTATE(RL_STATE_MACRODEF) != 0)
    {
ding:
        rl_ding();
        return 0;
    }

    extern bool activate_select_complete(editor_module::result& result, bool reactivate);
    if (!g_result || !activate_select_complete(*g_result, rl_last_func == clink_select_complete))
        goto ding;
    return 0;
}



//------------------------------------------------------------------------------
bool cua_clear_selection()
{
    if (s_cua_anchor < 0)
        return false;
    s_cua_anchor = -1;
    return true;
}

//------------------------------------------------------------------------------
bool cua_set_selection(int32 anchor, int32 point)
{
    const int32 new_anchor = min<int32>(rl_end, anchor);
    const int32 new_point = max<int32>(0, min<int32>(rl_end, point));
    if (new_anchor == s_cua_anchor && new_point == rl_point)
        return false;
    s_cua_anchor = new_anchor;
    rl_point = new_point;
    return true;
}

//------------------------------------------------------------------------------
int32 cua_get_anchor()
{
    return s_cua_anchor;
}

//------------------------------------------------------------------------------
bool cua_point_in_selection(int32 in)
{
    if (s_cua_anchor < 0)
        return false;
    if (s_cua_anchor < rl_point)
        return (s_cua_anchor <= in && in < rl_point);
    else
        return (rl_point <= in && in < s_cua_anchor);
}

//------------------------------------------------------------------------------
int32 cua_selection_event_hook(int32 event)
{
    if (!g_rl_buffer)
        return 0;

    static bool s_cleanup = false;

    switch (event)
    {
    case SEL_BEFORE_INSERTCHAR:
        assert(!s_cleanup);
        if (s_cua_anchor >= 0)
        {
            s_cleanup = true;
            g_rl_buffer->begin_undo_group();
            cua_delete();
        }
        break;
    case SEL_AFTER_INSERTCHAR:
        if (s_cleanup)
        {
            g_rl_buffer->end_undo_group();
            s_cleanup = false;
        }
        break;
    case SEL_BEFORE_DELETE:
        if (s_cua_anchor < 0 || s_cua_anchor == rl_point)
            break;
        cua_delete();
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
void cua_after_command(bool force_clear)
{
    static std::unordered_set<void*> s_map;

    if (s_map.empty())
    {
        // No action after a cua command.
        s_map.emplace(cua_previous_screen_line);
        s_map.emplace(cua_next_screen_line);
        s_map.emplace(cua_backward_char);
        s_map.emplace(cua_forward_char);
        s_map.emplace(cua_backward_word);
        s_map.emplace(cua_forward_word);
        s_map.emplace(cua_beg_of_line);
        s_map.emplace(cua_end_of_line);
        s_map.emplace(cua_select_all);
        s_map.emplace(cua_copy);
        s_map.emplace(cua_cut);
        s_map.emplace(clink_selectall_conhost);

        // No action after scroll commands.
        s_map.emplace(clink_scroll_line_up);
        s_map.emplace(clink_scroll_line_down);
        s_map.emplace(clink_scroll_page_up);
        s_map.emplace(clink_scroll_page_down);
        s_map.emplace(clink_scroll_top);
        s_map.emplace(clink_scroll_bottom);

        // No action after some special commands.
        s_map.emplace(show_rl_help);
        s_map.emplace(show_rl_help_raw);
        s_map.emplace(rl_dump_functions);
        s_map.emplace(rl_dump_macros);
        s_map.emplace(rl_dump_variables);
        s_map.emplace(clink_dump_functions);
        s_map.emplace(clink_dump_macros);
    }

    // If not a recognized command, clear the cua selection.
    if (force_clear || s_map.find((void*)rl_last_func) == s_map.end())
        cua_clear_selection();
}

//------------------------------------------------------------------------------
int32 cua_previous_screen_line(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_previous_screen_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_next_screen_line(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_next_screen_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_backward_char(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_backward_char(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_forward_char(int32 count, int32 invoking_key)
{
    if (count != 0)
    {
another_word:
        if (insert_suggestion(suggestion_action::insert_next_full_word))
        {
            count--;
            if (count > 0)
                goto another_word;
            return 0;
        }
    }

    cua_selection_manager mgr;
    return rl_forward_char(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_backward_word(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_backward_word(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_forward_word(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_forward_word(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_backward_bigword(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_vi_bWord(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_forward_bigword(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return clink_forward_bigword(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_select_word(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;

    const int32 orig_point = rl_point;

    // Look forward for a word.
    rl_forward_word(1, 0);
    int32 end = rl_point;
    rl_backward_word(1, 0);
    const int32 high_mid = rl_point;

    rl_point = orig_point;

    // Look backward for a word.
    rl_backward_word(1, 0);
    int32 begin = rl_point;
    rl_forward_word(1, 0);
    const int32 low_mid = rl_point;

    if (high_mid <= orig_point)
    {
        begin = high_mid;
    }
    else if (low_mid > orig_point)
    {
        end = low_mid;
    }
    else
    {
        // The original point is between two words.  For now, select the text
        // between the words.
        begin = low_mid;
        end = high_mid;
    }

    s_cua_anchor = begin;
    rl_point = end;

    return 0;
}

//------------------------------------------------------------------------------
int32 cua_beg_of_line(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_beg_of_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_end_of_line(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    return rl_end_of_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 cua_select_all(int32 count, int32 invoking_key)
{
    cua_selection_manager mgr;
    s_cua_anchor = 0;
    rl_point = rl_end;
    return 0;
}

//------------------------------------------------------------------------------
int32 cua_copy(int32 count, int32 invoking_key)
{
    if (g_rl_buffer)
    {
        bool has_sel = (s_cua_anchor >= 0);
        uint32 len = g_rl_buffer->get_length();
        uint32 beg = has_sel ? min<uint32>(len, s_cua_anchor) : 0;
        uint32 end = has_sel ? min<uint32>(len, rl_point) : len;
        if (beg > end)
            SWAP(beg, end);
        if (beg < end)
            os::set_clipboard_text(g_rl_buffer->get_buffer() + beg, end - beg);
    }
    return 0;
}

//------------------------------------------------------------------------------
int32 cua_cut(int32 count, int32 invoking_key)
{
    cua_copy(0, 0);
    cua_delete();
    return 0;
}



//------------------------------------------------------------------------------
int32 clink_forward_word(int32 count, int32 invoking_key)
{
    if (count != 0)
    {
another_word:
        if (insert_suggestion(suggestion_action::insert_next_word))
        {
            count--;
            if (count > 0)
                goto another_word;
        }
    }

    return rl_forward_word(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 clink_forward_bigword(int32 count, int32 invoking_key)
{
    if (count != 0)
    {
another_word:
        if (insert_suggestion(suggestion_action::insert_next_full_word))
        {
            count--;
            if (count > 0)
                goto another_word;
        }
    }

    return rl_vi_fWord(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 clink_forward_char(int32 count, int32 invoking_key)
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return rl_forward_char(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 clink_forward_byte(int32 count, int32 invoking_key)
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return rl_forward_byte(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 clink_end_of_line(int32 count, int32 invoking_key)
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    return rl_end_of_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int32 clink_insert_suggested_line(int32 count, int32 invoking_key)
{
    if (!insert_suggestion(suggestion_action::insert_to_end))
        rl_ding();

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_insert_suggested_full_word(int32 count, int32 invoking_key)
{
    if (!insert_suggestion(suggestion_action::insert_next_full_word))
        rl_ding();

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_insert_suggested_word(int32 count, int32 invoking_key)
{
    if (!insert_suggestion(suggestion_action::insert_next_word))
        rl_ding();

    return 0;
}

//------------------------------------------------------------------------------
int32 clink_accept_suggested_line(int32 count, int32 invoking_key)
{
    if (insert_suggestion(suggestion_action::insert_to_end))
        return rl_newline(count, invoking_key);

    rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_popup_history(int32 count, int32 invoking_key)
{
    HIST_ENTRY** list = history_list();
    if (!list || !history_length)
    {
        rl_ding();
        return 0;
    }

    rl_completion_invoking_key = invoking_key;

    int32 current = -1;
    int32 orig_pos = where_history();
    int32 search_len = rl_point;

    // Copy the history list (just a shallow copy of the line pointers).
    char** history = (char**)malloc(sizeof(*history) * history_length);
    entry_info* infos = (entry_info*)malloc(sizeof(*infos) * history_length);
    int32 total = 0;
    for (int32 i = 0; i < history_length; i++)
    {
        if (!find_streqn(g_rl_buffer->get_buffer(), list[i]->line, search_len))
            continue;
        history[total] = list[i]->line;
        infos[total].index = i;
        infos[total].marked = (list[i]->data != nullptr);
        if (i == orig_pos)
            current = total;
        total++;
    }
    if (!total)
    {
        rl_ding();
        free(history);
        free(infos);
        return 0;
    }
    if (current < 0)
        current = total - 1;

    // Popup list.
    const popup_results results = activate_history_text_list(const_cast<const char**>(history), total, current, infos, false/*win_history*/);

    switch (results.m_result)
    {
    case popup_result::cancel:
        break;
    case popup_result::error:
        rl_ding();
        break;
    case popup_result::select:
    case popup_result::use:
        {
            rl_maybe_save_line();
            rl_maybe_replace_line();

            const int32 pos = infos[results.m_index].index;
            history_set_pos(pos);
            rl_replace_from_history(current_history(), 0);
            suppress_suggestions();

            bool point_at_end = (!search_len || _rl_history_point_at_end_of_anchored_search);
            rl_point = point_at_end ? rl_end : search_len;
            rl_mark = point_at_end ? search_len : rl_end;

            (*rl_redisplay_function)();
            if (results.m_result == popup_result::use)
                rl_newline(1, invoking_key);
        }
        break;
    }

    free(history);
    free(infos);

    return 0;
}



//------------------------------------------------------------------------------
static int32 adjust_point_delta(int32& point, int32 delta, char* buffer)
{
    if (delta <= 0)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    if (delta > length - point)
        delta = length - point;

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        tmp += delta;
        count += delta;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        while (delta)
        {
            int32 was = tmp;
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            if (tmp <= was)
                break;
            count++;
            delta--;
        }
    }
#endif

    point = tmp;
    return count;
}

//------------------------------------------------------------------------------
static int32 adjust_point_point(int32& point, int32 target, char* buffer)
{
    if (target <= point)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    if (target > length)
        target = length;

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        count = target - tmp;
        tmp = target;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        while (tmp < target)
        {
            int32 was = tmp;
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            if (tmp <= was)
                break;
            count++;
        }
    }
#endif

    point = tmp;
    return true;
}

//------------------------------------------------------------------------------
static int32 adjust_point_keyseq(int32& point, const char* keyseq, char* buffer)
{
    if (!keyseq || !*keyseq)
        return 0;

    const int32 length = int32(strlen(buffer));
    if (point == length)
        return 0;

    if (point > length)
    {
        point = length;
        return 0;
    }

    int32 tmp = point;
    int32 count = 0;

#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX == 1 || rl_byte_oriented)
#endif
    {
        const char* found = strstr(buffer + tmp, keyseq);
        int32 delta = found ? int32(found - (buffer + tmp)) : length - tmp;
        tmp += delta;
        count += delta;
    }
#if defined (HANDLE_MULTIBYTE)
    else
    {
        int32 keyseq_len = int32(strlen(keyseq));
        while (buffer[tmp] && strncmp(buffer + tmp, keyseq, keyseq_len) != 0)
        {
            tmp = _rl_find_next_mbchar(buffer, tmp, 1, MB_FIND_NONZERO);
            count++;
        }
    }
#endif

    if (tmp > length)
        tmp = length;

    point = tmp;
    return count;
}

//------------------------------------------------------------------------------
static str<16, false> s_win_fn_input_buffer;
static bool read_win_fn_input_char()
{
    int32 c;

    RL_SETSTATE(RL_STATE_MOREINPUT);
    c = rl_read_key();
    RL_UNSETSTATE(RL_STATE_MOREINPUT);

    if (c < 0)
        return false;

    if (RL_ISSTATE(RL_STATE_MACRODEF))
        _rl_add_macro_char(c);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals ();
#endif

    if (c == 27/*Esc*/ || c == 7/*^G*/)
    {
nope:
        s_win_fn_input_buffer.clear();
        return true;
    }

    s_win_fn_input_buffer.concat(reinterpret_cast<const char*>(&c), 1);

    WCHAR_T wc;
    mbstate_t mbs = {};
    size_t validate = MBRTOWC(&wc, s_win_fn_input_buffer.c_str(), s_win_fn_input_buffer.length(), &mbs);

    if (MB_NULLWCH(validate))
        goto nope;

    // Once there's a valid UTF8 character, the input is complete.
    return !MB_INVALIDCH(validate);
}

//------------------------------------------------------------------------------
static char* get_history(int32 item)
{
    HIST_ENTRY** list = history_list();
    if (!list || !history_length)
        return nullptr;

    if (item >= history_length)
        item = history_length - 1;
    if (item < 0)
        return nullptr;

    return list[item]->line;
}

//------------------------------------------------------------------------------
static char* get_previous_command()
{
    int32 previous = where_history();
    return get_history(previous);
}

//------------------------------------------------------------------------------
int32 win_f1(int32 count, int32 invoking_key)
{
    const bool had_selection = (cua_get_anchor() >= 0);

    if (insert_suggestion(suggestion_action::insert_to_end))
        return 0;

    if (count <= 0)
        count = 1;

    while (count && rl_point < rl_end)
    {
        rl_forward_char(1, invoking_key);
        count--;
    }

    if (!count)
        return 0;

    if (had_selection)
        return 0;

    char* prev_buffer = get_previous_command();
    if (!prev_buffer)
    {
ding:
        rl_ding();
        return 0;
    }

    int32 old_point = 0;
    adjust_point_point(old_point, rl_point, prev_buffer);
    if (!prev_buffer[old_point])
        goto ding;

    int32 end_point = old_point;
    adjust_point_delta(end_point, count, prev_buffer);
    if (end_point <= old_point)
        goto ding;

    str<> more;
    more.concat(prev_buffer + old_point, end_point - old_point);
    rl_insert_text(more.c_str());

    // Prevent generating a suggestion when inserting characters from the
    // previous command, otherwise it's often only possible to insert one
    // character before suggestions take over.
    set_suggestion(rl_line_buffer, 0, rl_line_buffer, 0);

    return 0;
}

//------------------------------------------------------------------------------
static int32 finish_win_f2()
{
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    char* prev_buffer = get_previous_command();
    if (!prev_buffer)
    {
        rl_ding();
        return 0;
    }

    if (s_win_fn_input_buffer.empty())
        return 0;

    int32 old_point = 0;
    adjust_point_point(old_point, rl_point, prev_buffer);
    if (prev_buffer[old_point])
    {
        int32 end_point = old_point;
        int32 count = adjust_point_keyseq(end_point, s_win_fn_input_buffer.c_str(), prev_buffer);
        if (end_point > old_point)
        {
            // How much to delete.
            int32 del_point = rl_point;
            adjust_point_delta(del_point, count, rl_line_buffer);

            // What to insert.
            str<> more;
            more.concat(prev_buffer + old_point, end_point - old_point);

            rl_begin_undo_group();
            rl_delete_text(rl_point, del_point);
            rl_insert_text(more.c_str());
            rl_end_undo_group();
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f2_callback(_rl_callback_generic_arg *data)
{
    if (!read_win_fn_input_char())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    _rl_want_redisplay = 1;

    return finish_win_f2();
}
#endif

//------------------------------------------------------------------------------
static const char c_normal[] = "\001\x1b[m\002";
int32 win_f2(int32 count, int32 invoking_key)
{
    s_win_fn_input_buffer.clear();
    rl_message("\x01\x1b[%sm\x02(enter char to copy up to: )%s ", get_popup_colors(), c_normal);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f2_callback;
        return 0;
    }
#endif

    while (!read_win_fn_input_char())
        ;

    return finish_win_f2();
}

//------------------------------------------------------------------------------
int32 win_f3(int32 count, int32 invoking_key)
{
    return win_f1(999999, invoking_key);
}

//------------------------------------------------------------------------------
static int32 finish_win_f4()
{
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    if (s_win_fn_input_buffer.empty())
        return 0;

    int32 end_point = rl_point;
    adjust_point_keyseq(end_point, s_win_fn_input_buffer.c_str(), rl_line_buffer);
    if (end_point > rl_point)
        rl_delete_text(rl_point, end_point);

    return 0;
}

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f4_callback(_rl_callback_generic_arg *data)
{
    if (!read_win_fn_input_char())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    _rl_want_redisplay = 1;

    return finish_win_f4();
}
#endif

//------------------------------------------------------------------------------
int32 win_f4(int32 count, int32 invoking_key)
{
    s_win_fn_input_buffer.clear();
    rl_message("\x01\x1b[%sm\x02(enter char to delete up to: )%s ", get_popup_colors(), c_normal);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f4_callback;
        return 0;
    }
#endif

    while (!read_win_fn_input_char())
        ;

    return finish_win_f4();
}

//------------------------------------------------------------------------------
int32 win_f6(int32 count, int32 invoking_key)
{
    rl_insert_text("\x1a");
    return 0;
}

//------------------------------------------------------------------------------
int32 win_f7(int32 count, int32 invoking_key)
{
    if (RL_ISSTATE(RL_STATE_MACRODEF) != 0)
    {
ding:
        rl_ding();
        return 0;
    }

    HIST_ENTRY** list = history_list();
    if (!list)
        goto ding;

    const char** history = static_cast<const char**>(calloc(history_length, sizeof(const char**)));
    if (!history)
        goto ding;

#define ding __cant_goto__must_free_local__

    for (int32 i = 0; i < history_length; i++)
    {
        const char* p = list[i]->line;
        assert(p);
        history[i] = p ? p : "";
    }

    int32 current = where_history();
    if (current < 0 || current > history_length - 1)
        current = history_length - 1;

    const popup_results results = activate_history_text_list(history, history_length, current, nullptr, true/*win_history*/);

    switch (results.m_result)
    {
    case popup_result::error:
        rl_ding();
        break;

    case popup_result::use:
    case popup_result::select:
        rl_maybe_save_line();
        rl_maybe_replace_line();
        history_set_pos(results.m_index);
        rl_replace_from_history(current_history(), 0);
        suppress_suggestions();
        (*rl_redisplay_function)();
        if (results.m_result == popup_result::use)
            rl_newline(1, 0);
        break;
    }

    free(history);

    return 0;

#undef ding
}

//------------------------------------------------------------------------------
static int32 s_history_number = -1;
static int32 finish_win_f9()
{
#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals();
#endif

    rl_clear_message();

    if (s_history_number >= 0)
    {
        if (s_history_number >= history_length)
            s_history_number = history_length - 1;
        if (history_length > 0)
        {
            rl_begin_undo_group();
            rl_delete_text(0, rl_end);
            rl_point = 0;
            rl_insert_text(get_history(s_history_number));
            rl_end_undo_group();
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static void set_f9_message()
{
    if (s_history_number >= 0)
        rl_message("\x01\x1b[%sm\x02(enter history number: %d)%s ", get_popup_colors(), s_history_number, c_normal);
    else
        rl_message("\x01\x1b[%sm\x02(enter history number: )%s ", get_popup_colors(), c_normal);
}

//------------------------------------------------------------------------------
static bool read_history_digit()
{
    int32 c;

    RL_SETSTATE(RL_STATE_MOREINPUT);
    c = rl_read_key();
    RL_UNSETSTATE(RL_STATE_MOREINPUT);

    if (c < 0)
        return false;

    if (RL_ISSTATE(RL_STATE_MACRODEF))
        _rl_add_macro_char(c);

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_restore_tty_signals ();
#endif

    if (c >= '0' && c <= '9')
    {
        if (s_history_number < 0)
            s_history_number = 0;
        if (s_history_number <= 99999)
        {
            s_history_number *= 10;
            s_history_number += c - '0';
        }
    }
    else if (c == 27/*Esc*/ || c == 7/*^G*/)
    {
        s_history_number = -1;
        return true;
    }
    else if (c == 13/*Enter*/)
    {
        return true;
    }
    else if (c == 8/*Backspace*/)
    {
        s_history_number /= 10;
        if (s_history_number == 0)
            s_history_number = -1;
    }

    set_f9_message();
    return false;
}

//------------------------------------------------------------------------------
#if defined (READLINE_CALLBACKS)
int32 _win_f9_callback(_rl_callback_generic_arg *data)
{
    if (!read_history_digit())
        return 0;

    /* Deregister function, let rl_callback_read_char deallocate data */
    _rl_callback_func = 0;
    _rl_want_redisplay = 1;

    return finish_win_f9();
}
#endif

//------------------------------------------------------------------------------
int32 win_f9(int32 count, int32 invoking_key)
{
    s_history_number = -1;
    set_f9_message();

#if defined (HANDLE_SIGNALS)
    if (RL_ISSTATE(RL_STATE_CALLBACK) == 0)
        _rl_disable_tty_signals ();
#endif

#if defined (READLINE_CALLBACKS)
    if (RL_ISSTATE(RL_STATE_CALLBACK))
    {
        _rl_callback_data = _rl_callback_data_alloc(count);
        _rl_callback_func = _win_f9_callback;
        return 0;
    }
#endif

    while (!read_history_digit())
        ;

    return finish_win_f9();
}

//------------------------------------------------------------------------------
bool win_fn_callback_pending()
{
    return (_rl_callback_func == _win_f2_callback ||
            _rl_callback_func == _win_f4_callback ||
            _rl_callback_func == _win_f9_callback);
}



//------------------------------------------------------------------------------
static bool s_globbing_wild = false;
static bool s_literal_wild = false;
bool is_globbing_wild() { return s_globbing_wild; }
bool is_literal_wild() { return s_literal_wild; }

//------------------------------------------------------------------------------
static int32 glob_completion_internal(int32 what_to_do)
{
    s_globbing_wild = true;
    if (!rl_explicit_arg)
        s_literal_wild = true;

    return rl_complete_internal(what_to_do);
}

//------------------------------------------------------------------------------
int32 glob_complete_word(int32 count, int32 invoking_key)
{
    if (rl_editing_mode == emacs_mode)
        rl_explicit_arg = 1; /* force `*' append */

    return glob_completion_internal(rl_completion_mode(glob_complete_word));
}

//------------------------------------------------------------------------------
int32 glob_expand_word(int32 count, int32 invoking_key)
{
    return glob_completion_internal('*');
}

//------------------------------------------------------------------------------
int32 glob_list_expansions(int32 count, int32 invoking_key)
{
    return glob_completion_internal('?');
}



//------------------------------------------------------------------------------
int32 edit_and_execute_command(int32 count, int32 invoking_key)
{
    str<> line;
    if (rl_explicit_arg)
    {
        HIST_ENTRY* h = history_get(count);
        if (!h)
        {
            rl_ding();
            return 0;
        }
        line = h->line;
    }
    else
    {
        line.concat(rl_line_buffer, rl_end);
        if (!host_add_history(0, line.c_str()))
        {
            rl_ding();
            return 0;
        }
    }

    str_moveable tmp_file;
    FILE* file = os::create_temp_file(&tmp_file);
    if (!file)
    {
LDing:
        rl_ding();
        return 0;
    }

    if (fputs(line.c_str(), file) < 0)
    {
        fclose(file);
LUnlinkFile:
        unlink(tmp_file.c_str());
        goto LDing;
    }
    fclose(file);
    file = nullptr;

    // Save and reset console state.
    HANDLE std_handles[2] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE) };
    DWORD prev_mode[2];
    static_assert(_countof(std_handles) == _countof(prev_mode), "array sizes must match");
    for (size_t i = 0; i < _countof(std_handles); ++i)
        GetConsoleMode(std_handles[i], &prev_mode[i]);
    SetConsoleMode(std_handles[0], (prev_mode[0] | ENABLE_PROCESSED_INPUT) & ~(ENABLE_WINDOW_INPUT|ENABLE_MOUSE_INPUT));
    bool was_visible = show_cursor(true);
    rl_clear_signals();

    // Build editor command.
    str<> editor;
    str_moveable command;
    const char* const qs = (strpbrk(tmp_file.c_str(), rl_filename_quote_characters)) ? "\"" : "";
    if ((!os::get_env("VISUAL", editor) && !os::get_env("EDITOR", editor)) || editor.empty())
        editor = "%systemroot%\\system32\\notepad.exe";
    command.format("%s %s%s%s", editor.c_str(), qs, tmp_file.c_str(), qs);

    // Execute editor command.
    wstr_moveable wcommand(command.c_str());
    const int32 exit_code = _wsystem(wcommand.c_str());

    // Restore console state.
    show_cursor(was_visible);
    prev_mode[0] = cleanup_console_input_mode(prev_mode[0]);
    for (size_t i = 0; i < _countof(std_handles); ++i)
        SetConsoleMode(std_handles[i], prev_mode[i]);
    rl_set_signals();

    // Was the editor launched successfully?
    if (exit_code < 0)
        goto LUnlinkFile;

    // Read command(s) from temp file.
    line.clear();
    wstr_moveable wtmp_file(tmp_file.c_str());
    file = _wfopen(wtmp_file.c_str(), L"rt");
    if (!file)
        goto LUnlinkFile;
    char buffer[4096];
    while (true)
    {
        const int32 len = fread(buffer, 1, sizeof(buffer), file);
        if (len <= 0)
            break;
        line.concat(buffer, len);
    }
    fclose(file);

    // Trim trailing newlines to avoid redundant blank commands.  Ensure a final
    // newline so all lines get executed (otherwise it will go into edit mode).
    while (line.length() && line.c_str()[line.length() - 1] == '\n')
        line.truncate(line.length() - 1);
    line.concat("\n");

    // Split into multiple lines.
    std::list<str_moveable> overflow;
    strip_crlf(line.data(), overflow, paste_crlf_crlf, nullptr);
    strip_wakeup_chars(line);

    // Replace the input line with the content from the temp file.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;
    if (!line.empty())
        g_rl_buffer->insert(line.c_str());
    g_rl_buffer->end_undo_group();

    // Queue any additional lines.
    host_cmd_enqueue_lines(overflow, false, true);

    // Accept the input and execute it.
    (*rl_redisplay_function)();
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int32 magic_space(int32 count, int32 invoking_key)
{
    str<> in;
    str<> out;

    in.concat(g_rl_buffer->get_buffer(), g_rl_buffer->get_cursor());
    if (expand_history(in.c_str(), out))
    {
        g_rl_buffer->begin_undo_group();
        g_rl_buffer->remove(0, rl_point);
        rl_point = 0;
        if (!out.empty())
            g_rl_buffer->insert(out.c_str());
        g_rl_buffer->end_undo_group();
    }

    rl_insert(1, ' ');
    return 0;
}



//------------------------------------------------------------------------------
struct alert_char
{
    char32_t        ucs;
    char32_t        ucs2;
    const char*     text;
    uint32          len;
};

//------------------------------------------------------------------------------
static void list_ambiguous_codepoints(const char* tag, const std::vector<alert_char>& chars)
{
    static const char red[] = "\x1b[1;91;40m";
    static const char norm[] = "\x1b[m";

    str<> s;
    str<> tmp;

    s << "  " << tag << ":\n";
    g_printer->print(s.c_str(), s.length());

    for (alert_char ac : chars)
    {
        // Print formatted string.

        if (ac.ucs2)
            s.format("        Unicode: %s0x%04X 0x%04X%s, UTF8", red, ac.ucs, ac.ucs2, norm);
        else
            s.format("        Unicode: %s0x%04X%s, UTF8", red, ac.ucs, norm);
        for (uint32 i = 0; i < ac.len; ++i)
        {
            tmp.format(" %s0x%02.2X%s", red, uint8(ac.text[i]), norm);
            s.concat(tmp.c_str(), tmp.length());
        }
        tmp.format(", reported width %s%d%s", red, clink_wcswidth(ac.text, ac.len), norm);
        s << tmp << ", text \"" << red;
        s.concat(ac.text, ac.len);
        s << norm << "\"\n";
        g_printer->print(s.c_str(), s.length());

        // Log plain text string.

        ecma48_state state;
        ecma48_iter iter(s.c_str(), state);
        tmp.clear();

        while (const ecma48_code& code = iter.next())
            if (code.get_type() == ecma48_code::type_chars)
                tmp.concat(code.get_pointer(), code.get_length());

        tmp.trim();

        LOG("%s", tmp.c_str());
    }
}

//------------------------------------------------------------------------------
static void list_problem_codes(const std::vector<prompt_problem_details>& problems)
{
    static const char err[] = "\x1b[1;91;40m";
    static const char wrn[] = "\x1b[1;93;40m";
    static const char norm[] = "\x1b[m";

    str<> s;
    str<> tmp;

    for (auto const& problem : problems)
    {
        // Print formatted string.

        const char* color = (problem.type & BIT_PROMPT_PROBLEM) ? err : wrn;

        s.clear();
        s << "        " << color;
        if (problem.type & BIT_PROMPT_PROBLEM)
            s << "Problem:";
        else
            s << "Warning:";
        s << norm << " at offset ";

        tmp.format("%d", problem.offset);
        s << tmp.c_str() << ", text \"" << color;

        {
            str_iter iter(problem.code.c_str());
            const char* seq = iter.get_pointer();
            while (int32 c = iter.next())
            {
                if (c < 0x20)
                {
                    char ctrl[2] = { '^', char(c + 0x40) };
                    s.concat(ctrl, 2);
                }
                else if (c >= 0x7f && c < 0xa0)
                {
                    s.concat("^?", 2);
                }
                else
                {
                    s.concat(seq, int32(iter.get_pointer() - seq));
                }
                seq = iter.get_pointer();
            }
        }

        s << norm << "\"\n";
        g_printer->print(s.c_str(), s.length());

        // Log plain text string.

        {
            ecma48_state state;
            ecma48_iter iter(s.c_str(), state);
            tmp.clear();

            while (const ecma48_code& code = iter.next())
                if (code.get_type() == ecma48_code::type_chars)
                    tmp.concat(code.get_pointer(), code.get_length());
        }

        tmp.trim();
        LOG("%s", tmp.c_str());
    }
}

//------------------------------------------------------------------------------
static void analyze_char_widths(const char* s,
                                std::vector<alert_char>& cjk,
                                std::vector<alert_char>& emoji,
                                std::vector<alert_char>& qualified)
{
    if (!s)
        return;

    bool ignoring = false;
    str_iter iter(s);
    while (true)
    {
        const char* const text = iter.get_pointer();
        const int32 c = iter.next();
        if (!c)
            break;

        if (c == RL_PROMPT_START_IGNORE && !ignoring)
            ignoring = true;
        else if (c == RL_PROMPT_END_IGNORE && ignoring)
            ignoring = false;
        else if (!ignoring)
        {
            const int32 kind = test_ambiguous_width_char(c, &iter);
            if (kind)
            {
                alert_char ac = {};
                ac.ucs = c;
                if (kind == 4)
                    ac.ucs2 = iter.next();
                ac.text = text;
                ac.len = uint32(iter.get_pointer() - text);

                switch (kind)
                {
                case 1: cjk.push_back(ac); break;
                case 2: emoji.push_back(ac); break;
                case 3: qualified.push_back(ac); break;
                case 4: emoji.push_back(ac); break;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
extern void task_manager_diagnostics();
int32 clink_diagnostics(int32 count, int32 invoking_key)
{
    end_prompt(true/*crlf*/);

    static char bold[] = "\x1b[1m";
    static char norm[] = "\x1b[m";
    static char lf[] = "\n";

    str<> s;
    str<> t;
    const char* p;
    const int32 spacing = 16;

    int32 id = 0;
    host_context context;
    host_get_app_context(id, context);

    auto print_heading = [&](const char* text)
    {
        s.clear();
        s << bold << text << ":" << norm << lf;
        g_printer->print(s.c_str(), s.length());
    };

    auto print_value = [&](const char* name, const char* value)
    {
        if (value && *value)
        {
            s.clear();
            s.format("  %-*s  %s\n", spacing, name, value);
            g_printer->print(s.c_str(), s.length());
        }
    };

    // Version and binaries dir.

    print_heading("version");

    print_value("version", CLINK_VERSION_STR);
    print_value("binaries", context.binaries.c_str());

    if (rl_explicit_arg)
        print_value("architecture", AS_STR(ARCHITECTURE_NAME));

    // Session info.

    print_heading("session");

    printf("  %-*s  %d\n", spacing, "session", id);

    print_value("profile", context.profile.c_str());
    print_value("log", file_logger::get_path());    // ACTUAL FILE IN USE.
    print_value("default_settings", context.default_settings.c_str());

    settings::get_settings_file(t);
    print_value("settings", t.c_str());             // ACTUAL FILE IN USE.

    history_database* history = history_database::get();
    if (history)
    {
        history->get_history_path(t);
        print_value("history", t.c_str());          // ACTUAL FILE IN USE.
    }

    print_value("scripts", context.scripts.c_str());
    print_value("default_inputrc", context.default_inputrc.c_str());
    print_value("inputrc", rl_get_last_init_file());    // ACTUAL FILE IN USE.

    // Language info.

    if (rl_explicit_arg)
    {
        print_heading("language");

        const DWORD cpid = GetACP();
        const DWORD kbid = LOWORD(GetKeyboardLayout(0));
        WCHAR wide_layout_name[KL_NAMELENGTH * 2];
        if (!GetKeyboardLayoutNameW(wide_layout_name))
            wide_layout_name[0] = 0;
        t = wide_layout_name;

        printf("  %-*s  %u\n", spacing, "codepage", cpid);
        printf("  %-*s  %u\n", spacing, "keyboard langid", kbid);
        print_value("keyboard layout", t.c_str());
    }

    // Terminal info.

    if (rl_explicit_arg)
    {
        print_heading("terminal");

        const char* term = nullptr;
        switch (get_current_ansi_handler())
        {
        default:                            term = "Unknown"; break;
        case ansi_handler::clink:           term = "Clink terminal emulation"; break;
        case ansi_handler::ansicon:         term = "ANSICON"; break;
        case ansi_handler::conemu:          term = "ConEmu"; break;
        case ansi_handler::winterminal:     term = "Windows Terminal"; break;
        case ansi_handler::wezterm:         term = "WezTerm"; break;
        case ansi_handler::winconsolev2:    term = "Console V2 (with 24 bit color)"; break;
        case ansi_handler::winconsole:      term = "Default console (16 bit color only)"; break;
        }

        const char* found = get_found_ansi_handler();
        if (get_is_auto_ansi_handler() && found)
            t.format("%s (auto mode found '%s')", term, found);
        else
            t = term;

        print_value("terminal", t.c_str());
    }

    host_call_lua_rl_global_function("clink._diagnostics");

    task_manager_diagnostics();

    // Check for known potential ambiguous character width issues.

    {
        const char* prompt = strrchr(rl_display_prompt, '\n');
        if (!prompt)
            prompt = rl_display_prompt;
        else
            prompt++;

        std::vector<alert_char> cjk;
        std::vector<alert_char> emoji;
        std::vector<alert_char> qualified;

        analyze_char_widths(prompt, cjk, emoji, qualified);
        analyze_char_widths(rl_rprompt, cjk, emoji, qualified);

        if (cjk.size() || emoji.size() || qualified.size())
        {
            print_heading("ambiguous width characters in prompt");

            if (cjk.size())
            {
                list_ambiguous_codepoints("CJK ambiguous characters", cjk);
                puts("    Running 'chcp 65001' can often fix width problems with these characters.\n"
                     "    Or you can use a different character.");
            }

            if (emoji.size())
            {
                list_ambiguous_codepoints("color emoji", emoji);
                puts("    To fix problems with these, try using a different symbol or a different\n"
                     "    terminal program.  Or sometimes using a different font can help.");
            }

            if (qualified.size())
            {
                list_ambiguous_codepoints("qualified emoji", qualified);
                puts("    To fix problems with these, try using a different symbol or a different\n"
                     "    terminal program.  Or sometimes using a different font can help.");
                puts("    The fully-qualified forms of these symbols often encounter problems,\n"
                     "    but the unqualified forms often work.  For a table of emoji and their\n"
                     "    forms see https://www.unicode.org/Public/emoji/15.0/emoji-test.txt");
            }
        }
    }

    // Check for problem escape codes and characters in prompt string.

    {
        std::vector<prompt_problem_details> problems;
        prompt_contains_problem_codes(rl_display_prompt, &problems);

        if (!problems.empty())
        {
            print_heading("problematic codes in prompt");
            list_problem_codes(problems);
            puts("    These characters in the prompt string can cause problems.  Clink will try\n"
                 "    to compensate as much as it can, but for best results you may need to fix\n"
                 "    the prompt string by removing the characters.");
        }
    }

    if (!rl_explicit_arg)
        g_printer->print("\n(Use a numeric argument for additional diagnostics; e.g. press Alt+1 first.)\n");

    rl_forced_update_display();
    return 0;
}



//------------------------------------------------------------------------------
void reset_command_states()
{
    s_globbing_wild = false;
    s_literal_wild = false;
}
