// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "core/str.h"

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
int                 get_clink_setting_int(const char*);
void                enter_scroll_mode(int);
int                 show_rl_help(int, int);

//------------------------------------------------------------------------------
static void clear_line()
{
    using_history();
    rl_delete_text(0, rl_end);
    rl_point = 0;
}

//------------------------------------------------------------------------------
int ctrl_c(int count, int invoking_key)
{
    DWORD mode;

    clear_line();
    rl_crlf();
    rl_done = 1;

    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode))
    {
        if (mode & ENABLE_PROCESSED_INPUT)
        {
            // Fire a Ctrl-C event and stop Readline. ReadConsole would also
            // set error 0x3e3 (ERROR_OPERATION_ABORTED) too.
            GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
            Sleep(5);

            SetLastError(0x3e3);
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static void strip_crlf(char* line)
{
    char* read;
    char* write;
    int prev_was_crlf;
    int setting;

    setting = get_clink_setting_int("strip_crlf_on_paste");
    if (setting <= 0)
    {
        return;
    }

    read = write = line;
    while (*read)
    {
        char c = *read;
        if (c != '\n' && c != '\r')
        {
            prev_was_crlf = 0;
            *write = c;
            ++write;
        }
        else if (setting > 1 && !prev_was_crlf)
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
int paste_from_clipboard(int count, int invoking_key)
{
    if (OpenClipboard(nullptr) != FALSE)
    {
        HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
        if (clip_data != nullptr)
        {
            wchar_t* from_clipboard = (wchar_t*)clip_data;
            char utf8[1024];

            WideCharToMultiByte(
                CP_UTF8, 0,
                from_clipboard, -1,
                utf8, sizeof(utf8),
                nullptr, nullptr
            );
            utf8[sizeof(utf8) - 1] = '\0';

            strip_crlf(utf8);
            rl_insert_text(utf8);
        }

        CloseClipboard();
    }

    return 0;
}

//------------------------------------------------------------------------------
int copy_line_to_clipboard(int count, int invoking_key)
{
    HGLOBAL mem;
    wchar_t* data;
    int size;

    size = int(strlen(rl_line_buffer) + 1) * sizeof(wchar_t);
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem != nullptr)
    {
        data = (wchar_t*)GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, rl_line_buffer, -1, data, size);
        GlobalUnlock(mem);

        if (OpenClipboard(nullptr) != FALSE)
        {
            SetClipboardData(CF_TEXT, nullptr);
            SetClipboardData(CF_UNICODETEXT, mem);
            CloseClipboard();
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
int up_directory(int count, int invoking_key)
{
    rl_begin_undo_group();
    rl_delete_text(0, rl_end);
    rl_point = 0;
    rl_insert_text("cd ..");
    rl_end_undo_group();
    rl_done = 1;

    return 0;
}

//------------------------------------------------------------------------------
static void get_word_bounds(const char* str, int cursor, int* left, int* right)
{
    int i;
    int delim;
    const char* post;

    // Determine the word delimiter depending on whether the word's quoted.
    delim = 0;
    for (i = 0; i < cursor; ++i)
    {
        char c = str[i];
        delim += (c == '\"');
    }

    // Search outwards from the cursor for the delimiter.
    delim = (delim & 1) ? '\"' : ' ';
    *left = 0;
    for (i = cursor - 1; i >= 0; --i)
    {
        char c = str[i];
        if (c == delim)
        {
            *left = i + 1;
            break;
        }
    }

    post = strchr(str + cursor, delim);
    if (post != nullptr)
    {
        *right = int(post - str);
    }
    else
    {
        *right = int(strlen(str));
    }
}

//------------------------------------------------------------------------------
int expand_env_vars(int count, int invoking_key)
{
    // Extract the word under the cursor.
    int word_left, word_right;
    get_word_bounds(rl_line_buffer, rl_point, &word_left, &word_right);

    str<1024> in;
    in << (rl_line_buffer + word_left);
    in.truncate(word_right - word_left);

    // Do the environment variable expansion.
    str<1024> out;
    if (!ExpandEnvironmentStrings(in.c_str(), out.data(), out.size()))
        return 0;

    // Update Readline with the resulting expansion.
    rl_begin_undo_group();
    rl_delete_text(word_left, word_right);
    rl_point = word_left;
    rl_insert_text(out.c_str());
    rl_end_undo_group();

    return 0;
}
