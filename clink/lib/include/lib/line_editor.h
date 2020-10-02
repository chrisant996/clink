// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class editor_module;
class line_buffer;
class match_generator;
class terminal_in;
class terminal_out;
class printer;
class str_base;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        // Required.
        terminal_in*    input = nullptr;
        terminal_out*   output = nullptr;
        printer*        printer = nullptr;

        // Optional.
        const char*     shell_name = "clink";
        const char*     prompt = "clink $ ";
        const char*     command_delims = nullptr;
        const char*     quote_pair = "\"";
        const char*     word_delims = " \t";
        const char*     auto_quote_chars = " ";
    };

    virtual             ~line_editor() = default;
    virtual bool        add_module(editor_module& module) = 0;
    virtual bool        add_generator(match_generator& generator) = 0;
    virtual bool        get_line(str_base& out) = 0;
    virtual bool        edit(str_base& out) = 0;
    virtual bool        update() = 0;
};



//------------------------------------------------------------------------------
line_editor*            line_editor_create(const line_editor::desc& desc);
void                    line_editor_destroy(line_editor* editor);
