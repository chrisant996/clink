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

#include <shared/util.h>

//------------------------------------------------------------------------------
int     call_readline_w(const wchar_t*, wchar_t*, unsigned);
int     ctrl_c(int, int);
int     paste_from_clipboard(int, int);
int     page_up(int, int);
int     up_directory(int, int);
int     show_rl_help(int, int);
int     copy_line_to_clipboard(int, int);
int     expand_env_vars(int, int);
int     completion_shim(int, int);
int     menu_completion_shim(int, int);
int     backward_menu_completion_shim(int, int);

extern "C" {

//------------------------------------------------------------------------------
line_editor_t* initialise_rl_line_editor()
{
    return (line_editor_t*)(new rl_line_editor());
}

//------------------------------------------------------------------------------
void shutdown_rl_line_editor(line_editor_t* line_editor)
{
    delete (rl_line_editor*)line_editor;
}

} // extern "C"



//------------------------------------------------------------------------------
rl_line_editor::rl_line_editor()
{
    add_funmap_entries();
    bind_inputrc();
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
        { "page-up",                             page_up },
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

// vim: expandtab
