// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"
#include "rl_commands.h"
#include "line_buffer.h"

#include <core/base.h>
#include <core/log.h>
#include <terminal/ecma48_iter.h>
#include <terminal/printer.h>
#include <terminal/terminal_in.h>
#include <terminal/screen_buffer.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/xmalloc.h>
#include <compat/dirent.h>
#include <readline/posixdir.h>
extern int _rl_match_hidden_files;
extern int rl_complete_with_tilde_expansion;
// TODO: use the hidden attribute instead, or also?
#define HIDDEN_FILE(fn) ((fn)[0] == '.')
}

class pager;

//------------------------------------------------------------------------------
static FILE*        null_stream = (FILE*)1;
void                show_rl_help(printer&, pager&);
extern "C" int      wcwidth(int);
extern "C" char*    tgetstr(char*, char**);
static const int    RL_MORE_INPUT_STATES = ~(
                        RL_STATE_CALLBACK|
                        RL_STATE_INITIALIZED|
                        RL_STATE_OVERWRITE|
                        RL_STATE_VICMDONCE);

extern "C" {
extern void         (*rl_fwrite_function)(FILE*, const char*, int);
extern void         (*rl_fflush_function)(FILE*);
extern char*        _rl_comment_begin;
extern int          _rl_convert_meta_chars_to_ascii;
extern int          _rl_output_meta_chars;
#if defined(PLATFORM_WINDOWS)
extern int          _rl_vis_botlin;
extern int          _rl_last_c_pos;
extern int          _rl_last_v_pos;
#endif
} // extern "C"

#ifdef CLINK_CHRISANT_MODS
extern void host_add_history(int rl_history_index, const char* line);
extern void host_remove_history(int rl_history_index, const char* line);
#endif



