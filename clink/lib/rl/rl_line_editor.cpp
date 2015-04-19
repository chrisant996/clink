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
#include "terminal.h"

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

extern "C" {
extern void         (*rl_fwrite_function)(FILE*, const wchar_t*, int);
extern void         (*rl_fflush_function)(FILE*);
} // extern "C"



//------------------------------------------------------------------------------
static int terminal_read_thunk(FILE* stream)
{
    int alt;
    int i;

    while (1)
    {
        wchar_t wc[2];
        char utf8[4];

        alt = 0;
        terminal* term = (terminal*)stream;
        i = term->read();

        // MSB is set if value represents a printable character.
        int printable = (i & 0x80000000);
        i &= ~printable;

        // Treat esc like cmd.exe does - clear the line.
        if (i == 0x1b)
        {
            if (rl_editing_mode == emacs_mode &&
                get_clink_setting_int("esc_clears_line"))
            {
                using_history();
                rl_delete_text(0, rl_end);
                rl_point = 0;
                rl_redisplay();
                continue;
            }
        }

        // Mask off top bits, they're used to track ALT key state.
        if (i < 0x80 || (i == 0xe0 && !printable))
        {
            break;
        }

        // Convert to utf-8 and insert directly into rl's line buffer.
        wc[0] = (wchar_t)i;
        wc[1] = L'\0';
        WideCharToMultiByte(CP_UTF8, 0, wc, -1, utf8, sizeof(utf8), NULL, NULL);

        rl_insert_text(utf8);
        rl_redisplay();
    }

    alt = alt ? 0x80 : 0;
    return i|alt;
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



//------------------------------------------------------------------------------
class rl_line_editor
    : public line_editor
{
public:
                        rl_line_editor(const environment& env);
    virtual             ~rl_line_editor();
    virtual bool        edit_line(const wchar_t* prompt, wchar_t* out, int out_size) override;
    virtual const char* get_shell_name() const override;
    virtual void        set_shell_name(const char* name) override;

private:
    void                bind_inputrc();
    void                add_funmap_entries();
    rl_scroller         m_scroller;
};

//------------------------------------------------------------------------------
rl_line_editor::rl_line_editor(const environment& env)
: line_editor(env)
{
    add_funmap_entries();
    bind_inputrc();

    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = terminal_flush_thunk;
    rl_instream = (FILE*)env.term;
    rl_outstream = (FILE*)env.term;
}

//------------------------------------------------------------------------------
rl_line_editor::~rl_line_editor()
{
}

//------------------------------------------------------------------------------
bool rl_line_editor::edit_line(const wchar_t* prompt, wchar_t* out, int out_size)
{
    return (call_readline_w(prompt, out, out_size) != 0);
}

//------------------------------------------------------------------------------
const char* rl_line_editor::get_shell_name() const
{
    return rl_readline_name;
}

//------------------------------------------------------------------------------
void rl_line_editor::set_shell_name(const char* name)
{
    rl_readline_name = name;
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
void
rl_line_editor::bind_inputrc()
{
    // Apply Clink's embedded inputrc.
    const char** inputrc_line = clink_inputrc;
    while (*inputrc_line)
    {
        char buffer[128];
        str_cpy(buffer, *inputrc_line, sizeof(buffer));
        rl_parse_and_bind(buffer);

        ++inputrc_line;
    }
}



//------------------------------------------------------------------------------
line_editor* create_rl_line_editor(const environment& env)
{
    return new rl_line_editor(env);
}

//------------------------------------------------------------------------------
void destroy_rl_line_editor(line_editor* editor)
{
    delete editor;
}
