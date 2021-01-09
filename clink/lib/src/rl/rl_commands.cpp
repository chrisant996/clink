// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "line_state.h"
#include "popup.h"
#include "editor_module.h"
#include "rl_commands.h"

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/settings.h>
#include <terminal/printer.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/history.h>
#include <readline/xmalloc.h>
}



//------------------------------------------------------------------------------
static setting_enum g_paste_crlf(
    "clink.paste_crlf",
    "Strips CR and LF chars on paste",
    "Setting this to 'space' makes Clink strip CR and LF characters from text\n"
    "pasted into the current line. Set this to 'delete' to strip all newline\n"
    "characters to replace them with a space.",
    "delete,space",
    1);



//------------------------------------------------------------------------------
extern line_buffer* g_rl_buffer;
extern bool s_force_reload_scripts;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
static void write_line_feed()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(handle, L"\n", 1, &written, nullptr);
}

//------------------------------------------------------------------------------
static void strip_crlf(char* line)
{
    int setting = g_paste_crlf.get();

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
        else if (setting > 0 && !prev_was_crlf)
        {
            prev_was_crlf = 1;
            *write = ' ';
            ++write;
        }

        ++read;
    }

    *write = '\0';
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

    HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
    if (clip_data != nullptr)
    {
        str<1024> utf8;
        to_utf8(utf8, (wchar_t*)clip_data);

        strip_crlf(utf8.data());
        g_rl_buffer->insert(utf8.c_str());
    }

    CloseClipboard();

    return 0;
}

//------------------------------------------------------------------------------
static void copy_impl(const char* value, int length)
{
    int size = (length + 4) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem == nullptr)
        return;

    wchar_t* data = (wchar_t*)GlobalLock(mem);
    length = to_utf16((wchar_t*)data, length + 1, value);
    GlobalUnlock(mem);

    if (OpenClipboard(nullptr) == FALSE)
        return;

    SetClipboardData(CF_TEXT, nullptr);
    SetClipboardData(CF_UNICODETEXT, mem);
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
    std::vector<word> words;
    g_rl_buffer->collect_words(words, collect_words_mode::whole_command);

    if (!words.empty())
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

    rl_ding();
    return 0;
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
enum SCRMODE
{
    SCR_BYLINE,
    SCR_BYPAGE,
    SCR_TOEND,
};

//------------------------------------------------------------------------------
extern "C" int _rl_vis_botlin;
extern "C" int _rl_last_v_pos;

//------------------------------------------------------------------------------
int ScrollConsoleRelative(HANDLE h, int direction, SCRMODE mode)
{
    // Get the current screen buffer window position.
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    // Calculate the bottom line of the readline edit line.
    SHORT bottom_Y = csbiInfo.dwCursorPosition.Y + (_rl_vis_botlin - _rl_last_v_pos);

    // Calculate the new window position.
    SMALL_RECT srWindow = csbiInfo.srWindow;
    SHORT cy = srWindow.Bottom - srWindow.Top;
    if (mode == SCR_TOEND)
    {
        if (direction <= 0)
            srWindow.Top = srWindow.Bottom = -1;
        else
            srWindow.Bottom = csbiInfo.dwSize.Y;
    }
    else
    {
        if (mode == SCR_BYPAGE)
            direction *= csbiInfo.srWindow.Bottom - csbiInfo.srWindow.Top;
        srWindow.Top += (SHORT)direction;
        srWindow.Bottom += (SHORT)direction;
    }

    // Check whether the window is too close to the screen buffer top.
    if (srWindow.Bottom >= bottom_Y)
    {
        srWindow.Bottom = bottom_Y;
        srWindow.Top = srWindow.Bottom - cy;
    }
    if (srWindow.Top < 0)
    {
        srWindow.Top = 0;
        srWindow.Bottom = srWindow.Top + cy;
    }

    // Set the new window position.
    if (srWindow.Top == csbiInfo.srWindow.Top ||
        !SetConsoleWindowInfo(h, TRUE/*fAbsolute*/,  &srWindow))
        return 0;

    // Tell printer so it can work around a problem with WriteConsoleW.
    set_scrolled_screen_buffer();

    return srWindow.Top - csbiInfo.srWindow.Top;
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
        false/*completing*/, false/*auto_complete*/, current, choice);
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
bool is_scroll_mode()
{
    return (rl_last_func == clink_scroll_line_up ||
            rl_last_func == clink_scroll_line_down ||
            rl_last_func == clink_scroll_page_up ||
            rl_last_func == clink_scroll_page_down ||
            rl_last_func == clink_scroll_top ||
            rl_last_func == clink_scroll_bottom);
}
