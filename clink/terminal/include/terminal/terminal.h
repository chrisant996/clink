// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class screen_buffer;
class terminal_in;
class terminal_out;

//------------------------------------------------------------------------------
struct terminal
{
    screen_buffer*  screen;
    terminal_in*    in;
    terminal_out*   out;
    bool            screen_owned;
};

//------------------------------------------------------------------------------
terminal            terminal_create(screen_buffer* screen=nullptr, bool cursor_visibility=true);
void                terminal_destroy(const terminal& terminal);

//------------------------------------------------------------------------------
const char*         find_key_name(const char* keyseq, int32& len, int32& eqclass, int32& order);

//------------------------------------------------------------------------------
void                set_verbose_input(int32 verbose); // 1 = inline, 2 = at top of screen
void                interrupt_input();
void                reset_keyseq_to_name_map();
