// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

/* MODE4
//------------------------------------------------------------------------------
class rl_line_editor
{
public:
    void            bind_embedded_inputrc();
    void            load_user_inputrc();
    void            add_funmap_entries();
    rl_scroller     m_scroller;
};

//------------------------------------------------------------------------------
rl_line_editor::rl_line_editor(const desc& desc)
: line_editor(desc)
{
    rl_readline_name = get_shell_name();
    rl_catch_signals = 0;
    _rl_comment_begin = "::";
    rl_basic_word_break_characters = " <>|=;&";
    rl_filename_quote_characters = " %=;&^";

    add_funmap_entries();
    bind_embedded_inputrc();
    load_user_inputrc();

    rl_completion_entry_function = rl_delegate(this, rl_line_editor, completion);
    rl_completion_display_matches_hook = rl_delegate(this, rl_line_editor, display_matches);
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
MODE4 */
