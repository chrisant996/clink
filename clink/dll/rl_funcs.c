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
    if (get_clink_setting_int("passthrough_ctrlc"))
    {
        rl_line_buffer[0] = '\x03';
        rl_line_buffer[1] = '\x00';
        rl_point = 1;
        rl_end = 1;
        rl_done = 1;
    }
    else
    {
        clear_line();
    }

    return 0;
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
void clink_register_rl_funcs()
{
    rl_add_funmap_entry("ctrl-c", ctrl_c);
    rl_add_funmap_entry("paste-from-clipboard", paste_from_clipboard);
    rl_add_funmap_entry("page-up", page_up);
    rl_add_funmap_entry("up-directory", up_directory);
    rl_add_funmap_entry("show-rl-help", show_rl_help);
    rl_add_funmap_entry("copy-line-to-clipboard", copy_line_to_clipboard);
}
