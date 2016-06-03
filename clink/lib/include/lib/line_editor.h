// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class editor_backend;
class line_buffer;
class match_generator;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        // Required.
        terminal*       terminal = nullptr;
        line_buffer*    buffer = nullptr;

        // Optional.
        editor_backend* backend = nullptr;
        const char*     prompt = "clink $ ";
        const char*     command_delims; // MODE4
        const char*     quote_pair = "\"";
        const char*     word_delims = " \"";
        const char*     partial_delims = "\\/";
        const char*     auto_quote_chars = " ";
    };

    virtual             ~line_editor() {}
    virtual bool        add_backend(editor_backend& backend) = 0;
    virtual bool        add_generator(match_generator& generator) = 0;
    virtual bool        get_line(char* out, int out_size) = 0;
    virtual bool        edit(char* out, int out_size) = 0;
    virtual bool        update() = 0;
};



//------------------------------------------------------------------------------
line_editor*            line_editor_create(const line_editor::desc& desc);
void                    line_editor_destroy(line_editor* editor);
editor_backend*         classic_match_ui_create();
void                    classic_match_ui_destroy(editor_backend* classic_ui);
