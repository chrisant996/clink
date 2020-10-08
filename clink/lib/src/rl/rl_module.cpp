// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"
#include "rl_commands.h"
#include "line_buffer.h"
#include "matches.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/log.h>
#include <terminal/ecma48_iter.h>
#include <terminal/printer.h>
#include <terminal/terminal_in.h>
#include <terminal/key_tester.h>
#include <terminal/screen_buffer.h>
#include <terminal/setting_colour.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/keymaps.h>
#include <readline/xmalloc.h>
#include <compat/dirent.h>
#include <readline/posixdir.h>
extern int _rl_match_hidden_files;
extern int rl_complete_with_tilde_expansion;
#define HIDDEN_FILE(fn) ((fn)[0] == '.')
#if defined (COLOR_SUPPORT)
#include <readline/parse-colors.h>
extern int _rl_colored_stats;
extern int _rl_colored_completion_prefix;
#endif
}

class pager;

//------------------------------------------------------------------------------
static FILE*        null_stream = (FILE*)1;
static FILE*        in_stream = (FILE*)2;
static FILE*        out_stream = (FILE*)3;
terminal_in*        s_direct_input = nullptr;       // for read_key_hook
terminal_in*        s_processed_input = nullptr;    // for read thunk
printer*            s_printer = nullptr;
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

extern void host_add_history(int rl_history_index, const char* line);
extern void host_remove_history(int rl_history_index, const char* line);
extern setting_colour g_colour_interact;

line_buffer*        rl_buffer = nullptr;

//------------------------------------------------------------------------------
setting_colour g_colour_hidden(
    "colour.hidden",
    "Hidden file completions",
    "Used when Clink displays file completions with the hidden attribute.",
    setting_colour::value_light_red, setting_colour::value_bg_default);



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
static bool build_color_sequence(const attributes& colour, str_base& out)
{
    out.clear();

    auto bg = colour.get_bg();
    int value = (bg.is_default ? -1 :
                 (bg.value.value & 0x0f) < 8 ? (bg.value.value & 0x0f) + 30 :
                 (bg.value.value & 0x0f) - 8 + 90);
    if (value >= 0)
        out.format("%u", value);

    auto fg = colour.get_fg();
    value = (fg.is_default ? -1 :
             (fg.value.value & 0x0f) < 8 ? (fg.value.value & 0x0f) + 30 :
             (fg.value.value & 0x0f) - 8 + 90);
    if (value >= 0)
    {
        if (out.length())
            out << ";";
        out.format("%u", value);
    }

    if (auto bold = colour.get_bold())
    {
        if (out.length())
            out << ";";
        out << (bold.value ? "1" : "22");
    }
    if (auto underline = colour.get_underline())
    {
        if (out.length())
            out << ";";
        out << (underline.value ? "4" : "24");
    }

    return !!out.length();
}

//------------------------------------------------------------------------------
class rl_more_key_tester : public key_tester
{
public:
    virtual bool    is_bound(const char* seq, int len)
                    {
                        if (len <= 1)
                            return true;
                        if (len == 8 && strcmp(seq, "\x1b[27;27~") == 0)
                            return true;
                        rl_ding();
                        return false;
                    }
    virtual bool    translate(const char* seq, int len, str_base& out)
                    {
                        if (len == 8 && strcmp(seq, "\x1b[27;27~") == 0)
                        {
                            out = "\x1b";
                            return true;
                        }
                        return false;
                    }
};
extern "C" int read_key_hook(void)
{
    assert(s_direct_input);
    if (!s_direct_input)
        return 0;

    rl_more_key_tester tester;
    key_tester* old = s_direct_input->set_key_tester(&tester);

    s_direct_input->select();
    int key = s_direct_input->read();

    s_direct_input->set_key_tester(old);
    return key;
}



