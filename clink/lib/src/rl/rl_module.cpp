// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"

#include <core/base.h>
#include <core/log.h>
#include <terminal/ecma48_iter.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/xmalloc.h>
}

//------------------------------------------------------------------------------
static FILE*    null_stream = (FILE*)1;
void            show_rl_help(terminal_out&);

extern "C" {
extern void     (*rl_fwrite_function)(FILE*, const char*, int);
extern void     (*rl_fflush_function)(FILE*);
extern char*    _rl_comment_begin;
extern int      _rl_convert_meta_chars_to_ascii;
extern int      _rl_output_meta_chars;
#if defined(PLATFORM_WINDOWS)
extern int      _rl_vis_botlin;
extern int      _rl_last_c_pos;
extern int      _rl_last_v_pos;
#endif
} // extern "C"



//------------------------------------------------------------------------------
static void load_user_inputrc()
{
#if defined(PLATFORM_WINDOWS)
    const char* env_vars[] = {
        "clink_inputrc",
        "localappdata",
        "appdata",
        "userprofile",
        "home",
    };

    for (int i = 0; i < sizeof_array(env_vars); ++i)
    {
        str<MAX_PATH> path;
        int path_length = GetEnvironmentVariable(env_vars[i], path.data(), path.size());
        if (!path_length || path_length > int(path.size()))
            continue;

        path << "\\.inputrc";

        for (int j = 0; j < 2; ++j)
        {
            if (!rl_read_init_file(path.c_str()))
            {
                LOG("Found Readline inputrc at '%s'", path.c_str());
                break;
            }

            int dot = path.last_of('.');
            if (dot >= 0)
                path.data()[dot] = '_';
        }
    }
#endif // PLATFORM_WINDOWS
}



//------------------------------------------------------------------------------
enum {
    bind_id_input,
    bind_id_more_input,
    bind_id_rl_help,
};



//------------------------------------------------------------------------------
static int terminal_read_thunk(FILE* stream)
{
    if (stream == null_stream)
        return 0;

    terminal_in* term = (terminal_in*)stream;
    return term->read();
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const char* chars, int char_count)
{
    if (stream == stderr || stream == null_stream)
        return;

    terminal_out* term = (terminal_out*)stream;
    return term->write(chars, char_count);
}

//------------------------------------------------------------------------------
static void terminal_flush_thunk(FILE* stream)
{
    if (stream == stderr || stream == null_stream)
        return;

    terminal_out* term = (terminal_out*)stream;
    return term->flush();
}



//------------------------------------------------------------------------------
rl_module::rl_module(const char* shell_name)
: m_rl_buffer(nullptr)
, m_prev_group(-1)
{
    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = terminal_flush_thunk;
    rl_instream = null_stream;
    rl_outstream = null_stream;

    rl_readline_name = shell_name;
    rl_catch_signals = 0;
    _rl_comment_begin = savestring("::"); // this will do...

    // Readline needs a tweak of it's handling of 'meta' (i.e. IO bytes >=0x80)
    // so that it handles UTF-8 correctly (convert=input, output=output)
    _rl_convert_meta_chars_to_ascii = 0;
    _rl_output_meta_chars = 1;

    // Disable completion and match display.
    rl_completion_entry_function = [](const char*, int) -> char* { return nullptr; };
    rl_completion_display_matches_hook = [](char**, int, int) {};

    // Bind extended keys so editing follows Windows' conventions.
    static const char* ext_key_binds[][2] = {
        { "\\e[1;5D", "backward-word" },           // ctrl-left
        { "\\e[1;5C", "forward-word" },            // ctrl-right
        { "\\e[F",    "end-of-line" },             // end
        { "\\e[H",    "beginning-of-line" },       // home
        { "\\e[3~",   "delete-char" },             // del
        { "\\e[1;5F", "kill-line" },               // ctrl-end
        { "\\e[1;5H", "backward-kill-line" },      // ctrl-home
        { "\\e[5~",   "history-search-backward" }, // pgup
        { "\\e[6~",   "history-search-forward" },  // pgdn
        { "\\C-z",    "undo" },
    };

    for (int i = 0; i < sizeof_array(ext_key_binds); ++i)
        rl_bind_keyseq(ext_key_binds[i][0], rl_named_function(ext_key_binds[i][1]));

    load_user_inputrc();
}

//------------------------------------------------------------------------------
rl_module::~rl_module()
{
    free(_rl_comment_begin);
    _rl_comment_begin = nullptr;
}

//------------------------------------------------------------------------------
void rl_module::bind_input(binder& binder)
{
    int default_group = binder.get_group();
    binder.bind(default_group, "", bind_id_input);
    binder.bind(default_group, "\\M-h", bind_id_rl_help);

    m_catch_group = binder.create_group("readline");
    binder.bind(m_catch_group, "", bind_id_more_input);
}

