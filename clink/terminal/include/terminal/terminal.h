// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <tib.h>

class screen_buffer;
class terminal_in;
class terminal_out;
class tib_terminal_bridge;

//------------------------------------------------------------------------------
extern tib_terminal_bridge* g_terminal;

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
