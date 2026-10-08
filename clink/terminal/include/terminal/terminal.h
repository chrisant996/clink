// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>

#include <tib.h>

class screen_buffer;
class terminal_in;
class terminal_out;
class input_idle;
class tib_terminal_bridge;

//------------------------------------------------------------------------------
enum class bell_preference { none, visible, audible };

//------------------------------------------------------------------------------
extern tib_terminal_bridge* g_terminal;
extern uint32 g_ambiguous_keyseq_timeout;
extern bell_preference g_bell_preference;
extern bool g_debug_log_input_pipeline;

//------------------------------------------------------------------------------
bool                init_terminal();
bool                init_terminal(bool cursor_visibility);
bool                init_terminal(terminal_in* in, terminal_out* out);
bool                uninit_terminal();
void                terminal_discover_config(terminal_in* in);
bool                terminal_has_synchronize_output();

//------------------------------------------------------------------------------
const char*         find_key_name(const char* keyseq, int32& len, int32& eqclass, int32& order);

//------------------------------------------------------------------------------
void                set_verbose_input(int32 verbose); // 1 = inline, 2 = at top of screen
void                interrupt_input();
void                reset_keyseq_to_name_map();

//------------------------------------------------------------------------------
// Tib's terminal interface uses hidden global variables inside global
// functions:  there is only one terminal, and it's shared by everything.
//
// Clink has richer terminal interfaces than tib, and needs to wrap its
// terminal objects with tib's terminal interfaces.  Clink also needs to
// temporarily redirect terminal output to a file.
//
// This terminal object bridges the two models:
//  - Tib owns only the interface adapters constructed by its hooks.
//  - Clink remains responsible for console modes and idle waits.
//  - Clink gets to continue using its richer interfaces, while still being
//    fully integrated with tib.
//
// Clink no longer gets to create multiple different terminals and have
// different callers going through different (possibly competing) terminal
// instances.
//
// I could have redesigned tib to treat the terminal as object oriented, but
// that's a lot of extra complexity that isn't generally necessary.  I think
// that Clink is the outlier, not tib, so the complexity is encapsulated in
// this terminal object.  The drawback is that topology is non-obvious.
class tib_terminal_bridge : public singleton<tib_terminal_bridge>
{
public:
    static bool         init_terminal();
    static bool         init_terminal(bool cursor_visibility);
    static bool         init_terminal(terminal_in* in, terminal_out* out);
    static void         uninit_terminal();

                        ~tib_terminal_bridge();

    void                begin(bool can_hide_cursor=true);
    void                end(bool can_show_cursor=true);
    void                redirect(terminal_out* redirect);

    // Returns true when a pending binding was dispatched during the wait,
    // so the host can observe target completion and refresh its display.
    bool                wait_for_input(input_idle* idle=nullptr);
    void                add_target(std::weak_ptr<tib::dispatcher_target> target);
    void                reset_bindings();
    bool                is_bound(const char* seq, int32 len);
    bool                pending_input() const;
    bool                quoted_insert_pending() const;
    void                dispatch(uint8 key);

    void                set_chord(const char* keys, uint32 len);
    bool                has_chord() const { return m_chord_len != 0; }
    int32               read();
    int32               peek();
    bool                available(uint32 timeout);

    void                write(const char* text, size_t len);
    void                write(const char* text);
    void                ding();
    int32               get_columns() const;
    int32               get_rows() const;
    bool                get_cursor_pos(int16& x, int16& y) const;

    terminal_in*        get_in() const { return m_in; }
    terminal_out*       get_out() const { return m_out; }

private:
                        tib_terminal_bridge();
                        tib_terminal_bridge(bool cursor_visibility);
                        tib_terminal_bridge(terminal_in* in, terminal_out* out);
    void                init(bool cursor_visibility);

private:
    screen_buffer*      m_screen = nullptr;
    terminal_in*        m_in = nullptr;
    terminal_out*       m_out = nullptr;
    terminal_out*       m_old_out = nullptr;
    bool                m_screen_owned = false;
    bool                m_inout_owned = false;

    // Shared by all tib targets, independently of Clink's module routing.
    tib::binding_resolver m_resolver;
    bool                m_ambiguous = false;
    const char*         m_chord = nullptr;
    uint32              m_chord_len = 0;
    int32               m_began = 0;
    tib::hook_new_terminal_in_func_t m_old_input_hook = nullptr;
    tib::hook_new_terminal_out_func_t m_old_output_hook = nullptr;
    tib::hook_input_trace_func_t m_old_input_trace_hook = nullptr;
};
