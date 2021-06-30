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

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/settings.h>
#include <terminal/printer.h>
#include <terminal/scroll.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/history.h>
#include <readline/xmalloc.h>
}

#include <list>
#include <unordered_set>



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
static setting_enum g_paste_crlf(
    "clink.paste_crlf",
    "Strips CR and LF chars on paste",
    "Setting this to 'space' makes Clink strip CR and LF characters from text\n"
    "pasted into the current line.  Set this to 'delete' to strip all newline\n"
    "characters to replace them with a space.  Set this to 'ampersand' to replace\n"
    "all newline characters with an ampersand.  Or set this to 'crlf' to paste all\n"
    "newline characters as-is (executing commands that end with newline).",
    "delete,space,ampersand,crlf",
    3);

extern setting_bool g_adjust_cursor_style;



//------------------------------------------------------------------------------
extern line_buffer* g_rl_buffer;
extern word_collector* g_word_collector;
extern bool s_force_reload_scripts;
extern editor_module::result* g_result;
extern void host_cmd_enqueue_lines(std::list<str_moveable>& lines);

//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
static void strip_crlf(char* line, std::list<str_moveable>& overflow, bool& done)
{
    int setting = g_paste_crlf.get();

    bool has_overflow = false;
    int prev_was_crlf = 0;
    char* write = line;
    const char* read = line;
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
            case 0: // delete
                break;
            case 1: // space
                prev_was_crlf = 1;
                *write = ' ';
                ++write;
                break;
            case 2: // ampersand
                prev_was_crlf = 1;
                *write = '&';
                ++write;
                break;
            case 3: // crlf
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
                unsigned int len = (unsigned int)(end - start);
                overflow.emplace_back();
                str_moveable& back = overflow.back();
                back.reserve(len);
                back.concat(start, len);
            }

            start = end;
        }
    }
}

//------------------------------------------------------------------------------
static void get_word_bounds(const line_buffer& buffer, int* left, int* right)
{
    const char* str = buffer.get_buffer();
    unsigned int cursor = buffer.get_cursor();

    // Determine the word delimiter depending on whether the word's quoted.
    int delim = 0;
    for (unsigned int i = 0; i < cursor; ++i)
    {
        char c = str[i];
        delim += (c == '\"');
    }

    // Search outwards from the cursor for the delimiter.
    delim = (delim & 1) ? '\"' : ' ';
    *left = 0;
    for (int i = cursor - 1; i >= 0; --i)
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
        *right = int(post - str);
    else
        *right = int(strlen(str));
}



//------------------------------------------------------------------------------
static int s_cua_anchor = -1;

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
        if (g_rl_buffer && (m_anchor != s_cua_anchor || m_point != rl_point))
            g_rl_buffer->set_need_draw();
    }

private:
    int m_anchor;
    int m_point;
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
int clink_reload(int count, int invoking_key)
{
    assert(g_result);
    s_force_reload_scripts = true;
    if (g_result)
        g_result->done(true); // Force a new edit line so scripts can be reloaded.
    return rl_re_read_init_file(0, 0);
}

//------------------------------------------------------------------------------
int clink_reset_line(int count, int invoking_key)
{
    using_history();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;

    return 0;
}

//------------------------------------------------------------------------------
int clink_exit(int count, int invoking_key)
{
    clink_reset_line(1, 0);
    g_rl_buffer->insert("exit");
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int clink_ctrl_c(int count, int invoking_key)
{
    if (s_cua_anchor >= 0)
    {
        cua_selection_manager mgr;
        cua_copy(count, invoking_key);
        cua_clear_selection();
        return 0;
    }

    clink_reset_line(1, 0);
    write_line_feed();
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int clink_paste(int count, int invoking_key)
{
    if (OpenClipboard(nullptr) == FALSE)
        return 0;

    str<1024> utf8;
    HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
    if (clip_data != nullptr)
        to_utf8(utf8, (wchar_t*)clip_data);

    CloseClipboard();

    bool done = false;
    bool sel = (s_cua_anchor >= 0);
    std::list<str_moveable> overflow;
    strip_crlf(utf8.data(), overflow, done);
    if (sel)
    {
        g_rl_buffer->begin_undo_group();
        cua_delete();
    }
    g_rl_buffer->insert(utf8.c_str());
    if (sel)
        g_rl_buffer->end_undo_group();
    host_cmd_enqueue_lines(overflow);
    if (done)
    {
        rl_redisplay();
        rl_newline(1, invoking_key);
    }

    return 0;
}

//------------------------------------------------------------------------------
static void copy_impl(const char* value, int length)
{
    int size = 0;
    if (length)
    {
        size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0) * sizeof(wchar_t);
        if (!size)
            return;
    }
    size += sizeof(wchar_t);

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, size);
    if (mem == nullptr)
        return;

    if (length)
    {
        wchar_t* data = (wchar_t*)GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, value, length, data, size);
        GlobalUnlock(mem);
    }

    if (OpenClipboard(nullptr) == FALSE)
        return;

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem); // Windows automatically dynamically converts to CF_TEXT as needed.
    CloseClipboard();
}

