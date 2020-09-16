// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"
#include "rl_commands.h"
#include "line_buffer.h"

#include <core/base.h>
#include <core/log.h>
#include <terminal/ecma48_iter.h>
#include <terminal/printer.h>
#include <terminal/terminal_in.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/xmalloc.h>
}

//------------------------------------------------------------------------------
static FILE*        null_stream = (FILE*)1;
void                show_rl_help(printer&);
extern "C" int      wcwidth(int);
extern "C" char*    tgetstr(char*, char**);
static const int    RL_MORE_INPUT_STATES = ~(
                        RL_STATE_CALLBACK|
                        RL_STATE_INITIALIZED|
                        RL_STATE_OVERWRITE|
                        RL_STATE_VICMDONCE);

extern "C" {
extern void         (*rl_fwrite_function)(FILE*, const char*, int);
extern void         (*rl_fflush_function)(FILE*);
extern char*        _rl_comment_begin;
extern int          _rl_convert_meta_chars_to_ascii;
extern int          _rl_output_meta_chars;
#if defined(PLATFORM_WINDOWS)
extern int          _rl_vis_botlin;
extern int          _rl_last_c_pos;
extern int          _rl_last_v_pos;
#endif
} // extern "C"



//------------------------------------------------------------------------------
static void load_user_inputrc()
{
#if defined(PLATFORM_WINDOWS)
    // Remember to update clink_info() if anything changes in here.

    const char* env_vars[] = {
        "clink_inputrc",
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    for (const char* env_var : env_vars)
    {
        str<MAX_PATH> path;
        int path_length = GetEnvironmentVariable(env_var, path.data(), path.size());
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

    printer* pter = (printer*)stream;
    pter->print(chars, char_count);
}



//------------------------------------------------------------------------------
rl_module::rl_module(const char* shell_name)
: m_rl_buffer(nullptr)
, m_prev_group(-1)
{
    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = [] (FILE*) {};
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
#ifndef CLINK_CHRISANT_FIXES
    // No: disabling completion breaks `complete` and `menu-complete`.
    rl_completion_entry_function = [](const char*, int) -> char* { return nullptr; };
    rl_completion_display_matches_hook = [](char**, int, int) {};
#endif

    // Add commands.
    rl_add_funmap_entry("add-line-to-history", add_line_to_history);
#if 0
    rl_add_funmap_entry("remove-line-from-history", remove_line_from_history);
#endif

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
#ifdef CLINK_CHRISANT_MODS
        { "\\e[3;5~", "kill-word" },               // ctrl+del
        { "\\d",      "unix-word-rubout" },        // ctrl+backspace
#endif
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
void rl_module::on_begin_line(const context& context)
{
    rl_outstream = (FILE*)(terminal_out*)(&context.printer);

    // Readline needs to be told about parts of the prompt that aren't visible
    // by enclosing them in a pair of 0x01/0x02 chars.
    str<128> rl_prompt;

    ecma48_state state;
    ecma48_iter iter(context.prompt, state);
    while (const ecma48_code& code = iter.next())
    {
        bool c1 = (code.get_type() == ecma48_code::type_c1);
        if (c1) rl_prompt.concat("\x01", 1);
                rl_prompt.concat(code.get_pointer(), code.get_length());
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

    // This prevents any partial Readline state leaking from one line to the next
    rl_readline_state &= ~RL_MORE_INPUT_STATES;
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
        show_rl_help(context.printer);
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
    int is_inc_searching = rl_readline_state & RL_STATE_ISEARCH;
    unsigned int len = input.len;
    while (len && !m_done)
    {
        --len;
        rl_callback_read_char();

        // Internally Readline tries to resend escape characters but it doesn't
        // work with how Clink uses Readline. So we do it here instead.
        if (term_in.data[-1] == 0x1b && is_inc_searching)
        {
            --term_in.data;
            is_inc_searching = 0;
        }
    }

    if (m_done)
    {
        result.done(m_eof);
        return;
    }

    // Check if Readline want's more input or if we're done.
    if (rl_readline_state & RL_MORE_INPUT_STATES)
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
#if 1
    rl_reset_screen_size();
    rl_redisplay();
#else
    static int prev_columns = columns;

    int remaining = prev_columns;
    int line_count = 1;

    auto measure = [&] (const char* input, int length) {
        ecma48_state state;
        ecma48_iter iter(input, state, length);
        while (const ecma48_code& code = iter.next())
        {
            switch (code.get_type())
            {
            case ecma48_code::type_chars:
                for (str_iter i(code.get_pointer(), code.get_length()); i.more(); )
                {
                    int n = wcwidth(i.next());
                    remaining -= n;
                    if (remaining > 0)
                        continue;

                    ++line_count;

                    remaining = prev_columns - ((remaining < 0) << 1);
                }
                break;

            case ecma48_code::type_c0:
                switch (code.get_code())
                {
                case ecma48_code::c0_lf:
                    ++line_count;
                    /* fallthrough */

                case ecma48_code::c0_cr:
                    remaining = prev_columns;
                    break;

                case ecma48_code::c0_ht:
                    if (int n = 8 - ((prev_columns - remaining) & 7))
                        remaining = max(remaining - n, 0);
                    break;

                case ecma48_code::c0_bs:
                    remaining = min(remaining + 1, prev_columns); // doesn't consider full-width
                    break;
                }
                break;
            }
        }
    };

    measure(context.prompt, -1);

    const line_buffer& buffer = context.buffer;
    const char* buffer_ptr = buffer.get_buffer();
    measure(buffer_ptr, buffer.get_cursor());
    int cursor_line = line_count - 1;

    buffer_ptr += buffer.get_cursor();
    measure(buffer_ptr, -1);

    static const char* const termcap_up    = tgetstr("ku", nullptr);
    static const char* const termcap_down  = tgetstr("kd", nullptr);
    static const char* const termcap_cr    = tgetstr("cr", nullptr);
    static const char* const termcap_clear = tgetstr("ce", nullptr);

    auto& printer = context.printer;

    // Move cursor to bottom line.
    for (int i = line_count - cursor_line; --i;)
        printer.print(termcap_down, 64);

    printer.print(termcap_cr, 64);
    do
    {
        printer.print(termcap_clear, 64);

        if (--line_count)
            printer.print(termcap_up, 64);
    }
    while (line_count);

    printer.print(context.prompt, 1024);
    printer.print(buffer.get_buffer(), 1024);

    prev_columns = columns;
#endif
}
