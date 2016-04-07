// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_line_editor.h"
#include "inputrc.h"
#include "rl_delegate.h"
#include "rl_scroller.h"

#include <core/base.h>
#include <core/log.h>
#include <core/singleton.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <line_state.h>
#include <matches/match_printer.h>
#include <matches/matches.h>
#include <terminal/ecma48_iter.h>
#include <terminal/terminal.h>

//------------------------------------------------------------------------------
bool    call_readline(const char*, str_base&);
int     copy_line_to_clipboard(int, int);
int     ctrl_c(int, int);
int     expand_env_vars(int, int);
int     paste_from_clipboard(int, int);
int     show_rl_help(int, int);
int     up_directory(int, int);
int     history_expand_control(char*, int);

extern "C" {
extern void     (*rl_fwrite_function)(FILE*, const char*, int);
extern void     (*rl_fflush_function)(FILE*);
extern char*    _rl_comment_begin;
extern int      _rl_completion_case_map;
extern int      rl_catch_signals;
extern int      rl_display_fixed;
} // extern "C"



//------------------------------------------------------------------------------
static int terminal_read_thunk(FILE* stream)
{
    terminal* term = (terminal*)stream;
    return term->read();
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const char* chars, int char_count)
{
    if (stream == stderr)
        return;

    terminal* term = (terminal*)stream;
    return term->write(chars, char_count);
}

//------------------------------------------------------------------------------
static void terminal_flush_thunk(FILE* stream)
{
    if (stream == stderr)
        return;

    terminal* term = (terminal*)stream;
    return term->flush();
}



//------------------------------------------------------------------------------
class rl_line_editor
    : public line_editor
    , public singleton<rl_line_editor>
{
public:
                    rl_line_editor(const desc& desc);
    virtual         ~rl_line_editor();
    virtual bool    edit_line(const char* prompt, str_base& out) override;

private:
    void            bind_embedded_inputrc();
    void            load_user_inputrc();
    void            add_funmap_entries();
    char*           completion(const char*, int);
    void            display_matches(char**, int, int);
    rl_scroller     m_scroller;
    matches         m_matches;
};

//------------------------------------------------------------------------------
rl_line_editor::rl_line_editor(const desc& desc)
: line_editor(desc)
{
    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = terminal_flush_thunk;
    rl_instream = (FILE*)desc.term;
    rl_outstream = (FILE*)desc.term;

    rl_readline_name = get_shell_name();
    rl_catch_signals = 0;
    _rl_comment_begin = "::";
    rl_basic_word_break_characters = " <>|=;&";
    rl_basic_quote_characters = "\"";
    rl_completer_quote_characters = "\"";
    rl_completer_word_break_characters = (char*)rl_basic_word_break_characters;
    rl_filename_quote_characters = " %=;&^";

    add_funmap_entries();
    bind_embedded_inputrc();
    load_user_inputrc();

    rl_completion_entry_function = rl_delegate(this, rl_line_editor, completion);
    rl_completion_display_matches_hook = rl_delegate(this, rl_line_editor, display_matches);

    history_inhibit_expansion_function = history_expand_control;
}

//------------------------------------------------------------------------------
rl_line_editor::~rl_line_editor()
{
}

//------------------------------------------------------------------------------
bool rl_line_editor::edit_line(const char* prompt, str_base& out)
{
    // Readline needs to be told about parts of the prompt that aren't visible
    // by enclosing them in a pair of 0x01/0x02 chars.
    str<128> rl_prompt;

    ecma48_state state;
    ecma48_iter iter(prompt, state);
    while (const ecma48_code* code = iter.next())
    {
        bool csi = (code->type == ecma48_code::type_csi);
        if (csi) rl_prompt.concat("\x01", 1);
                 rl_prompt.concat(code->str, code->length);
        if (csi) rl_prompt.concat("\x02", 1);
    }

    return call_readline(rl_prompt.c_str(), out);
}

//------------------------------------------------------------------------------
void rl_line_editor::add_funmap_entries()
{
    struct {
        const char*         name;
        rl_command_func_t*  func;
    } entries[] = {
        { "ctrl-c",                              ctrl_c },
        { "paste-from-clipboard",                paste_from_clipboard },
        { "up-directory",                        up_directory },
        { "show-rl-help",                        show_rl_help },
        { "copy-line-to-clipboard",              copy_line_to_clipboard },
        { "expand-env-vars",                     expand_env_vars },
    };

    for(int i = 0; i < sizeof_array(entries); ++i)
        rl_add_funmap_entry(entries[i].name, entries[i].func);
}

//------------------------------------------------------------------------------
void rl_line_editor::bind_embedded_inputrc()
{
    // Apply Clink's embedded inputrc.
    const char** inputrc_line = clink_inputrc;
    while (*inputrc_line)
    {
        str<96> buffer;
        buffer << *inputrc_line;
        rl_parse_and_bind(buffer.data());
        ++inputrc_line;
    }
}

//------------------------------------------------------------------------------
void rl_line_editor::load_user_inputrc()
{
    const char* env_vars[] = {
        "clink_inputrc",
        "localappdata",
        "appdata",
        "userprofile",
        "home",
    };

    for (int i = 0; i < sizeof_array(env_vars); ++i)
    {
        str<MAX_PATH> path;
        int path_length = GetEnvironmentVariable(env_vars[i], path.data(), path.size());
        if (!path_length || path_length > int(path.size()))
            continue;

        path << "\\.inputrc";

        for (int j = 0; j < 2; ++j)
        {
            if (!rl_read_init_file(path.c_str()))
            {
                LOG("Found Readline inputrc at '%s'", path);
                break;
            }

            int dot = path.last_of('.');
            if (dot >= 0)
                path.data()[dot] = '_';
        }
    }
}

//------------------------------------------------------------------------------
char* rl_line_editor::completion(const char*, int)
{
    /* disabled */
    return nullptr;
}

//------------------------------------------------------------------------------
void rl_line_editor::display_matches(char**, int, int)
{
    /* disabled */
}



//------------------------------------------------------------------------------
line_editor* create_rl_line_editor(const line_editor::desc& desc)
{
    return new rl_line_editor(desc);
}

//------------------------------------------------------------------------------
void destroy_rl_line_editor(line_editor* editor)
{
    delete editor;
}