//------------------------------------------------------------------------------
static const matches* s_matches = nullptr;

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

        /* Don't bother trying to complete a UNC path that doesn't have at least
           both a server and share component. */
        if (path::is_incomplete_unc(text))
            return nullptr;

        filename = savestring(text);
        if (*text == 0)
            text = ".";
        dirname = savestring(text);

        temp = rl_last_path_separator(dirname);
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

        str<32> dn;
        dn << dirname;
        if (dn.length() && !path::is_separator(dn.c_str()[dn.length() - 1]))
            dn << "\\";
        dn << filename << "*";
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

        // Always skip "." and ".." no matter the filename pattern.
        if (convfn[0] == '.' &&
            (!convfn[1] || (convfn[1] == '.' && !convfn[2])))
            continue;

        // When the `match-hidden-files' variable is disabled and a filename was
        // given, only include filenames beginning with '.' if the pattern
        // started with '.' or with a wildcard.  This is reachable because
        // Windows skips leading '.' when matching (the file ".foo" matches the
        // pattern "foo").
        if (_rl_match_hidden_files == 0 && HIDDEN_FILE(convfn))
            if (filename[0] != '.')
                continue;

        if (filename_len == 0 ||
            complete_fncmp(convfn, convlen, filename, filename_len))
            break;
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
static char** alternative_matches(const char* text, int start, int end)
{
// TODO: Use s_matches?  Or maybe generate matches at the moment of completion
// so clink isn't doing arbitrary completion IO while you're typing?
    if (!s_matches)
        return nullptr;

    int match_count = s_matches->get_match_count();
    if (!match_count)
    {
        str<32> s(text);
        path::normalise(s);
        assert(rl_buffer);
        assert(rl_point == end);
        if (!s.equals(text))
        {
            rl_buffer->begin_undo_group();
            rl_buffer->remove(start, end);
            rl_point = start;
            rl_buffer->insert(s.c_str());
            rl_point = start + s.length();
            rl_buffer->end_undo_group();
        }
        return nullptr;
    }

    rl_filename_completion_desired = 1;

    // Identify common prefix.
    char* end_prefix = rl_last_path_separator(text);
    if (end_prefix)
        end_prefix++;
    else if (ISALPHA((unsigned char)text[0]) && text[1] == ':')
        end_prefix = (char*)text + 2;

    // Deep copy of the generated matches.  Inefficient, but this is how
    // readline wants them.
    str<32> lcd;
    int past_flag = 1;
    char** matches = (char**)calloc(match_count + 2, sizeof(*matches));
    matches[0] = (char*)malloc(past_flag + (end - start) + 1);
    if (past_flag)
        matches[0][0] = (char)match_type::none;
    memcpy(matches[0] + past_flag, text, end - start);
    matches[0][past_flag + (end - start)] = '\0';
    for (int i = 0; i < match_count; ++i)
    {
        int len_prefix = end_prefix ? end_prefix - text : 0;
        match_type type = past_flag ? (match_type)s_matches->get_match_type(i) : match_type::none;

        const char* match = s_matches->get_match(i);
        int match_len = strlen(match);
        int match_size = past_flag + len_prefix + match_len + 1;
        matches[i + 1] = (char*)malloc(match_size);

        if (past_flag)
            matches[i + 1][0] = (char)s_matches->get_match_type(i);

        str_base str(matches[i + 1] + past_flag, match_size - past_flag);
        str.clear();
        if (len_prefix)
            str.concat(text, len_prefix);

        if ((type == match_type::none || type == match_type::dir) &&
            (path::is_separator(match[match_len - 1])))
            match_len--;

        str.concat(match, match_len);
    }
    matches[match_count + 1] = nullptr;

    rl_completion_matches_include_type = past_flag;
    rl_attempted_completion_over = 1;
    return matches;
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
    if (stream == in_stream)
    {
        assert(s_processed_input);
        return s_processed_input->read();
    }

    if (stream == null_stream)
        return 0;

    assert(false);
    return fgetc(stream);
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const char* chars, int char_count)
{
    if (stream == out_stream)
    {
        assert(s_printer);
        s_printer->print(chars, char_count);
        return;
    }

    if (stream == null_stream)
        return;

    if (stream == stderr || stream == stdout)
    {
        DWORD dw;
        HANDLE h = GetStdHandle(stream == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &dw))
        {
            wstr<32> s;
            to_utf16(s, str_iter(chars, char_count));
            WriteConsoleW(h, s.c_str(), s.length(), &dw, nullptr);
        }
        else
        {
            WriteFile(h, chars, char_count, &dw, nullptr);
        }
        return;
    }

    assert(false);
    fwrite(chars, char_count, 1, stream);
}

