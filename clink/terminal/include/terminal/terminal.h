// Copyright (c) 2018 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class terminal_in;
class terminal_out;

//------------------------------------------------------------------------------
struct terminal
{
    terminal_in*    in;
    terminal_out*   out;
};

//------------------------------------------------------------------------------
terminal            terminal_create();
void                terminal_destroy(const terminal& terminal);
