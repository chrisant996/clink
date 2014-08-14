/* Copyright (c) 2013 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
void                enter_scroll_mode(int);
int                 show_rl_help(int, int);
int                 get_clink_setting_int(const char*);

//------------------------------------------------------------------------------
static void clear_line()
{
    using_history();
    rl_delete_text(0, rl_end);
    rl_point = 0;
}

//------------------------------------------------------------------------------
static int ctrl_c(int count, int invoking_key)
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
static int paste_from_clipboard(int count, int invoking_key)
{
    if (OpenClipboard(NULL) != FALSE)
    {
        HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
        if (clip_data != NULL)
        {
            wchar_t* from_clipboard = (wchar_t*)clip_data;
            char utf8[1024];

            WideCharToMultiByte(
                CP_UTF8, 0,
                from_clipboard, -1,
                utf8, sizeof(utf8),
                NULL, NULL
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
static int copy_line_to_clipboard(int count, int invoking_key)
{
    HGLOBAL mem;
    void* data;
    int size;

    size = (strlen(rl_line_buffer) + 1) * sizeof(wchar_t);
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem != NULL)
    {
        data = GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, rl_line_buffer, -1, data, size);
        GlobalUnlock(mem);

        if (OpenClipboard(NULL) != FALSE)
        {
            SetClipboardData(CF_TEXT, NULL);
            SetClipboardData(CF_UNICODETEXT, mem);
            CloseClipboard();
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static int up_directory(int count, int invoking_key)
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
static int page_up(int count, int invoking_key)
{
    enter_scroll_mode(-1);
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
    if (post != NULL)
    {
        *right = post - str;
    }
    else
    {
        *right = strlen(str);
    }
}

//------------------------------------------------------------------------------
static int expand_env_vars(int count, int invoking_key)
{
    static const int buffer_size = 0x8000;
    char* in;
    char* out;
    int word_left, word_right;

    // Create some buffers to work in.
    out = malloc(buffer_size * 2);
    in = out + buffer_size;

    // Extract the word under the cursor.
    str_cpy(in, rl_line_buffer, buffer_size);
    get_word_bounds(in, rl_point, &word_left, &word_right);
    in[word_right] = '\0';
    in += word_left;

    // Do the environment variable expansion.
    if (!ExpandEnvironmentStrings(in, out, buffer_size))
    {
        return 0;
    }

    // Update Readline with the resulting expansion.
    rl_begin_undo_group();
    rl_delete_text(word_left, word_right);
    rl_point = word_left;
    rl_insert_text(out);
    rl_end_undo_group();

    free(out);
    return 0;
}

//------------------------------------------------------------------------------
void clink_register_rl_funcs()
{
    rl_add_funmap_entry("ctrl-c", ctrl_c);
    rl_add_funmap_entry("paste-from-clipboard", paste_from_clipboard);
    rl_add_funmap_entry("page-up", page_up);
    rl_add_funmap_entry("up-directory", up_directory);
    rl_add_funmap_entry("show-rl-help", show_rl_help);
    rl_add_funmap_entry("copy-line-to-clipboard", copy_line_to_clipboard);
    rl_add_funmap_entry("expand-env-vars", expand_env_vars);
}

// vim: expandtab