//------------------------------------------------------------------------------
static void terminal_fflush_thunk(FILE* stream)
{
    if (stream != out_stream && stream != null_stream)
        fflush(stream);
}

//------------------------------------------------------------------------------
typedef const char* two_strings[2];
static void bind_keyseq_list(const two_strings* list, Keymap map)
{
    for (int i = 0; list[i][0]; ++i)
        rl_bind_keyseq_in_map(list[i][0], rl_named_function(list[i][1]), map);
}



//------------------------------------------------------------------------------
rl_module::rl_module(const char* shell_name, terminal_in* input)
: m_rl_buffer(nullptr)
, m_prev_group(-1)
{
    assert(!s_direct_input);
    s_direct_input = input;

    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_fflush_function = terminal_fflush_thunk;
    rl_instream = in_stream;
    rl_outstream = out_stream;

    rl_readline_name = shell_name;
    rl_catch_signals = 0;
    _rl_comment_begin = savestring("::"); // this will do...

    // Readline needs a tweak of it's handling of 'meta' (i.e. IO bytes >=0x80)
    // so that it handles UTF-8 correctly (convert=input, output=output)
    _rl_convert_meta_chars_to_ascii = 0;
    _rl_output_meta_chars = 1;

    // Recognize both / and \\ as path separators, and normalize to \\.
    rl_backslash_path_sep = 1;
    rl_preferred_path_separator = '\\';

    // Quote spaces in completed filenames.
    rl_filename_quoting_desired = 1;
    rl_completer_quote_characters = "\"";
    rl_basic_quote_characters = "\"";
    rl_filename_quote_characters = " &^";

    // Completion and match display.
    // TODO: postprocess_matches is for better quote handling.
    //rl_ignore_some_completions_function = postprocess_matches;
    rl_attempted_completion_function = alternative_matches;
    rl_menu_completion_entry_function = filename_menu_completion_function;
    rl_read_key_hook = read_key_hook;

    // Add commands.
    static bool s_rl_initialized = false;
    if (!s_rl_initialized)
    {
        s_rl_initialized = true;
        rl_add_history_hook = host_add_history;
        rl_remove_history_hook = host_remove_history;
        rl_add_funmap_entry("clink-reset-line", clink_reset_line);
        rl_add_funmap_entry("clink-exit", clink_exit);
        rl_add_funmap_entry("clink-ctrl-c", clink_ctrl_c);
        rl_add_funmap_entry("clink-paste", clink_paste);
        rl_add_funmap_entry("clink-copy-line", clink_copy_line);
        rl_add_funmap_entry("clink-copy-cwd", clink_copy_cwd);
        rl_add_funmap_entry("clink-up-directory", clink_up_directory);
        rl_add_funmap_entry("clink-insert-dot-dot", clink_insert_dot_dot);
    }

    // Bind extended keys so editing follows Windows' conventions.
    static const char* ext_key_binds[][2] = {
        { "\\e[1;5D",       "backward-word" },           // ctrl-left
        { "\\e[1;5C",       "forward-word" },            // ctrl-right
        { "\\e[F",          "end-of-line" },             // end
        { "\\e[H",          "beginning-of-line" },       // home
        { "\\e[3~",         "delete-char" },             // del
        { "\\e[1;5F",       "kill-line" },               // ctrl-end
        { "\\e[1;5H",       "backward-kill-line" },      // ctrl-home
        { "\\e[5~",         "history-search-backward" }, // pgup
        { "\\e[6~",         "history-search-forward" },  // pgdn
        { "\\e[3;5~",       "kill-word" },               // ctrl-del
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        { "\\e[2~",         "overwrite-mode" },          // ins
        { "\\e[27;27~",     "clink-reset-line" },        // esc

        { "\\C-c",          "clink-ctrl-c" },            // ctrl-c
        { "\\C-v",          "clink-paste" },             // ctrl-v
        { "\\C-z",          "undo" },                    // ctrl-z
        { "\\e\\eOS",       "clink-exit" },              // alt-f4

        { "\\M-a",          "clink-insert-dot-dot" },    // alt-a
        { "\\M-c",          "clink-copy-cwd" },          // alt-c
        { "\\M-\\C-c",      "clink-copy-line" },         // alt-ctrl-c
        { "\\M-\\C-e",      "clink-expand-env-var" },    // alt-ctrl-e
        { "\\M-\\C-f",      "clink-expand-doskey-alias" }, // alt-ctrl-f
        { "\\e[5;5~",       "clink-up-directory" },      // ctrl-pgup
        {}
    };

    int restore_convert = _rl_convert_meta_chars_to_ascii;
    _rl_convert_meta_chars_to_ascii = 1;
    bind_keyseq_list(ext_key_binds, emacs_standard_keymap);
    _rl_convert_meta_chars_to_ascii = restore_convert;

    load_user_inputrc();
}