//------------------------------------------------------------------------------
void rl_module::on_begin_line(const char* prompt, const context& context)
{
    rl_outstream = (FILE*)(terminal_out*)(&context.terminal);

    // Readline needs to be told about parts of the prompt that aren't visible
    // by enclosing them in a pair of 0x01/0x02 chars.
    str<128> rl_prompt;

    ecma48_state state;
    ecma48_iter iter(prompt, state);
    while (const ecma48_code* code = iter.next())
    {
        bool c1 = (code->get_type() == ecma48_code::type_c1);
        if (c1) rl_prompt.concat("\x01", 1);
                rl_prompt.concat(code->get_pointer(), code->get_length());
        if (c1) rl_prompt.concat("\x02", 1);
    }

    auto handler = [] (char* line) { rl_module::get()->done(line); };
    rl_callback_handler_install(rl_prompt.c_str(), handler);

    m_done = false;
    m_eof = false;
    m_prev_group = -1;
}

//------------------------------------------------------------------------------
void rl_module::on_end_line()
{
    if (m_rl_buffer != nullptr)
    {
        rl_line_buffer = m_rl_buffer;
        m_rl_buffer = nullptr;
    }
}

//------------------------------------------------------------------------------
void rl_module::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
void rl_module::on_input(const input& input, result& result, const context& context)
{
    if (input.id == bind_id_rl_help)
    {
        show_rl_help(context.terminal);
        result.redraw();
        return;
    }

    // Setup the terminal.
    struct : public terminal_in
    {
        virtual void begin() override   {}
        virtual void end() override     {}
        virtual void select() override  {}
        virtual int  read() override    { return *(unsigned char*)(data++); }
        const char*  data; 
    } term_in;
             
    term_in.data = input.keys;

    rl_instream = (FILE*)(&term_in);

    // Call Readline's until there's no characters left.
    while (*term_in.data && !m_done)
        rl_callback_read_char();

    if (m_done)
    {
        result.done(m_eof);
        return;
    }

    // Check if Readline want's more input or if we're done.
    int rl_state = rl_readline_state;
    rl_state &= ~RL_STATE_CALLBACK;
    rl_state &= ~RL_STATE_INITIALIZED;
    rl_state &= ~RL_STATE_OVERWRITE;
    rl_state &= ~RL_STATE_VICMDONCE;
    if (rl_state)
    {
        if (m_prev_group < 0)
            m_prev_group = result.set_bind_group(m_catch_group);
    }
    else if (m_prev_group >= 0)
    {
        result.set_bind_group(m_prev_group);
        m_prev_group = -1;
    }
}

//------------------------------------------------------------------------------
void rl_module::done(const char* line)
{
    m_done = true;
    m_eof = (line == nullptr);

    // Readline will reset the line state on returning from this call. Here we
    // trick it into reseting something else so we can use rl_line_buffer later.
    static char dummy_buffer = 0;
    m_rl_buffer = rl_line_buffer;
    rl_line_buffer = &dummy_buffer;

    rl_callback_handler_remove();
}

//------------------------------------------------------------------------------
void rl_module::on_terminal_resize(int columns, int rows, const context& context)
{
#if defined(PLATFORM_WINDOWS)
    // The gymnastics below shouldn't really be necessary. The underlying code
    // dealing with terminals should instead take care of cursors and the like.

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD cursor_pos;
    HANDLE handle;
    int cell_count;
    DWORD written;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);

    // If the new buffer size has clipped the cursor, conhost will move it down
    // one line. Readline on the other hand thinks that the cursor's row remains
    // unchanged.
    cursor_pos.X = 0;
    cursor_pos.Y = csbi.dwCursorPosition.Y;
    if (_rl_last_c_pos >= csbi.dwSize.X && cursor_pos.Y > 0)
        --cursor_pos.Y;

    SetConsoleCursorPosition(handle, cursor_pos);

    // Readline only clears the last row. If a resize causes a line to now occupy
    // two or more fewer lines that it did previous it will leave display artefacts.
    if (_rl_vis_botlin)
    {
        // _rl_last_v_pos is vertical offset of cursor from first line.
        if (_rl_last_v_pos > 0)
            cursor_pos.Y -= _rl_last_v_pos - 1; // '- 1' so we're line below first.

        cell_count = csbi.dwSize.X * _rl_vis_botlin;

        FillConsoleOutputCharacterW(handle, ' ', cell_count, cursor_pos, &written);
        FillConsoleOutputAttribute(handle, csbi.wAttributes, cell_count, cursor_pos,
            &written);
    }

    // Tell Readline the buffer's resized, but make sure we don't use Clink's
    // redisplay path as then Readline won't redisplay multiline prompts correctly.
    {
        rl_voidfunc_t* old_redisplay = rl_redisplay_function;
        rl_redisplay_function = rl_redisplay;

        rl_resize_terminal();

        rl_redisplay_function = old_redisplay;
    }
#endif // PLATFORM_WINDOWS
}