//------------------------------------------------------------------------------
int clink_copy_line(int count, int invoking_key)
{
    copy_impl(g_rl_buffer->get_buffer(), g_rl_buffer->get_length());

    return 0;
}

//------------------------------------------------------------------------------
int clink_copy_word(int count, int invoking_key)
{
    if (count < 0 || !g_rl_buffer || !g_word_collector)
    {
Nope:
        rl_ding();
        return 0;
    }

    std::vector<word> words;
    g_word_collector->collect_words(*g_rl_buffer, words, collect_words_mode::whole_command);

    if (words.empty())
        goto Nope;

    if (!rl_explicit_arg)
    {
        unsigned int line_cursor = g_rl_buffer->get_cursor();
        for (auto const& word : words)
        {
            if (line_cursor >= word.offset &&
                line_cursor <= word.offset + word.length)
            {
                copy_impl(g_rl_buffer->get_buffer() + word.offset, word.length);
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
                copy_impl(g_rl_buffer->get_buffer() + word.offset, word.length);
                return 0;
            }
        }
    }

    goto Nope;
}

//------------------------------------------------------------------------------
int clink_copy_cwd(int count, int invoking_key)
{
    wstr<270> cwd;
    unsigned int length = GetCurrentDirectoryW(cwd.size(), cwd.data());
    if (length < cwd.size())
    {
        str<> tmp;
        to_utf8(tmp, cwd.c_str());
        tmp << PATH_SEP;
        path::normalise(tmp);
        copy_impl(tmp.c_str(), tmp.length());
    }

    return 0;
}

//------------------------------------------------------------------------------
int clink_expand_env_var(int count, int invoking_key)
{
    // Extract the word under the cursor.
    int word_left, word_right;
    get_word_bounds(*g_rl_buffer, &word_left, &word_right);

    str<1024> in;
    in.concat(g_rl_buffer->get_buffer() + word_left, word_right - word_left);

    wstr<> win;
    to_utf16(win, in.c_str());

    // Do the environment variable expansion.
    DWORD size = ExpandEnvironmentStringsW(win.c_str(), 0, 0);
    if (!size)
        return 0;
    wstr<1024> wout;
    wout.reserve(size);
    size = ExpandEnvironmentStringsW(win.c_str(), wout.data(), wout.size());
    if (!size || size > wout.size())
        return 0;

    str<> out;
    to_utf8(out, wout.c_str());

    // Update Readline with the resulting expansion.
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(word_left, word_right);
    g_rl_buffer->set_cursor(word_left);
    g_rl_buffer->insert(out.c_str());
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
// Expands a doskey alias (but only the first line, if $T is present).
int clink_expand_doskey_alias(int count, int invoking_key)
{
    doskey_alias alias;
    doskey doskey("cmd.exe");
    doskey.resolve(g_rl_buffer->get_buffer(), alias);

    if (!alias)
    {
        rl_ding();
        return 0;
    }

    str<> expand;
    alias.next(expand);

    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, rl_end);
    rl_point = 0;
    if (!expand.empty())
        g_rl_buffer->insert(expand.c_str());
    g_rl_buffer->end_undo_group();

    return 0;
}

//------------------------------------------------------------------------------
int clink_up_directory(int count, int invoking_key)
{
    g_rl_buffer->begin_undo_group();
    g_rl_buffer->remove(0, ~0u);
    g_rl_buffer->insert(" cd ..");
    g_rl_buffer->end_undo_group();
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int clink_insert_dot_dot(int count, int invoking_key)
{
    str<> str;

    if (unsigned int cursor = g_rl_buffer->get_cursor())
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
int clink_scroll_line_up(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int clink_scroll_line_down(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYLINE);
    return 0;
}

//------------------------------------------------------------------------------
int clink_scroll_page_up(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int clink_scroll_page_down(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_BYPAGE);
    return 0;
}

//------------------------------------------------------------------------------
int clink_scroll_top(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), -1, SCR_TOEND);
    return 0;
}

//------------------------------------------------------------------------------
int clink_scroll_bottom(int count, int invoking_key)
{
    ScrollConsoleRelative(GetStdHandle(STD_OUTPUT_HANDLE), 1, SCR_TOEND);
    return 0;
}