//------------------------------------------------------------------------------
rl_module::~rl_module()
{
    free(_rl_comment_begin);
    _rl_comment_begin = nullptr;

    s_direct_input = nullptr;
}

//------------------------------------------------------------------------------
void rl_module::set_keyseq_len(int len)
{
    assert(m_insert_next_len == 0);
    if (rl_is_insert_next_callback_pending())
        m_insert_next_len = len;
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
    s_printer = &context.printer;
    rl_buffer = &context.buffer;

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

    if (_rl_colored_stats || _rl_colored_completion_prefix)
        _rl_parse_colors();

    _rl_pager_color = nullptr;
    if (build_color_sequence(g_colour_interact.get(), m_pager_color))
        _rl_pager_color = m_pager_color.c_str();

    _rl_hidden_color = nullptr;
    if (build_color_sequence(g_colour_hidden.get(), m_hidden_color))
        _rl_hidden_color = m_hidden_color.c_str();

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

    rl_buffer = nullptr;
    s_printer = nullptr;
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
        virtual key_tester* set_key_tester(key_tester* keys) override { return nullptr; }
        const char*  data;
    } term_in;

    term_in.data = input.keys;

    terminal_in* old_input = s_processed_input;
    s_processed_input = &term_in;
    s_matches = &context.matches;

    // Call Readline's until there's no characters left.
    int is_inc_searching = rl_readline_state & RL_STATE_ISEARCH;
    unsigned int len = input.len;
    while (len && !m_done)
    {
        bool is_quoted_insert = rl_is_insert_next_callback_pending();

        --len;
        rl_callback_read_char();

        // Internally Readline tries to resend escape characters but it doesn't
        // work with how Clink uses Readline. So we do it here instead.
        if (term_in.data[-1] == 0x1b && is_inc_searching)
        {
            assert(!is_quoted_insert);
            --term_in.data;
            ++len;
            is_inc_searching = 0;
        }

        if (m_insert_next_len > 0)
        {
            if (is_quoted_insert && --m_insert_next_len)
                rl_quoted_insert(1, 0);
            else
                m_insert_next_len = 0;
        }
    }

    s_matches = nullptr;
    s_processed_input = old_input;

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