//------------------------------------------------------------------------------
static void load_user_inputrc()
{
#if defined(PLATFORM_WINDOWS)
    // Remember to update clink_info() if anything changes in here.

    const char* env_vars[] = {
        "clink_inputrc",
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    for (const char* env_var : env_vars)
    {
        str<MAX_PATH> path;
        int path_length = GetEnvironmentVariable(env_var, path.data(), path.size());
        if (!path_length || path_length > int(path.size()))
            continue;

        path << "\\.inputrc";

        for (int j = 0; j < 2; ++j)
        {
            if (!rl_read_init_file(path.c_str()))
            {
                LOG("Found Readline inputrc at '%s'", path.c_str());
                break;
            }

            int dot = path.last_of('.');
            if (dot >= 0)
                path.data()[dot] = '_';
        }
    }
#endif // PLATFORM_WINDOWS
}

//------------------------------------------------------------------------------
extern "C" const char* host_get_env(const char* name)
{
    static int rotate = 0;
    static str<> rotating_tmp[10];

    str<>& s = rotating_tmp[rotate];
    rotate = (rotate + 1) % sizeof_array(rotating_tmp);
    if (!os::get_env(name, s))
        return nullptr;
    return s.c_str();
}

//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
static int complete_fncmp(const char *convfn, int convlen, const char *filename, int filename_len)
{
    // We let the OS handle wildcards, so not much to do here.  And we ignore
    // _rl_completion_case_fold because (1) this is Windows and (2) the
    // alternative is to write our own wildcard matching implementation.
    return 1;
}

//------------------------------------------------------------------------------
static char* filename_menu_completion_function(const char *text, int state)
{
    static DIR *directory = (DIR *)NULL;
    static char *filename = (char *)NULL;
    static char *dirname = (char *)NULL;
    static char *users_dirname = (char *)NULL;
    static int filename_len;
    char *temp, *dentry, *convfn;
    int dirlen, dentlen, convlen;
    int tilde_dirname;
    struct dirent *entry;

    /* If we don't have any state, then do some initialization. */
    if (state == 0)
    {
        /* If we were interrupted before closing the directory or reading
           all of its contents, close it. */
        if (directory)
        {
            closedir(directory);
            directory = (DIR *)NULL;
        }
        FREE(dirname);
        FREE(filename);
        FREE(users_dirname);

        filename = savestring(text);
        if (*text == 0)
            text = ".";
        dirname = savestring(text);

        temp = rl_last_path_separator(dirname);

#if defined(__MSDOS__) || defined(_WIN32)
        /* special hack for //X/... */
        if (rl_is_path_separator(dirname[0]) && rl_is_path_separator(dirname[1]) && ISALPHA((unsigned char)dirname[2]) && rl_is_path_separator(dirname[3]))
            temp = rl_last_path_separator(dirname + 3);
#endif

        if (temp)
        {
            strcpy(filename, ++temp);
            *temp = '\0';
        }
#if defined(__MSDOS__) || (defined(_WIN32) && !defined(__CYGWIN__))
        /* searches from current directory on the drive */
        else if (ISALPHA((unsigned char)dirname[0]) && dirname[1] == ':')
        {
            strcpy(filename, dirname + 2);
            dirname[2] = '\0';
        }
#endif
        else
        {
            dirname[0] = '.';
            dirname[1] = '\0';
        }

        /* We aren't done yet.  We also support the "~user" syntax. */

        /* Save the version of the directory that the user typed, dequoting
           it if necessary. */
        if (rl_completion_found_quote && rl_filename_dequoting_function)
            users_dirname = (*rl_filename_dequoting_function)(dirname, rl_completion_quote_character);
        else
            users_dirname = savestring(dirname);

        tilde_dirname = 0;
        if (*dirname == '~')
        {
            temp = tilde_expand(dirname);
            xfree(dirname);
            dirname = temp;
            tilde_dirname = 1;
        }

        /* We have saved the possibly-dequoted version of the directory name
           the user typed.  Now transform the directory name we're going to
           pass to opendir(2).  The directory rewrite hook modifies only the
           directory name; the directory completion hook modifies both the
           directory name passed to opendir(2) and the version the user
           typed.  Both the directory completion and rewrite hooks should perform
           any necessary dequoting.  The hook functions return 1 if they modify
           the directory name argument.  If either hook returns 0, it should
           not modify the directory name pointer passed as an argument. */
        if (rl_directory_rewrite_hook)
            (*rl_directory_rewrite_hook)(&dirname);
        else if (rl_directory_completion_hook && (*rl_directory_completion_hook)(&dirname))
        {
            xfree(users_dirname);
            users_dirname = savestring(dirname);
        }
        else if (tilde_dirname == 0 && rl_completion_found_quote && rl_filename_dequoting_function)
        {
            /* delete single and double quotes */
            xfree(dirname);
            dirname = savestring(users_dirname);
        }

        str<> dn;
        dn << dirname << "\\" << filename << "*";
        directory = opendir(dn.c_str());

        /* Now dequote a non-null filename.  FILENAME will not be NULL, but may
           be empty. */
        if (*filename && rl_completion_found_quote && rl_filename_dequoting_function)
        {
            /* delete single and double quotes */
            temp = (*rl_filename_dequoting_function)(filename, rl_completion_quote_character);
            xfree(filename);
            filename = temp;
        }
        filename_len = strlen(filename);

        rl_filename_completion_desired = 1;
    }

    /* At this point we should entertain the possibility of hacking wildcarded
       filenames, like /usr/man/man<WILD>/te<TAB>.  If the directory name
       contains globbing characters, then build an array of directories, and
       then map over that list while completing. */
    /* *** UNIMPLEMENTED *** */

    /* Now that we have some state, we can read the directory. */

    entry = (struct dirent *)NULL;
    while (directory && (entry = readdir(directory)))
    {
        convfn = dentry = entry->d_name;
        convlen = dentlen = D_NAMLEN(entry);

        if (rl_filename_rewrite_hook)
        {
            convfn = (*rl_filename_rewrite_hook)(dentry, dentlen);
            convlen = (convfn == dentry) ? dentlen : strlen(convfn);
        }

        /* Special case for no filename.  If the user has disabled the
           `match-hidden-files' variable, skip filenames beginning with `.'.
           All other entries except "." and ".." match. */
        if (filename_len == 0)
        {
            if (_rl_match_hidden_files == 0 && HIDDEN_FILE(convfn))
                continue;

            if (convfn[0] != '.' ||
                (convfn[1] && (convfn[1] != '.' || convfn[2])))
                break;
        }
        else
        {
            if (complete_fncmp(convfn, convlen, filename, filename_len))
                break;
        }
    }

    if (entry == 0)
    {
        if (directory)
        {
            closedir(directory);
            directory = (DIR *)NULL;
        }
        if (dirname)
        {
            xfree(dirname);
            dirname = (char *)NULL;
        }
        if (filename)
        {
            xfree(filename);
            filename = (char *)NULL;
        }
        if (users_dirname)
        {
            xfree(users_dirname);
            users_dirname = (char *)NULL;
        }

        return (char *)NULL;
    }
    else
    {
        /* dirname && (strcmp (dirname, ".") != 0) */
        if (dirname && (dirname[0] != '.' || dirname[1]))
        {
            if (rl_complete_with_tilde_expansion && *users_dirname == '~')
            {
                dirlen = strlen(dirname);
                temp = (char *)xmalloc(2 + dirlen + D_NAMLEN(entry));
                strcpy(temp, dirname);
                /* Canonicalization cuts off any final slash present.  We
                   may need to add it back. */
                if (!rl_is_path_separator(dirname[dirlen - 1]))
                {
                    temp[dirlen++] = rl_preferred_path_separator;
                    temp[dirlen] = '\0';
                }
            }
            else
            {
                dirlen = strlen(users_dirname);
                temp = (char *)xmalloc(2 + dirlen + D_NAMLEN(entry));
                strcpy(temp, users_dirname);
/* begin_clink_change
 * Removed appending of a '/' to correctly support volume-relative paths.
 */
#if 0
                /* Make sure that temp has a trailing slash here. */
                if (!rl_is_path_separator (users_dirname[dirlen - 1]))
                    temp[dirlen++] = rl_preferred_path_separator;
#endif
/* end_clink_change */
            }

            strcpy(temp + dirlen, convfn);
        }
        else
            temp = savestring(convfn);

        if (convfn != dentry)
            xfree(convfn);

        return (temp);
    }
}



//------------------------------------------------------------------------------
enum {
    bind_id_input,
    bind_id_more_input,
    bind_id_rl_help,
};



//------------------------------------------------------------------------------
static int terminal_read_thunk(FILE* stream)
{
    if (stream == null_stream)
        return 0;

    terminal_in* term = (terminal_in*)stream;
    return term->read();
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const char* chars, int char_count)
{
    if (stream == stderr || stream == null_stream)
    {
        // TODO: What gets lost in this black hole?
        return;
    }

    printer* pter = (printer*)stream;
    pter->print(chars, char_count);
}



//------------------------------------------------------------------------------
rl_module::rl_module(const char* shell_name)
: m_rl_buffer(nullptr)
, m_prev_group(-1)
{
    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = [] (FILE*) {};
    rl_instream = null_stream;
    rl_outstream = null_stream;

    rl_readline_name = shell_name;
    rl_catch_signals = 0;
    _rl_comment_begin = savestring("::"); // this will do...

    // Readline needs a tweak of it's handling of 'meta' (i.e. IO bytes >=0x80)
    // so that it handles UTF-8 correctly (convert=input, output=output)
    _rl_convert_meta_chars_to_ascii = 0;
    _rl_output_meta_chars = 1;

    // Recognize both / and \\ as path separators, and normalize to \\.
#ifdef CLINK_CHRISANT_MODS
    rl_backslash_path_sep = 1;
    rl_preferred_path_separator = '\\';
#endif

    // Disable completion and match display.
#ifndef CLINK_CHRISANT_FIXES
    // No: disabling completion breaks `complete` and `menu-complete`.
    rl_completion_entry_function = [](const char*, int) -> char* { return nullptr; };
    rl_completion_display_matches_hook = [](char**, int, int) {};
#endif
    rl_menu_completion_entry_function = filename_menu_completion_function;

    // Add commands.
#ifdef CLINK_CHRISANT_MODS
    static bool s_rl_initialized = false;
    if (!s_rl_initialized)
    {
        s_rl_initialized = true;
        rl_add_history_hook = host_add_history;
        rl_remove_history_hook = host_remove_history;
        rl_add_funmap_entry("reset-line", clink_reset_line);
    }
#endif

    // Bind extended keys so editing follows Windows' conventions.
    static const char* ext_key_binds[][2] = {
        { "\\e[1;5D", "backward-word" },           // ctrl-left
        { "\\e[1;5C", "forward-word" },            // ctrl-right
        { "\\e[F",    "end-of-line" },             // end
        { "\\e[H",    "beginning-of-line" },       // home
        { "\\e[3~",   "delete-char" },             // del
        { "\\e[1;5F", "kill-line" },               // ctrl-end
        { "\\e[1;5H", "backward-kill-line" },      // ctrl-home
        { "\\e[5~",   "history-search-backward" }, // pgup
        { "\\e[6~",   "history-search-forward" },  // pgdn
#ifdef CLINK_CHRISANT_MODS
        { "\\e[3;5~", "kill-word" },               // ctrl+del
        { "\\d",      "unix-word-rubout" },        // ctrl+backspace
#endif
        { "\\C-z",    "undo" },
    };

    for (int i = 0; i < sizeof_array(ext_key_binds); ++i)
        rl_bind_keyseq(ext_key_binds[i][0], rl_named_function(ext_key_binds[i][1]));

    load_user_inputrc();
}

//------------------------------------------------------------------------------
rl_module::~rl_module()
{
    free(_rl_comment_begin);
    _rl_comment_begin = nullptr;
}

//------------------------------------------------------------------------------
void rl_module::bind_input(binder& binder)
{
    int default_group = binder.get_group();
    binder.bind(default_group, "", bind_id_input);
    binder.bind(default_group, "\\M-h", bind_id_rl_help);

    m_catch_group = binder.create_group("readline");
    binder.bind(m_catch_group, "", bind_id_more_input);
}

//------------------------------------------------------------------------------
void rl_module::on_begin_line(const context& context)
{
    rl_outstream = (FILE*)(terminal_out*)(&context.printer);

    // Readline needs to be told about parts of the prompt that aren't visible
    // by enclosing them in a pair of 0x01/0x02 chars.
    str<128> rl_prompt;

    ecma48_state state;
    ecma48_iter iter(context.prompt, state);
    while (const ecma48_code& code = iter.next())
    {
        bool c1 = (code.get_type() == ecma48_code::type_c1);
        if (c1) rl_prompt.concat("\x01", 1);
                rl_prompt.concat(code.get_pointer(), code.get_length());
        if (c1) rl_prompt.concat("\x02", 1);
    }

    auto handler = [] (char* line) { rl_module::get()->done(line); };
    rl_callback_handler_install(rl_prompt.c_str(), handler);

    m_done = false;
    m_eof = false;
    m_prev_group = -1;
}

//------------------------------------------------------------------------------
void rl_module::on_end_line()
{
    if (m_rl_buffer != nullptr)
    {
        rl_line_buffer = m_rl_buffer;
        m_rl_buffer = nullptr;
    }

    // This prevents any partial Readline state leaking from one line to the next
    rl_readline_state &= ~RL_MORE_INPUT_STATES;
}

//------------------------------------------------------------------------------
void rl_module::on_matches_changed(const context& context)
{
}

//------------------------------------------------------------------------------
void rl_module::on_input(const input& input, result& result, const context& context)
{
    if (input.id == bind_id_rl_help)
    {
        show_rl_help(context.printer, context.pager);
        result.redraw();
        return;
    }

    // Setup the terminal.
    struct : public terminal_in
    {
        virtual void begin() override   {}
        virtual void end() override     {}
        virtual void select() override  {}
        virtual int  read() override    { return *(unsigned char*)(data++); }
        virtual void set_key_tester(key_tester* keys) override {}
        const char*  data;
    } term_in;

    term_in.data = input.keys;
    rl_instream = (FILE*)(&term_in);

    // Call Readline's until there's no characters left.
    int is_inc_searching = rl_readline_state & RL_STATE_ISEARCH;
#ifdef CLINK_CHRISANT_FIXES
    unsigned int len = input.len;
    while (len && !m_done)
#else
    while (*term_in.data && !m_done)
#endif
    {
#ifdef CLINK_CHRISANT_FIXES
        --len;
#endif
        rl_callback_read_char();

        // Internally Readline tries to resend escape characters but it doesn't
        // work with how Clink uses Readline. So we do it here instead.
        if (term_in.data[-1] == 0x1b && is_inc_searching)
        {
            --term_in.data;
#ifdef CLINK_CHRISANT_FIXES
            ++len;
#endif
            is_inc_searching = 0;
        }
    }

    if (m_done)
    {
        result.done(m_eof);
        return;
    }

    // Check if Readline want's more input or if we're done.
    if (rl_readline_state & RL_MORE_INPUT_STATES)
    {
        if (m_prev_group < 0)
            m_prev_group = result.set_bind_group(m_catch_group);
    }
    else if (m_prev_group >= 0)
    {
        result.set_bind_group(m_prev_group);
        m_prev_group = -1;
    }
}

//------------------------------------------------------------------------------
void rl_module::done(const char* line)
{
    m_done = true;
    m_eof = (line == nullptr);

    // Readline will reset the line state on returning from this call. Here we
    // trick it into reseting something else so we can use rl_line_buffer later.
    static char dummy_buffer = 0;
    m_rl_buffer = rl_line_buffer;
    rl_line_buffer = &dummy_buffer;

    rl_callback_handler_remove();
}

//------------------------------------------------------------------------------
void rl_module::on_terminal_resize(int columns, int rows, const context& context)
{
#if 1
    rl_reset_screen_size();
    rl_redisplay();
#else
    static int prev_columns = columns;

    int remaining = prev_columns;
    int line_count = 1;

    auto measure = [&] (const char* input, int length) {
        ecma48_state state;
        ecma48_iter iter(input, state, length);
        while (const ecma48_code& code = iter.next())
        {
            switch (code.get_type())
            {
            case ecma48_code::type_chars:
                for (str_iter i(code.get_pointer(), code.get_length()); i.more(); )
                {
                    int n = wcwidth(i.next());
                    remaining -= n;
                    if (remaining > 0)
                        continue;

                    ++line_count;

                    remaining = prev_columns - ((remaining < 0) << 1);
                }
                break;

            case ecma48_code::type_c0:
                switch (code.get_code())
                {
                case ecma48_code::c0_lf:
                    ++line_count;
                    /* fallthrough */

                case ecma48_code::c0_cr:
                    remaining = prev_columns;
                    break;

                case ecma48_code::c0_ht:
                    if (int n = 8 - ((prev_columns - remaining) & 7))
                        remaining = max(remaining - n, 0);
                    break;

                case ecma48_code::c0_bs:
                    remaining = min(remaining + 1, prev_columns); // doesn't consider full-width
                    break;
                }
                break;
            }
        }
    };

    measure(context.prompt, -1);

    const line_buffer& buffer = context.buffer;
    const char* buffer_ptr = buffer.get_buffer();
    measure(buffer_ptr, buffer.get_cursor());
    int cursor_line = line_count - 1;

    buffer_ptr += buffer.get_cursor();
    measure(buffer_ptr, -1);

    static const char* const termcap_up    = tgetstr("ku", nullptr);
    static const char* const termcap_down  = tgetstr("kd", nullptr);
    static const char* const termcap_cr    = tgetstr("cr", nullptr);
    static const char* const termcap_clear = tgetstr("ce", nullptr);

    auto& printer = context.printer;

    // Move cursor to bottom line.
    for (int i = line_count - cursor_line; --i;)
        printer.print(termcap_down, 64);

    printer.print(termcap_cr, 64);
    do
    {
        printer.print(termcap_clear, 64);

        if (--line_count)
            printer.print(termcap_up, 64);
    }
    while (line_count);

    printer.print(context.prompt, 1024);
    printer.print(buffer.get_buffer(), 1024);

    prev_columns = columns;
#endif
}
