// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class editor_module;
class line_buffer;
class match_generator;
class word_classifier;
class input_idle;
class terminal_in;
class terminal_out;
class printer;
class host_callbacks;
class str_base;
class collector_tokeniser;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
                        desc(terminal_in* i, terminal_out* o, printer* p, host_callbacks* c)
                        : input(i), output(o), printer(p), callbacks(c) {}

        // Required.
        terminal_in*    input = nullptr;
        terminal_out*   output = nullptr;
        printer*        printer = nullptr;
        host_callbacks* callbacks = nullptr;

        // Optional.
        const char*     prompt = "clink $ ";
        const char*     rprompt = nullptr;
        collector_tokeniser* command_tokeniser = nullptr;
        collector_tokeniser* word_tokeniser = nullptr;

        const char*     get_quote_pair() const { return quote_pair ? quote_pair : ""; }
        void            reset_quote_pair() { quote_pair = "\""; }

        // Clink used to support arbitrary quote pairs, such as "()".  But
        // Readline doesn't support heterogenous quote pairs.  So making this
        // private lets the logic stay dormant while ensuring callers don't get
        // themselves into trouble.
    private:
        const char*     quote_pair = "\"";
    };

    virtual             ~line_editor() = default;
    virtual bool        add_module(editor_module& module) = 0;
    virtual void        set_generator(match_generator& generator) = 0;
    virtual void        set_classifier(word_classifier& classifier) = 0;
    virtual void        set_input_idle(input_idle* idle) = 0;
    virtual void        set_prompt(const char* prompt, const char* rprompt, bool redisplay) = 0;
    virtual bool        get_line(str_base& out) = 0;
    virtual bool        edit(str_base& out, bool edit=true) = 0;
    virtual void        override_line(const char* line, const char* needle, int32 point) = 0;
    virtual bool        update() = 0;
    virtual void        update_matches() = 0;
#ifdef DEBUG
    virtual bool        is_line_overridden() = 0;
#endif
};



//------------------------------------------------------------------------------
line_editor*            line_editor_create(const line_editor::desc& desc);
void                    line_editor_destroy(line_editor* editor);
