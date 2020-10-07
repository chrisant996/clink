// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_buffer.h"
#include "rl_commands.h"

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/settings.h>

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
    "Setting this to a value >0 will make Clink strip CR and LF characters\n"
    "from text pasted into the current line. Set this to 'delete' to strip all\n"
    "newline characters to replace them with a space.",
    "delete,space",
    1);



//------------------------------------------------------------------------------
extern line_buffer* rl_buffer;

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
int clink_reset_line(int count, int invoking_key)
{
    using_history();
    rl_buffer->remove(0, rl_end);
    rl_point = 0;

    return 0;
}

//------------------------------------------------------------------------------
int clink_exit(int count, int invoking_key)
{
    clink_reset_line(1, 0);
    rl_buffer->insert("exit");
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
        rl_buffer->insert(utf8.c_str());
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
    copy_impl(rl_buffer->get_buffer(), rl_buffer->get_length());

    return 0;
}

//------------------------------------------------------------------------------
int clink_copy_cwd(int count, int invoking_key)
{
    wstr<270> cwd;
    unsigned int length = GetCurrentDirectoryW(cwd.size(), cwd.data());
    if (length < cwd.size())
    {
        cwd << L"\\";
        str<> tmp;
        to_utf8(tmp, cwd.c_str());
        path::normalise(tmp);
        copy_impl(tmp.c_str(), tmp.length());
    }

    return 0;
}

//------------------------------------------------------------------------------
int clink_up_directory(int count, int invoking_key)
{
    rl_buffer->begin_undo_group();
    rl_buffer->remove(0, ~0u);
    rl_buffer->insert(" cd ..");
    rl_buffer->end_undo_group();
    rl_newline(1, invoking_key);

    return 0;
}

//------------------------------------------------------------------------------
int clink_insert_dot_dot(int count, int invoking_key)
{
    str<> str;

    if (unsigned int cursor = rl_buffer->get_cursor())
    {
        char last_char = rl_buffer->get_buffer()[cursor - 1];
        if (last_char != ' ' && !path::is_separator(last_char))
            str << "\\";
    }

    str << "..\\";

    rl_buffer->insert(str.c_str());

    return 0;
}
