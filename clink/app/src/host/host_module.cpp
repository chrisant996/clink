// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_module.h"

#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <lib/line_buffer.h>
#include <terminal/terminal_out.h>

//------------------------------------------------------------------------------
static setting_enum g_paste_crlf(
    "clink.paste_crlf",
    "Strips CR and LF chars on paste",
    "Setting this to a value >0 will make Clink strip CR and LF characters\n"
    "from text pasted into the current line. Set this to 'delete' to strip all\n"
    "newline characters to replace them with a space.",
    "delete,space",
    1);

static setting_str g_key_paste(
    "keybind.paste",
    "Inserts clipboard contents",
    "^v");

static setting_str g_key_ctrlc(
    "keybind.ctrlc",
    "Cancels current line edit",
    "^c");

static setting_str g_key_copy_line(
    "keybind.copy_line",
    "Copies line to clipboard",
    "\\M-C-c");

static setting_str g_key_copy_cwd(
    "keybind.copy_cwd",
    "Copies current directory",
    "\\M-C");

static setting_str g_key_up_dir(
    "keybind.up_dir",
    "Goes up a directory",
    "\\eO5");

static setting_str g_key_dotdot(
    "keybind.dotdot",
    "Inserts ..\\",
    "\\M-a");

static setting_str g_key_expand_env(
    "keybind.expand_env",
    "Expands %envvar% under cursor",
    "\\M-C-e");



//------------------------------------------------------------------------------
static void ctrl_c(
    editor_module::result& result,
    const editor_module::context& context)
{
    //auto& buffer = context.buffer;
    context.buffer.remove(0, ~0u);
    context.terminal.write("\n^C\n", 4);
    result.redraw();

#if 0
    DWORD mode;
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
#endif // 0
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
static void paste(line_buffer& buffer)
{
    if (OpenClipboard(nullptr) == FALSE)
        return;

    HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
    if (clip_data != nullptr)
    {
        str<1024> utf8;
        to_utf8(utf8, (wchar_t*)clip_data);

        strip_crlf(utf8.data());
        buffer.insert(utf8.c_str());
    }

    CloseClipboard();
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
static void copy_line(const line_buffer& buffer)
{
    copy_impl(buffer.get_buffer(), buffer.get_length());
}

//------------------------------------------------------------------------------
static void copy_cwd(const line_buffer& buffer)
{
    str<270> cwd;
    unsigned int length = GetCurrentDirectory(cwd.size(), cwd.data());
    if (length < cwd.size())
    {
        cwd << "\\";
        path::clean(cwd);
        copy_impl(cwd.c_str(), cwd.length());
    }
}

//------------------------------------------------------------------------------
static void up_directory(editor_module::result& result, line_buffer& buffer)
{
    buffer.begin_undo_group();
    buffer.remove(0, ~0u);
    buffer.insert(" cd ..");
    buffer.end_undo_group();
    result.done();
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
static void expand_env_vars(line_buffer& buffer)
{
    // Extract the word under the cursor.
    int word_left, word_right;
    get_word_bounds(buffer, &word_left, &word_right);

    str<1024> in;
    in << (buffer.get_buffer() + word_left);
    in.truncate(word_right - word_left);

    // Do the environment variable expansion.
    str<1024> out;
    if (!ExpandEnvironmentStrings(in.c_str(), out.data(), out.size()))
        return;

    // Update Readline with the resulting expansion.
    buffer.begin_undo_group();
    buffer.remove(word_left, word_right);
    buffer.set_cursor(word_left);
    buffer.insert(out.c_str());
    buffer.end_undo_group();
}

//------------------------------------------------------------------------------
static void insert_dot_dot(line_buffer& buffer)
{
    if (unsigned int cursor = buffer.get_cursor())
    {
        char last_char = buffer.get_buffer()[cursor - 1];
        if (last_char != ' ' && !path::is_separator(last_char))
            buffer.insert("\\");
    }

    buffer.insert("..\\");
}



//------------------------------------------------------------------------------
enum
{
    bind_id_paste,
    bind_id_ctrlc,
    bind_id_copy_line,
    bind_id_copy_cwd,
    bind_id_up_dir,
    bind_id_expand_env,
    bind_id_dotdot,
};



//------------------------------------------------------------------------------
host_module::host_module(const char* host_name)
: m_host_name(host_name)
{
}

//------------------------------------------------------------------------------
void host_module::bind_input(binder& binder)
{
    int default_group = binder.get_group();
    binder.bind(default_group, g_key_paste.get(), bind_id_paste);
    binder.bind(default_group, g_key_ctrlc.get(), bind_id_ctrlc);
    binder.bind(default_group, g_key_copy_line.get(), bind_id_copy_line);
    binder.bind(default_group, g_key_copy_cwd.get(), bind_id_copy_cwd);
    binder.bind(default_group, g_key_up_dir.get(), bind_id_up_dir);
    binder.bind(default_group, g_key_dotdot.get(), bind_id_dotdot);

    if (stricmp(m_host_name, "cmd.exe") == 0)
        binder.bind(default_group, g_key_expand_env.get(), bind_id_expand_env);
}

//------------------------------------------------------------------------------
void host_module::on_begin_line(const char* prompt, const context& context)
{
}

//------------------------------------------------------------------------------
void host_module::on_end_line()
{
}

//------------------------------------------------------------------------------
void host_module::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
void host_module::on_input(const input& input, result& result, const context& context)
{
    switch (input.id)
    {
    case bind_id_paste:         paste(context.buffer);                break;
    case bind_id_ctrlc:         ctrl_c(result, context);              break;
    case bind_id_copy_line:     copy_line(context.buffer);            break;
    case bind_id_copy_cwd:      copy_cwd(context.buffer);             break;
    case bind_id_up_dir:        up_directory(result, context.buffer); break;
    case bind_id_expand_env:    expand_env_vars(context.buffer);      break;
    case bind_id_dotdot:        insert_dot_dot(context.buffer);       break;
    };
}

//------------------------------------------------------------------------------
void host_module::on_terminal_resize(int columns, int rows, const context& context)
{
}