//------------------------------------------------------------------------------
int clink_find_conhost(int count, int invoking_key)
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
int clink_mark_conhost(int count, int invoking_key)
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
    if (g_adjust_cursor_style.get())
    {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO info;
        GetConsoleCursorInfo(handle, &info);
        info.bVisible = true;
        SetConsoleCursorInfo(handle, &info);
    }

    // Invoke conhost's Mark command via the system menu.
    SendMessage(hwndConsole, WM_SYSCOMMAND, ID_CONSOLE_MARK, 0);
    return 0;
}



//------------------------------------------------------------------------------
extern const char** host_copy_dir_history(int* total);
int clink_popup_directories(int count, int invoking_key)
{
    // Copy the directory list (just a shallow copy of the dir pointers).
    int total = 0;
    const char** history = host_copy_dir_history(&total);
    if (!history || !total)
    {
        free(history);
        rl_ding();
        return 0;
    }

    // Popup list.
    str<> choice;
    int current = total - 1;
    popup_list_result result = do_popup_list("Directories",
        (const char **)history, total, 0, 0,
        false/*completing*/, false/*auto_complete*/, false/*reverse_find*/,
        current, choice);
    switch (result)
    {
    case popup_list_result::cancel:
        break;
    case popup_list_result::error:
        rl_ding();
        break;
    case popup_list_result::select:
    case popup_list_result::use:
        {
            bool use = (result == popup_list_result::use);
            rl_begin_undo_group();
            if (use)
            {
                rl_replace_line(history[current], 0);
                rl_point = rl_end;
                if (!history[current][0] ||
                    !path::is_separator(history[current][strlen(history[current]) - 1]))
                    rl_insert_text(PATH_SEP);
            }
            else
            {
                rl_insert_text(history[current]);
            }
            rl_end_undo_group();
            rl_redisplay();
            if (use)
                rl_newline(1, invoking_key);
        }
        break;
    }

    free(history);

    return 0;
}



//------------------------------------------------------------------------------
extern bool call_lua_rl_global_function(const char* func_name);

//------------------------------------------------------------------------------
int clink_complete_numbers(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_menu_complete_numbers(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._menu_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_menu_complete_numbers_backward(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._menu_complete_numbers_backward"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_old_menu_complete_numbers(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._old_menu_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_old_menu_complete_numbers_backward(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._old_menu_complete_numbers_backward"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_popup_complete_numbers(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._popup_complete_numbers"))
        rl_ding();
    return 0;
}

//------------------------------------------------------------------------------
int clink_popup_show_help(int count, int invoking_key)
{
    if (!call_lua_rl_global_function("clink._popup_show_help"))
        rl_ding();
    return 0;
}



//------------------------------------------------------------------------------
void cua_clear_selection()
{
    s_cua_anchor = -1;
}

//------------------------------------------------------------------------------
bool cua_point_in_selection(int in)
{
    if (s_cua_anchor < 0)
        return false;
    if (s_cua_anchor < rl_point)
        return (s_cua_anchor <= in && in < rl_point);
    else
        return (rl_point <= in && in < s_cua_anchor);
}

//------------------------------------------------------------------------------
int cua_selection_event_hook(int event)
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
        s_map.emplace(cua_backward_char);
        s_map.emplace(cua_forward_char);
        s_map.emplace(cua_backward_word);
        s_map.emplace(cua_forward_word);
        s_map.emplace(cua_beg_of_line);
        s_map.emplace(cua_end_of_line);
        s_map.emplace(cua_copy);
        s_map.emplace(cua_cut);

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
    }

    // If not a recognized command, clear the cua selection.
    if (s_map.find(rl_last_func) == s_map.end())
        cua_clear_selection();
}

//------------------------------------------------------------------------------
int cua_backward_char(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_backward_char(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_forward_char(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_forward_char(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_backward_word(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_backward_word(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_forward_word(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_forward_word(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_beg_of_line(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_beg_of_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_end_of_line(int count, int invoking_key)
{
    cua_selection_manager mgr;
    return rl_end_of_line(count, invoking_key);
}

//------------------------------------------------------------------------------
int cua_copy(int count, int invoking_key)
{
    if (g_rl_buffer)
    {
        bool has_sel = (s_cua_anchor >= 0);
        unsigned int len = g_rl_buffer->get_length();
        unsigned int beg = has_sel ? min<unsigned int>(len, s_cua_anchor) : 0;
        unsigned int end = has_sel ? min<unsigned int>(len, rl_point) : len;
        if (beg > end)
            SWAP(beg, end);
        if (beg < end)
            copy_impl(g_rl_buffer->get_buffer() + beg, end - beg);
    }
    return 0;
}

//------------------------------------------------------------------------------
int cua_cut(int count, int invoking_key)
{
    cua_copy(0, 0);
    cua_delete();
    return 0;
}
