// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_backend.h"

#include <core/base.h>
#include <terminal/ecma48_iter.h>
#include <terminal/terminal.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/xmalloc.h>
}

//------------------------------------------------------------------------------
static FILE*    null_stream = (FILE*)1;

extern "C" {
extern void     (*rl_fwrite_function)(FILE*, const char*, int);
extern void     (*rl_fflush_function)(FILE*);
extern char*    _rl_comment_begin;
extern int      _rl_convert_meta_chars_to_ascii;
extern int      _rl_output_meta_chars;
} // extern "C"



//------------------------------------------------------------------------------
enum {
    bind_id_input,
    bind_id_more_input,
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
rl_backend::rl_backend(const char* shell_name)
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
        { "\\eOD", "backward-word" },           // ctrl-left
        { "\\eOC", "forward-word" },            // ctrl-right
        { "\\e[4", "end-of-line" },             // end
        { "\\e[1", "beginning-of-line" },       // home
        { "\\e[3", "delete-char" },             // del
        { "\\eO4", "kill-line" },               // ctrl-end
        { "\\eO1", "backward-kill-line" },      // ctrl-home
        { "\\e[5", "history-search-backward" }, // pgup
        { "\\e[6", "history-search-forward" },  // pgdn
    };

    for (int i = 0; i < sizeof_array(ext_key_binds); ++i)
        rl_bind_keyseq(ext_key_binds[i][0], rl_named_function(ext_key_binds[i][1]));
}

//------------------------------------------------------------------------------
void rl_backend::bind_input(binder& binder)
{
    int default_group = binder.get_group();
    binder.bind(default_group, "", bind_id_input);

    m_catch_group = binder.create_group("readline");
    binder.bind(m_catch_group, "", bind_id_more_input);
}

//------------------------------------------------------------------------------
void rl_backend::on_begin_line(const char* prompt, const context& context)
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

    auto handler = [] (char* line) { rl_backend::get()->done(line); };
    rl_callback_handler_install(rl_prompt.c_str(), handler);

    m_done = false;
    m_eof = false;
    m_prev_group = -1;
}

//------------------------------------------------------------------------------
void rl_backend::on_end_line()
{
    if (m_rl_buffer != nullptr)
    {
        rl_line_buffer = m_rl_buffer;
        m_rl_buffer = nullptr;
    }
}

//------------------------------------------------------------------------------
void rl_backend::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
void rl_backend::on_input(const input& input, result& result, const context& context)
{
    // MODE4 : should wrap all external line edits in single undo.

    // Setup the terminal.
    struct : public terminal_in
    {
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
void rl_backend::done(const char* line)
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
