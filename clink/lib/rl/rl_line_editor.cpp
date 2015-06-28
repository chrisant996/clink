/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "rl_line_editor.h"
#include "inputrc.h"
#include "rl_scroller.h"
#include "singleton.h"
#include "terminal.h"
#include "matches/match_generator.h"
#include "line_state.h"

#include <core/str.h>
#include <shared/util.h>

//------------------------------------------------------------------------------
int     call_readline_w(const wchar_t*, wchar_t*, unsigned);
int     completion_shim(int, int);
int     copy_line_to_clipboard(int, int);
int     ctrl_c(int, int);
int     expand_env_vars(int, int);
int     get_clink_setting_int(const char*);
int     menu_completion_shim(int, int);
int     backward_menu_completion_shim(int, int);
int     paste_from_clipboard(int, int);
int     show_rl_help(int, int);
int     up_directory(int, int);
int     postprocess_matches(char**);
char**  alternative_matches(const char*, int, int);
void    display_matches(char**, int, int);
int     history_expand_control(char*, int);

extern "C" {
extern void         (*rl_fwrite_function)(FILE*, const wchar_t*, int);
extern void         (*rl_fflush_function)(FILE*);
extern char*        _rl_comment_begin;
extern int          rl_catch_signals;
} // extern "C"



//------------------------------------------------------------------------------
static int terminal_read_thunk(FILE* stream)
{
    int i;

    while (1)
    {
        terminal* term = (terminal*)stream;
        i = term->read();

        // Mask off top bits, they're used to track ALT key state.
        if (i < 0x80)
            break;

        // Convert to utf-8 and insert directly into rl's line buffer.
        wchar_t wc[2] = { (wchar_t)i, 0 };
        char utf8[4] = {};
        WideCharToMultiByte(CP_UTF8, 0, wc, -1, utf8, sizeof(utf8), nullptr, nullptr);

        rl_insert_text(utf8);
        rl_redisplay();
    }

    return i;
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const wchar_t* chars, int char_count)
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

// MODE4
template <class T, class R, class P0, class P1, class P2>
R (*rl_delegate(T* t, R (T::*f)(P0, P1, P2)))(P0, P1, P2)
{
    static T* self = t;
    static R (T::*func)(P0, P1, P2) = f;

    struct thunk
    {
        static R impl(P0 p0, P1 p1, P2 p2)
        {
            return (self->*func)(p0, p1, p2);
        }
    };

    return &thunk::impl;
}
// MODE4



//------------------------------------------------------------------------------
class rl_line_editor
    : public line_editor
    , public singleton<rl_line_editor>
{
public:
                    rl_line_editor(const desc& desc);
    virtual         ~rl_line_editor();
    virtual bool    edit_line_impl(const wchar_t* prompt, wchar_t* out, int out_count) override;

private:
    void            bind_embedded_inputrc();
    void            load_user_inputrc();
    void            add_funmap_entries();
    char**          completion(const char* word, int start, int end);
    void            display_matches(char**, int, int);
    rl_scroller     m_scroller;
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

    rl_attempted_completion_function = rl_delegate(this, &rl_line_editor::completion);
    rl_completion_display_matches_hook = rl_delegate(this, &rl_line_editor::display_matches);

    history_inhibit_expansion_function = history_expand_control;
}

//------------------------------------------------------------------------------
rl_line_editor::~rl_line_editor()
{
}

//------------------------------------------------------------------------------
bool rl_line_editor::edit_line_impl(const wchar_t* prompt, wchar_t* out, int out_count)
{
    return (call_readline_w(prompt, out, out_count) != 0);
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
        { "clink-completion-shim",               completion_shim },
        { "clink-menu-completion-shim",          menu_completion_shim },
        { "clink-backward-menu-completion-shim", backward_menu_completion_shim },
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
        if (!path_length || path_length > path.size())
            continue;

        path << "\\.inputrc";

        for (int j = 0; j < 2; ++j)
        {
            if (!rl_read_init_file(path))
            {
                LOG_INFO("Found Readline inputrc at '%s'", path);
                break;
            }

            int dot = path.last_of('.');
            if (dot >= 0)
                path.data()[dot] = '_';
        }
    }
}

//------------------------------------------------------------------------------
char** rl_line_editor::completion(const char* word, int start, int end)
{
    rl_attempted_completion_over = 1;
    line_state line = { word, rl_line_buffer, start, end, rl_point };
    return get_match_generator()->generate(line);
}

//------------------------------------------------------------------------------
void rl_line_editor::display_matches(char** matches, int match_count, int longest)
{
    return ::display_matches(matches, match_count, longest);
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
