// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_backend.h"

#include <terminal/terminal.h>

//------------------------------------------------------------------------------
rl_backend::rl_backend(const char* shell_name)
{
    rl_readline_name = shell_name;
}

//------------------------------------------------------------------------------
void rl_backend::bind(binder& binder)
{
}

//------------------------------------------------------------------------------
void rl_backend::begin_line(const char* prompt, const context& context)
{
    auto handler = [] (char* line) { rl_backend::get()->done(line); };
    rl_callback_handler_install("testbed $ ", handler);

    m_need_draw = false;
    m_done = false;
    m_eof = false;
}

//------------------------------------------------------------------------------
void rl_backend::end_line()
{
}

//------------------------------------------------------------------------------
void rl_backend::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
editor_backend::result rl_backend::on_input(
    const char* keys,
    int id,
    const context& context)
{
    // MODE4 : should wrap all external line edits in single undo.

    static char more_input_id = -1;
    if (char(id) != more_input_id)
        return result::next;

    // MODE4
    static struct : public terminal_in
    {
        virtual void select() {}
        virtual int read() { return *(unsigned char*)(data++); }
        const char* data; 
    } term_in;
    term_in.data = keys;
    rl_instream = (FILE*)(&term_in);

    while (*term_in.data)
    // MODE4

    rl_callback_read_char();

    int rl_state = rl_readline_state;
    rl_state &= ~RL_STATE_CALLBACK;
    rl_state &= ~RL_STATE_INITIALIZED;
    rl_state &= ~RL_STATE_OVERWRITE;
    rl_state &= ~RL_STATE_VICMDONCE;

    if (m_done)
        return result::done;

    if (rl_state)
        return {result::more_input, more_input_id};

    return result::next;
}

//------------------------------------------------------------------------------
const char* rl_backend::get_buffer() const
{
    return (m_eof ? nullptr : rl_line_buffer);
}

//------------------------------------------------------------------------------
unsigned int rl_backend::get_cursor() const
{
    return rl_point;
}

//------------------------------------------------------------------------------
unsigned int rl_backend::set_cursor(unsigned int pos)
{
    return (pos <= rl_end) ? rl_point = pos : rl_point = rl_end;
}

//------------------------------------------------------------------------------
bool rl_backend::insert(const char* text)
{
    return (m_need_draw = (text[rl_insert_text(text)] == '\0'));
}

//------------------------------------------------------------------------------
bool rl_backend::remove(unsigned int from, unsigned int to)
{
    return (m_need_draw = !!rl_delete_text(from, to));
}

//------------------------------------------------------------------------------
void rl_backend::draw()
{
    m_need_draw ? rl_redisplay() : nullptr;
}

//------------------------------------------------------------------------------
void rl_backend::redraw()
{
    rl_forced_update_display();
}

//------------------------------------------------------------------------------
void rl_backend::done(const char* line)
{
    m_done = true;
    m_eof = (line == nullptr);

    rl_callback_handler_remove();
}
