// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"
#include "rl_commands.h"
#include "line_buffer.h"
#include "line_state.h"
#include "matches.h"
#include "match_pipeline.h"
#include "matches_lookaside.h"
#include "word_classifier.h"
#include "word_classifications.h"
#include "popup.h"
#include "textlist_impl.h"
#include "match_colors.h"
#include "display_matches.h"
#include "display_readline.h"
#include "clink_ctrlevent.h"
#include "clink_rl_signal.h"
#include "sticky_search.h"
#include "line_editor_integration.h"
#include "rl_integration.h"
#include "suggestions.h"
#include "slash_translation.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_compare.h>
#include <core/str_hash.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/log.h>
#include <core/debugheap.h>
#include <core/callstack.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>
#include <terminal/printer.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_helpers.h>
#include <terminal/key_tester.h>
#include <terminal/screen_buffer.h>
#include <terminal/scroll.h>

#include <signal.h>
#include <unordered_set>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/history.h>
#include <readline/histlib.h>
#include <readline/keymaps.h>
#include <readline/xmalloc.h>
#include <compat/dirent.h>
#include <readline/posixdir.h>
#include <readline/history.h>
extern int32 _rl_get_inserted_char(void);
extern char* tgetstr(const char*, char**);
extern int32 tputs(const char* str, int32 affcnt, int32 (*putc_func)(int32));
extern char* tgoto(const char* base, int32 x, int32 y);
extern Keymap _rl_dispatching_keymap;
extern int _rl_default_init_file_optional_set;
}

//------------------------------------------------------------------------------
static FILE*        null_stream = (FILE*)1;
static FILE*        in_stream = (FILE*)2;
static FILE*        out_stream = (FILE*)3;
const int32 RL_RESET_STATES = ~(RL_STATE_INITIALIZED|
                                RL_STATE_TERMPREPPED|
                                RL_STATE_OVERWRITE|
                                RL_STATE_CALLBACK|
                                RL_STATE_VICMDONCE|
                                RL_STATE_DONE|
                                RL_STATE_TIMEOUT|
                                RL_STATE_EOF);
const int32 RL_MORE_INPUT_STATES = (RL_STATE_READCMD|
                                    RL_STATE_METANEXT|
                                    RL_STATE_DISPATCHING|
                                    RL_STATE_MOREINPUT|
                                    RL_STATE_ISEARCH|
                                    RL_STATE_NSEARCH|
                                    RL_STATE_SEARCH|
                                    RL_STATE_NUMERICARG|
                                    RL_STATE_MACROINPUT|
                                    RL_STATE_MACRODEF|
                                    RL_STATE_INPUTPENDING|
                                    RL_STATE_VIMOTION|
                                    RL_STATE_MULTIKEY|
                                    RL_STATE_CHARSEARCH|
                                    RL_STATE_READSTR);
const int32 RL_SIMPLE_INPUT_STATES = (RL_STATE_MOREINPUT|
                                      RL_STATE_NSEARCH|
                                      RL_STATE_CHARSEARCH|
                                      RL_STATE_READSTR);

extern "C" {
extern void         (*rl_fwrite_function)(FILE*, const char*, int);
extern void         (*rl_fflush_function)(FILE*);
extern char*        _rl_comment_begin;
extern int          _rl_convert_meta_chars_to_ascii;
extern int          _rl_output_meta_chars;
#if defined(PLATFORM_WINDOWS)
extern int          _rl_last_v_pos;
#endif
} // extern "C"

extern setting_color g_color_interact;
extern int32 g_prompt_refilter;
extern int32 g_prompt_redisplay;

static terminal_in* s_direct_input = nullptr;       // for read_key_hook
static terminal_in* s_processed_input = nullptr;    // for read thunk

// TODO: Refactor to avoid globals.
line_buffer*        g_rl_buffer = nullptr;
pager*              g_pager = nullptr;
editor_module::result* g_result = nullptr;

static str<>        s_last_prompt;
static str_moveable s_needle;
static bool         s_need_collect_words = false;

static suggestion_manager s_suggestion;

//------------------------------------------------------------------------------
setting_bool g_classify_words(
    "clink.colorize_input",
    "Colorize the input text",
    "When enabled, this colors the words in the input line based on the argmatcher\n"
    "Lua scripts.",
    true);

// This is here because it's about Readline, not CMD, and exposing it from
// host_cmd.cpp caused linkage errors for the tests.
setting_bool g_ctrld_exits(
    "cmd.ctrld_exits",
    "Pressing Ctrl-D exits session",
    "Ctrl-D exits cmd.exe when used on an empty line.",
    true);

static setting_color g_color_arg(
    "color.arg",
    "Argument color",
    "The color for arguments in the input line.",
    "bold");

static setting_color g_color_arginfo(
    "color.arginfo",
    "Argument info color",
    "Some argmatchers may show that some flags or arguments accept additional\n"
    "arguments, when listing possible completions.  This color is used for those\n"
    "additional arguments.  (E.g. the \"dir\" in a \"-x dir\" listed completion.)",
    "yellow");

static setting_color g_color_argmatcher(
    "color.argmatcher",
    "Argmatcher color",
    "The color for a command name that has an argmatcher.  If a command name has\n"
    "an argmatcher available, then this color will be used for the command name,\n"
    "otherwise the doskey, cmd, or input color will be used.",
    "");

static setting_color g_color_cmd(
    "color.cmd",
    "Shell command completions",
    "Used when Clink displays shell (CMD.EXE) command completions.",
    "bold");

static setting_color g_color_cmdredir(
    "color.cmdredir",
    "Color for < and > redirection symbols",
    "bold");

static setting_color g_color_cmdsep(
    "color.cmdsep",
    "Color for & and | command separators",
    "bold");

static setting_color g_color_description(
    "color.description",
    "Description completion color",
    "The default color for descriptions of completions.",
    "bright cyan");

static setting_color g_color_doskey(
    "color.doskey",
    "Doskey completions",
    "Used when Clink displays doskey macro completions.",
    "bold cyan");

setting_color g_color_executable(
    "color.executable",
    "Color for executable command word",
    "When set, this is the color in the input line for a command word that is\n"
    "recognized as an executable file.",
    "");

static setting_color g_color_filtered(
    "color.filtered",
    "Filtered completion color",
    "The default color for filtered completions.",
    "bold");

static setting_color g_color_flag(
    "color.flag",
    "Flag color",
    "The color for flags in the input line.",
    "default");

static setting_color g_color_hidden(
    "color.hidden",
    "Hidden file completions",
    "Used when Clink displays file completions with the hidden attribute.",
    "");

setting_color g_color_histexpand(
    "color.histexpand",
    "History expansion color",
    "The color for history expansions in the input line.  When this is not set,\n"
    "history expansions are not colored.",
    "");

static setting_color g_color_horizscroll(
    "color.horizscroll",
    "Horizontal scroll marker color",
    "Used when Clink displays < or > to indicate the input line can scroll\n"
    "horizontally when horizontal-scroll-mode is set.",
    "");

static setting_color g_color_input(
    "color.input",
    "Input text color",
    "Used when Clink displays the input line text.",
    "");

static setting_color g_color_message(
    "color.message",
    "Message area color",
    "The color for the Readline message area (e.g. search prompt, etc).",
    "default");

static setting_color g_color_modmark(
    "color.modmark",
    "Modified history line mark color",
    "Used when Clink displays the * mark on modified history lines when\n"
    "mark-modified-lines is set.",
    "");

static setting_color g_color_prompt(
    "color.prompt",
    "Prompt color",
    "When set, this is used as the default color for the prompt.  But it's\n"
    "overridden by any colors set by prompt filter scripts.",
    "");

static setting_color g_color_readonly(
    "color.readonly",
    "Readonly file completions",
    "Used when Clink displays file completions with the readonly attribute.",
    "");

static setting_color g_color_selected(
    "color.selected_completion",
    "Selected completion color",
    "The color for the selected completion with the clink-select-complete command.",
    "");

static setting_color g_color_selection(
    "color.selection",
    "Selection color",
    "The color for selected text in the input line.",
    "");

setting_color g_color_suggestion(
    "color.suggestion",
    "Color for suggestion text",
    "The color for suggestion text to be inserted at the end of the input line.",
    "");

static setting_color g_color_unexpected(
    "color.unexpected",
    "Unexpected argument color",
    "The color for unexpected arguments in the input line.  An argument is\n"
    "unexpected if an argument matcher expected there to be no more arguments\n"
    "in the input line or if the word doesn't match any expected values.",
    "default");

setting_color g_color_unrecognized(
    "color.unrecognized",
    "Color for unrecognized command word",
    "When set, this is the color in the input line for a command word that is not\n"
    "recognized as a command, doskey macro, directory, argmatcher, or executable\n"
    "file.",
    "");

setting_bool g_match_expand_abbrev(
    "match.expand_abbrev",
    "Expand abbreviated paths when completing",
    "Expands unambiguously abbreviated directories in a path when performing\n"
    "completion.",
    true);

setting_bool g_match_expand_envvars(
    "match.expand_envvars",
    "Expand envvars when completing",
    "Expands environment variables in a word before performing completion.",
    false);

setting_bool g_match_wild(
    "match.wild",
    "Match ? and * wildcards when completing",
    "Matches ? and * wildcards and leading . characters when using any of the\n"
    "completion commands.  Turn this off to behave how bash does, and not match\n"
    "wildcards or leading dots.",
    true);

setting_bool g_prompt_async(
    "prompt.async",
    "Enables asynchronous prompt refresh",
    true);

static setting_bool g_rl_hide_stderr(
    "readline.hide_stderr",
    "Suppress stderr from the Readline library",
    false);

setting_enum g_default_bindings(
    "clink.default_bindings",
    "Selects default key bindings",
    "Clink uses bash key bindings when this is set to 'bash' (the default).\n"
    "When this is set to 'windows' Clink overrides some of the bash defaults with\n"
    "familiar Windows key bindings for Tab, Ctrl+F, Ctrl+M, and some others.",
    "bash,windows",
    0);

extern setting_bool g_debug_log_terminal;
#ifdef _MSC_VER
extern setting_bool g_debug_log_output_callstacks;
#endif
extern setting_bool g_terminal_raw_esc;

extern setting_bool g_autosuggest_enable;
#ifdef USE_SUGGESTION_HINT_INLINE
extern setting_bool g_autosuggest_hint;
#endif



//------------------------------------------------------------------------------
#if !defined(OMIT_DEFAULT_DISPLAY_MATCHES)
static void __cdecl dummy_display_matches_hook(char**, int32, int32)
{
    // This exists purely to prevent rl_complete_internal from setting up
    // _rl_complete_sigcleanup and freeing matches out from under Clink code.
    // Clink uses rl_completion_display_matches_func, which isn't fully
    // integrated into Readline.
}
#endif



//------------------------------------------------------------------------------
ignore_volatile_matches::ignore_volatile_matches(matches_impl& matches)
: m_matches(matches)
, m_volatile(matches.m_volatile)
{
    if (!s_suggestion.can_update_matches() &&
        matches.is_from_current_input_line())
    {
        m_matches.m_volatile = false;
    }
}

//------------------------------------------------------------------------------
ignore_volatile_matches::~ignore_volatile_matches()
{
    m_matches.m_volatile |= m_volatile;
}



//------------------------------------------------------------------------------
static const char* build_color_sequence(const setting_color& setting, str_base& out, bool include_csi = false)
{
    str<> tmp;
    setting.get(tmp);
    if (tmp.empty())
        return nullptr;

    // WARNING:  Can't use format() because it DOESN'T GROW!

    out.clear();

    if (include_csi)
        out << "\x1b[";

    const char* t = tmp.c_str();
    if (t[0] != '0' || t[1] != ';')
        out << "0;";
    out << tmp;

    if (include_csi)
        out << "m";

    return out.c_str();
}

//------------------------------------------------------------------------------
class rl_more_key_tester : public key_tester
{
public:
    virtual bool    is_bound(const char* seq, int32 len) override
                    {
                        if (len <= 1)
                            return true;
                        // Unreachable; gets handled by translate.
                        assert(!bindableEsc || strcmp(seq, bindableEsc) != 0);
                        rl_ding();
                        return false;
                    }
    virtual bool    translate(const char* seq, int32 len, str_base& out) override
                    {
                        if (bindableEsc && strcmp(seq, bindableEsc) == 0)
                        {
                            out = "\x1b";
                            return true;
                        }
                        return false;
                    }
private:
    const char* bindableEsc = get_bindable_esc();
};

//------------------------------------------------------------------------------
static bool is_readline_input_pending()
{
    assert(!!rl_pending_input == !!RL_ISSTATE(RL_STATE_INPUTPENDING));
    return (rl_pending_input ||
            _rl_pushed_input_available() ||
            RL_ISSTATE(RL_STATE_INPUTPENDING|RL_STATE_MACROINPUT) ||
            rl_executing_macro);
}

//------------------------------------------------------------------------------
static uint32* s_input_len_ptr = nullptr;
static bool s_input_more = false;
extern "C" int32 input_available_hook(void)
{
    assert(s_direct_input);
    if (s_direct_input)
    {
        // These are in order of next-ness:

        // Any remaining read-but-not-processed bytes in input chord?
        if (s_input_len_ptr && *s_input_len_ptr > 0)
            return true;

        // Any read-but-not-processed bytes not yet in input chord?  The binding
        // resolver may have more bytes pending.
        if (s_input_more)
            return true;

        // Any pending input from Readline?
        if (is_readline_input_pending())
            return true;

        // Any unread input available from stdin?
        // Passing -1 returns the current timeout without changing it.  The
        // timeout is in microseconds (µsec) so divide by 1000 for milliseconds.
        const int32 timeout = rl_set_keyboard_input_timeout(-1);
        if (s_direct_input->available(timeout > 0 ? timeout / 1000 : 0))
        {
            // Buffers the available input so it's available to Readline for
            // reading.  Clink's terminal_read_thunk is designed to require a
            // loop of select() and read() in order to control how/when/whether
            // Readline sees input.  It's necessary to call select here so
            // that win_terminal_in has the input queued, otherwise rl_read_key
            // won't be able to receive the available input.
            s_direct_input->select(nullptr, 0);
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
extern "C" int32 read_key_hook(void)
{
    assert(s_direct_input);
    if (!s_direct_input)
        return 0;

    rl_more_key_tester tester;
    key_tester* old = s_direct_input->set_key_tester(&tester);

    s_direct_input->select();
    int32 key = s_direct_input->read();

    s_direct_input->set_key_tester(old);
    return key;
}

//------------------------------------------------------------------------------
int32 read_key_direct(bool wait)
{
    if (!s_direct_input)
    {
        assert(false);
        return -1;
    }

    key_tester* old = s_direct_input->set_key_tester(nullptr);

    if (wait)
        s_direct_input->select();
    int32 key = s_direct_input->read();

    s_direct_input->set_key_tester(old);
    return key;
}

//------------------------------------------------------------------------------
static bool find_func_in_keymap(str_base& out, rl_command_func_t *func, Keymap map)
{
    for (int32 key = 0; key < KEYMAP_SIZE; key++)
    {
        switch (map[key].type)
        {
        case ISMACR:
            break;
        case ISFUNC:
            if (map[key].function == func)
            {
                char ch = char(uint8(key));
                out.concat_no_truncate(&ch, 1);
                return true;
            }
            break;
        case ISKMAP:
            {
                uint32 old_len = out.length();
                char ch = char(uint8(key));
                out.concat_no_truncate(&ch, 1);
                if (find_func_in_keymap(out, func, FUNCTION_TO_KEYMAP(map, key)))
                    return true;
                out.truncate(old_len);
            }
            break;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
static bool find_abort_in_keymap(str_base& out)
{
    rl_command_func_t *func = rl_named_function("abort");
    if (!func)
        return false;

    Keymap map = rl_get_keymap();
    return find_func_in_keymap(out, func, map);
}



//------------------------------------------------------------------------------
enum class shell_integration_mode { none, winterminal };
static shell_integration_mode s_in_command_output = shell_integration_mode::none;
extern "C" void terminal_begin_command()
{
    // Only print begin code once per block of command output.
    if (s_in_command_output != shell_integration_mode::none)
        return;

    // Find which terminal shell integration mode to use.
    const ansi_handler native = get_native_ansi_handler();
    const char* sequence = nullptr;
    size_t len = 0;
    shell_integration_mode mode = shell_integration_mode::none;
    switch (native)
    {
    case ansi_handler::winterminal:
        {
            // Windows Terminal built-in shell integration (WT v1.18).
            static constexpr char c_wt_begincommand[] = "\x1b]133;C\a";
            constexpr size_t c_len = _countof(c_wt_begincommand) - 1;
            static_assert(c_len == 8, "unexpected string length");
            assert(c_len == strlen(c_wt_begincommand));

            sequence = c_wt_begincommand;
            len = c_len;
            mode = shell_integration_mode::winterminal;
        }
        break;
    }

    // Emit sequence if a shell integration mode is chosen.
    if (sequence && len && mode != shell_integration_mode::none)
    {
        rl_fwrite_function(_rl_out_stream, sequence, len);
        s_in_command_output = mode;
    }
}

//------------------------------------------------------------------------------
extern "C" void terminal_end_command()
{
    // Only print end code one per block of command output.
    if (s_in_command_output == shell_integration_mode::none)
        return;

    // Clear the shell integration mode.
    const shell_integration_mode mode = s_in_command_output;
    s_in_command_output = shell_integration_mode::none;

    // Emit sequence as appropriate.
    switch (mode)
    {
    case shell_integration_mode::winterminal:
        {
            str<16> tmp;
            tmp.format("\x1b]133;D;%u\a", os::get_errorlevel());
            rl_fwrite_function(_rl_out_stream, tmp.c_str(), tmp.length());
        }
        break;
    }
}

//------------------------------------------------------------------------------
static int32 terminal_read_thunk(FILE* stream)
{
    if (stream == in_stream)
    {
        if (RL_ISSTATE(RL_STATE_READSTR))
        {
            assert(s_direct_input);
            s_direct_input->select();
            return s_direct_input->read();
        }
        else
        {
            assert(s_processed_input);
            return s_processed_input->read();
        }
    }

    if (stream == null_stream)
        return 0;

    assert(false);
    return fgetc(stream);
}

//------------------------------------------------------------------------------
static void terminal_write_thunk(FILE* stream, const char* chars, int32 char_count)
{
    if (stream == out_stream)
    {
        assert(g_printer);
        g_printer->print(chars, char_count);
        return;
    }

    if (stream == null_stream)
        return;

    if (stream == stderr || stream == stdout)
    {
        if (stream == stderr && g_rl_hide_stderr.get())
            return;

        DWORD dw;
        HANDLE h = GetStdHandle(stream == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &dw))
        {
            wstr<32> s;
            str_iter tmpi(chars, char_count);
            to_utf16(s, tmpi);
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
static int32 s_puts_face = 0;
static void terminal_log_write(FILE* stream, const char* chars, int32 char_count)
{
    suppress_implicit_write_console_logging nolog;

    if (stream == out_stream)
    {
        assert(g_printer);
        LOGCURSORPOS();
        LOG("%s \"%.*s\", %d", s_puts_face ? "PUTSFACE" : "RL_OUTSTREAM", char_count, chars, char_count);
#ifdef _MSC_VER
        if (g_debug_log_output_callstacks.get())
        {
            char stk[8192];
            format_callstack(2, 20, stk, sizeof(stk), false);
            LOG("%s", stk);
        }
#endif
        g_printer->print(chars, char_count);
        return;
    }

    if (stream == null_stream)
        return;

    if (stream == stderr || stream == stdout)
    {
        if (stream == stderr && g_rl_hide_stderr.get())
            return;

        DWORD dw;
        HANDLE h = GetStdHandle(stream == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (GetConsoleMode(h, &dw))
        {
            LOGCURSORPOS(h);
            LOG("%s \"%.*s\", %d", (stream == stderr) ? "CONERR" : "CONOUT", char_count, chars, char_count);
            wstr<32> s;
            str_iter tmpi(chars, char_count);
            to_utf16(s, tmpi);
            WriteConsoleW(h, s.c_str(), s.length(), &dw, nullptr);
        }
        else
        {
            LOG("%s \"%.*s\", %d", (stream == stderr) ? "FILEERR" : "FILEOUT", char_count, chars, char_count);
            WriteFile(h, chars, char_count, &dw, nullptr);
        }
        return;
    }

    assert(false);
    LOGCURSORPOS();
    LOG("FWRITE \"%.*s\", %d", char_count, chars, char_count);
    fwrite(chars, char_count, 1, stream);
}

//------------------------------------------------------------------------------
static void terminal_log_read(int c, const char* _src)
{
    str<16> src;
    if (_src)
        src << "  (" << _src << ")";

    if (c >= 0 && c < 0x80)
        LOG("INPUT 0x%02x \"%c\"%s", c, c, src.c_str());
    else
        LOG("INPUT 0x%02x%s", c, src.c_str());
}

//------------------------------------------------------------------------------
static void terminal_fflush_thunk(FILE* stream)
{
    if (stream != out_stream && stream != null_stream)
        fflush(stream);
}



//------------------------------------------------------------------------------
static const word_classifications* s_classifications = nullptr;
static const char* s_input_color = nullptr;
static const char* s_selection_color = nullptr;
static const char* s_argmatcher_color = nullptr;
static const char* s_executable_color = nullptr;
static const char* s_arg_color = nullptr;
static const char* s_flag_color = nullptr;
static const char* s_unrecognized_color = nullptr;
static const char* s_none_color = nullptr;
static const char* s_suggestion_color = nullptr;
static const char* s_histexpand_color = nullptr;
int32 g_suggestion_offset = -1;
bool g_suggestion_includes_hint = false;

//------------------------------------------------------------------------------
// This counts the number of screen lines needed to draw prompt_prefix.
//
// Why:  Readline expands the prompt string into a prefix and the last line of
// the prompt.  Readline draws the prefix only once.  To asynchronously filter
// the prompt again after it's already been displayed, it's necessary to draw
// the prefix again.  To do that, it's necessary to know how many lines to move
// up to reach the beginning of the prompt prefix.
//
// Note:  This only counts whole lines; i.e. caused by newline or wrapping.
int32 count_prompt_lines(const char* prompt_prefix)
{
    if (!prompt_prefix || !*prompt_prefix)
        return 0;

    assert(_rl_screenwidth > 0);
    int32 width = _rl_screenwidth;

    int32 lines = 0;
    int32 cells = 0;
    bool ignore = false;

    str<> bracketed;
    ecma48_processor_flags flags = ecma48_processor_flags::bracket;
    ecma48_processor(prompt_prefix, &bracketed, nullptr/*cell_count*/, flags);

    wcwidth_iter iter(bracketed.c_str(), bracketed.length());
    while (int32 c = iter.next())
    {
        if (ignore)
        {
            if (c == RL_PROMPT_END_IGNORE)
                ignore = false;
            continue;
        }
        if (c == RL_PROMPT_START_IGNORE)
        {
            ignore = true;
            continue;
        }

        if (c == '\r')
        {
            cells = 0;
            continue;
        }
        if (c == '\n')
        {
            lines++;
            cells = 0;
            continue;
        }

        int32 w = iter.character_wcwidth_onectrl();
        if (cells + w > width)
        {
            lines++;
            cells = 0;
        }
        cells += w;
    }

    return lines;
}

//------------------------------------------------------------------------------
static char get_face_func(int32 in, int32 active_begin, int32 active_end)
{
    assertimplies(g_suggestion_offset != -1, g_autosuggest_enable.get());
    if (0 <= g_suggestion_offset && g_suggestion_offset <= in)
    {
#ifdef USE_SUGGESTION_HINT_INLINE
        if (g_suggestion_includes_hint)
        {
            if (in >= rl_end + IDX_SUGGESTION_LINK_TEXT)
                return FACE_SUGGESTIONLINK;
            else if (in >= rl_end + IDX_SUGGESTION_KEY_BEGIN && in < rl_end + IDX_SUGGESTION_KEY_END)
                return FACE_SUGGESTIONKEY;
        }
#endif
        return FACE_SUGGESTION;
    }

    if (in >= active_begin && in < active_end)
        return FACE_STANDOUT;

    if (cua_point_in_selection(in) || point_in_select_complete(in))
        return FACE_SELECTION;

    if (s_classifications)
    {
        char face = s_classifications->get_face(in);
        if (face != FACE_SPACE)
            return face;
    }

    return s_input_color ? FACE_INPUT : FACE_NORMAL;
}

//------------------------------------------------------------------------------
inline const char* fallback_color(const char* preferred, const char* fallback)
{
    return preferred ? preferred : fallback;
}

//------------------------------------------------------------------------------
static void puts_face_func(const char* s, const char* face, int32 n)
{
    static const char c_normal[] = "\x1b[m";
    static const char c_hyperlink[] = "\x1b]8;;";
    static const char c_BEL[] = "\a";
    static const char c_doc_histexpand[] = "https://chrisant996.github.io/clink/clink.html#using-history-expansion";
#ifdef USE_SUGGESTION_HINT_INLINE
    static const char c_doc_autosuggest[] = DOC_HYPERLINK_AUTOSUGGEST;
#endif

    str<280> out;
    const char* const other_color = fallback_color(s_input_color, c_normal);
    char cur_face = FACE_NORMAL;
    bool hyperlink = false;

    while (n)
    {
        // Append face string if face changed.
        if (cur_face != *face)
        {
            if (hyperlink)
            {
                out << c_hyperlink << c_BEL;
                hyperlink = false;
            }

            cur_face = *face;
            switch (cur_face)
            {
            default:
                if (s_classifications)
                {
                    const char* color = s_classifications->get_face_output(cur_face);
                    if (color)
                    {
                        out << "\x1b[";
                        if (color[0] != '0' || color[1] != ';')
                            out << "0;";
                        out << color << "m";
                        break;
                    }
                }
                // fall through
            case FACE_NORMAL:       out << c_normal; break;
            case FACE_STANDOUT:     out << fallback_color(_rl_active_region_start_color, "\x1b[0;7m"); break;

            case FACE_INPUT:        out << fallback_color(s_input_color, c_normal); break;
            case FACE_MODMARK:      out << fallback_color(_rl_display_modmark_color, c_normal); break;
            case FACE_MESSAGE:      out << fallback_color(_rl_display_message_color, c_normal); break;
            case FACE_SCROLL:       out << fallback_color(_rl_display_horizscroll_color, c_normal); break;
            case FACE_SELECTION:    out << fallback_color(s_selection_color, "\x1b[0;7m"); break;

            case FACE_HISTEXPAND:
                out << fallback_color(s_histexpand_color, "\x1b[0;97;45m") << c_hyperlink << c_doc_histexpand << c_BEL;
                hyperlink = true;
                break;

            case FACE_SUGGESTION:
#ifdef USE_SUGGESTION_HINT_INLINE
            case FACE_SUGGESTIONKEY:
            case FACE_SUGGESTIONLINK:
#endif
                assert(g_autosuggest_enable.get());
                if (s_suggestion_color)
                    out << s_suggestion_color;
                else
                {
#ifdef AUTO_DETECT_CONSOLE_COLOR_THEME
                    switch (get_console_theme())
                    {
                    case console_theme::light:
                    case console_theme::dark:
                        {
                            str<16> faint;
                            faint.format(";%u", get_console_faint_text());
                            out << "\x1b[0;38;2" << faint << faint << faint << "m";
                        }
                        break;
                    default:
                        out << "\x1b[0;90m";
                        break;
                    }
#else
                    out << "\x1b[0;90m";
#endif
                }
#ifdef USE_SUGGESTION_HINT_INLINE
                if (cur_face == FACE_SUGGESTIONKEY)
                    out << "\x1b[7m";
                else if (cur_face == FACE_SUGGESTIONLINK)
                {
                    out << c_hyperlink << c_doc_autosuggest << c_BEL;
                    hyperlink = true;
                }
#endif
                break;

            case FACE_OTHER:        out << other_color; break;
            case FACE_UNRECOGNIZED: out << fallback_color(s_unrecognized_color, other_color); break;
            case FACE_EXECUTABLE:   out << fallback_color(s_executable_color, other_color); break;
            case FACE_COMMAND:
                if (_rl_command_color)
                    out << "\x1b[" << _rl_command_color << "m";
                else
                    out << c_normal;
                break;
            case FACE_ALIAS:
                if (_rl_alias_color)
                    out << "\x1b[" << _rl_alias_color << "m";
                else
                    out << c_normal;
                break;
            case FACE_ARGMATCHER:
                assert(s_argmatcher_color); // Shouldn't reach here otherwise.
                if (s_argmatcher_color) // But avoid crashing, just in case.
                    out << s_argmatcher_color;
                break;
            case FACE_ARGUMENT:     out << fallback_color(s_arg_color, fallback_color(s_input_color, c_normal)); break;
            case FACE_FLAG:         out << fallback_color(s_flag_color, c_normal); break;
            case FACE_NONE:         out << fallback_color(s_none_color, c_normal); break;
            }
        }

        // Get run of characters with the same face.
        const char* s_concat = s;
        const char* face_concat = face;
        while (n && cur_face == *face)
        {
            s++;
            face++;
            n--;
        }

        // Append the characters.
        int32 len = int32(s - s_concat);
        out.concat(s_concat, len);
    }

    if (hyperlink)
        out << c_hyperlink << c_BEL;
    if (cur_face != FACE_NORMAL)
        out << c_normal;

    ++s_puts_face;
    rl_fwrite_function(_rl_out_stream, out.c_str(), out.length());
    --s_puts_face;
}



//------------------------------------------------------------------------------
void set_suggestion_started(const char* line)
{
    s_suggestion.set_started(line);
}

//------------------------------------------------------------------------------
void set_suggestion(const char* line, uint32 endword_offset, const char* suggestion, uint32 offset)
{
    s_suggestion.set(line, endword_offset, suggestion, offset);
}

//------------------------------------------------------------------------------
static bool s_force_signaled_redisplay = false;
void force_signaled_redisplay()
{
    s_force_signaled_redisplay = true;
}

//------------------------------------------------------------------------------
static void hook_display()
{
    struct clear_want { ~clear_want() { _rl_want_redisplay = false; } } clear_want;

    static bool s_busy = false;
    if (s_busy)
        return;
    rollback<bool> rb(s_busy, true);

    // Readline callback mode seems to have some problems with how redisplay
    // works.  It shows the old buffer and shows the prompt at an inopportune
    // time.  So just disable it so Clink can drive when redisplay happens.
    if (clink_is_signaled())
    {
        if (!s_force_signaled_redisplay)
            return;
        s_force_signaled_redisplay = false;
    }

    if (!s_suggestion.more() || rl_point != rl_end)
    {
        display_readline();
        return;
    }

    assert(g_autosuggest_enable.get());
    assert(g_suggestion_offset < 0);
    assert(!g_suggestion_includes_hint);

    rollback<int32> rb_sugg_offset(g_suggestion_offset, rl_end);
    rollback<bool> rb_sugg_includes_hint(g_suggestion_includes_hint, false);
    rollback<char*> rb_buf(rl_line_buffer);
    rollback<int32> rb_len(rl_line_buffer_len);
    rollback<int32> rb_end(rl_end);

    str_moveable tmp;
    if (s_suggestion.get_visible(tmp, &g_suggestion_includes_hint))
    {
        rl_line_buffer = tmp.data();
        rl_line_buffer_len = tmp.length();
        rl_end = tmp.length();
    }

    display_readline();
}

//------------------------------------------------------------------------------
bool can_suggest_internal(const line_state& line)
{
    return s_suggestion.can_suggest(line);
}

//------------------------------------------------------------------------------
void suppress_suggestions()
{
    s_suggestion.suppress_suggestions();
}

//------------------------------------------------------------------------------
bool has_suggestion()
{
    return s_suggestion.has_suggestion();
}

//------------------------------------------------------------------------------
bool insert_suggestion(suggestion_action action)
{
    return s_suggestion.insert(action);
}

//------------------------------------------------------------------------------
bool pause_suggestions(bool pause)
{
    return s_suggestion.pause(pause);
}

//------------------------------------------------------------------------------
extern "C" void clear_suggestion()
{
    s_suggestion.clear();
    if (g_rl_buffer)
        g_rl_buffer->draw();
}

//------------------------------------------------------------------------------
bool can_show_suggestion_hint()
{
    if (g_autosuggest_hint.get())
    {
        int32 type;
        rl_command_func_t* func = rl_function_of_keyseq_len("\x1b[C", 3, nullptr, &type);
        if (type == ISFUNC &&
            (func == win_f1 ||
             func == clink_forward_char ||
             func == clink_forward_byte ||
             func == clink_end_of_line))
            return true;
    }
    return false;
}



//------------------------------------------------------------------------------
static const matches* s_matches = nullptr;

//------------------------------------------------------------------------------
extern "C" void free_match_list_hook(char** matches)
{
    destroy_matches_lookaside(matches);
}

//------------------------------------------------------------------------------
static int32 complete_fncmp(const char *convfn, int32 convlen, const char *filename, int32 filename_len)
{
    // We let the OS handle wildcards, so not much to do here.  And we ignore
    // _rl_completion_case_fold because (1) this is Windows and (2) the
    // alternative is to write our own wildcard matching implementation.
    return 1;
}

//------------------------------------------------------------------------------
static void adjust_completion_defaults()
{
    if (!s_matches || !g_rl_buffer)
        return;

    if (g_match_expand_envvars.get())
    {
        const int32 word_break = s_matches->get_word_break_position();
        const int32 word_len = g_rl_buffer->get_cursor() - word_break;
        const char* buffer = g_rl_buffer->get_buffer();

#ifdef DEBUG
        const int32 dbg_row = dbg_get_env_int("DEBUG_EXPANDENVVARS");
        if (dbg_row > 0)
        {
            str<> tmp;
            tmp.format("\x1b[s\x1b[%dHexpand envvars in:  ", dbg_row);
            g_printer->print(tmp.c_str(), tmp.length());
            tmp.format("\x1b[0;37;7m%.*s\x1b[m", word_len, buffer + word_break);
            g_printer->print(tmp.c_str(), tmp.length());
            g_printer->print("\x1b[K\x1b[u");
        }
#endif

        str<> out;
        if (os::expand_env(buffer + word_break, word_len, out))
        {
            const bool quoted = (rl_filename_quote_characters &&
                                rl_completer_quote_characters &&
                                *rl_completer_quote_characters &&
                                word_break > 0 &&
                                buffer[word_break - 1] == *rl_completer_quote_characters);
            const bool need_quote = !quoted && _rl_strpbrk(out.c_str(), rl_filename_quote_characters);
            const char qc = need_quote ? *rl_completer_quote_characters : '\0';
            const char qs[2] = { qc };
            bool close_quote = qc && buffer[word_break + word_len] != qc;

            g_rl_buffer->begin_undo_group();
            g_rl_buffer->set_cursor(word_break);
            g_rl_buffer->remove(word_break, word_break + word_len);
            if (qc)
                g_rl_buffer->insert(qs);
            g_rl_buffer->insert(out.c_str());
            if (close_quote)
                g_rl_buffer->insert(qs);
            g_rl_buffer->end_undo_group();

            force_update_internal(false); // Update needle since line changed.
            reset_generate_matches();
            return;
        }
    }

    if (rl_completion_type == '%' && g_default_bindings.get() == 1)
    {
        // Give a chance to apply a match selection filter that accepts '.'
        // prefix like Windows normally does.
        reselect_matches();
    }
}

//------------------------------------------------------------------------------
const char* get_last_prompt()
{
    return s_last_prompt.c_str();
}

//------------------------------------------------------------------------------
static void after_dispatch_hook()
{
    s_need_collect_words = true;
}

//------------------------------------------------------------------------------
static int can_concat_undo_hook(UNDO_LIST* undo, const char* string)
{
    const double clock = os::clock();
    const double delta = clock - undo->clock;

    assert(undo->end > undo->start);
    const int was_space = whitespace(rl_line_buffer[undo->end - 1]);
    const int is_space = whitespace(string[0]) && !string[1];

    const int can = ((delta < 0.1) ||
                     (delta < 5.0 && !(was_space && !is_space) && undo->end - undo->start < 20));

    if (can)
        undo->clock = clock;
    return can;
}

//------------------------------------------------------------------------------
static void maybe_collect_words()
{
    if (s_need_collect_words)
    {
        s_need_collect_words = false;
        force_update_internal(false);
    }
}

//------------------------------------------------------------------------------
static char* completion_word_break_hook()
{
    // When processing pending input or macro input, adjust_completion_word()
    // can get called before gen_completion_matches() and end up using stale
    // word break info.  E.g. macro input "echo %user\t".
    maybe_collect_words();
    return nullptr;
}

//------------------------------------------------------------------------------
static char adjust_completion_word(char quote_char, int32 *found_quote, int32 *delimiter)
{
    // This is too late to call maybe_collect_words(); by the time this is
    // called, rl_find_completion_word() has already modified rl_point.
    if (s_matches)
    {
        // Override Readline's word break position.  Often it's the same as
        // what Clink chose (possibly with help from generators), but Clink must
        // override it otherwise things go wrong in edge cases such as issue #59
        // (https://github.com/chrisant996/clink/issues/59).
        assert(s_matches->get_word_break_position() >= 0);
        if (s_matches->get_word_break_position() >= 0)
        {
            int32 old_point = rl_point;
            rl_point = min(s_matches->get_word_break_position(), rl_end);

            const char* pqc = nullptr;
            if (rl_point > 0)
            {
                // Check if the preceding character is a quote.
                pqc = strchr(rl_completer_quote_characters, rl_line_buffer[rl_point - 1]);
                if (rl_point < old_point && !(pqc && *pqc))
                {
                    // If the preceding character is not a quote, but rl_point
                    // got moved and it points at a quote, then advance rl_point
                    // so that lua scripts don't have to do quote handling.
                    pqc = strchr(rl_completer_quote_characters, rl_line_buffer[rl_point]);
                    if (pqc && *pqc)
                        rl_point++;
                }
            }
            if (pqc && *pqc)
            {
                quote_char = *pqc;
                switch (quote_char)
                {
                case '\'':  *found_quote = RL_QF_SINGLE_QUOTE; break;
                case '\"':  *found_quote = RL_QF_DOUBLE_QUOTE; break;
                default:    *found_quote = RL_QF_OTHER_QUOTE; break;
                }
            }
            else
            {
                quote_char = 0;
                *found_quote = 0;
            }

            *delimiter = 0;
        }
    }

    return quote_char;
}

//------------------------------------------------------------------------------
extern "C" int32 is_exec_ext(const char* ext)
{
    return path::is_executable_extension(ext);
}

//------------------------------------------------------------------------------
static char* filename_menu_completion_function(const char *text, int32 state)
{
    // This function should be unreachable.
    assert(false);
    return nullptr;
}

//------------------------------------------------------------------------------
static bool ensure_matches_size(char**& matches, int32 count, int32& reserved)
{
    count += 2;
    if (count > reserved)
    {
        int32 new_reserve = 64;
        while (new_reserve < count)
        {
            int32 prev = new_reserve;
            new_reserve <<= 1;
            if (new_reserve < prev)
                return false;
        }
        char **new_matches = (char **)realloc(matches, new_reserve * sizeof(matches[0]));
        if (!new_matches)
            return false;

        matches = new_matches;
        reserved = new_reserve;
    }
    return true;
}

//------------------------------------------------------------------------------
static void buffer_changing()
{
    // Reset the history position for the next input line prompt, upon changing
    // the input text at all.
    if (has_sticky_search_position())
    {
        clear_sticky_search_position();
        // TODO: Does this cross-link or leak or interfere with undo lists?
        using_history();
    }

    // The buffer text is changing, so the selection will be invalidated and
    // needs to be cleared.
    cua_clear_selection();

    // When the buffer changes, rl_display_fixed is no longer accurate.
    rl_display_fixed = false;
}

//------------------------------------------------------------------------------
static bool is_complete_with_wild()
{
    return g_match_wild.get() || is_globbing_wild();
}

//------------------------------------------------------------------------------
static char** alternative_matches(const char* text, int32 start, int32 end)
{
    rl_attempted_completion_over = 1;

    if (!s_matches)
        return nullptr;

    // If this assertion fails, the word break info may be out of sync, so a
    // fix would need to located further upstream.
    assert(!s_need_collect_words);

    update_matches();
    const display_filter_flags flags = display_filter_flags::none;
    if (matches* regen = maybe_regenerate_matches(text, flags))
    {
        // It's ok to redirect s_matches here because s_matches is reset in
        // every rl_module::on_input() call.
        s_matches = regen;
    }

    // Special case for possible-completions with a tilde by itself:  return no
    // matches so that it doesn't list anything.  Bash lists user accounts, but
    // Clink only supports tilde for the current user account.
    if (rl_completion_type == '?' && strcmp(text, "~") == 0)
        return nullptr;

    // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
    // quotes may seem surprising, but it's what CMD does and it works well.
    str_moveable tmp;
    concat_strip_quotes(tmp, text);

    // Handle tilde expansion.
    bool just_tilde = false;
    if (rl_complete_with_tilde_expansion && tmp.c_str()[0] == '~')
    {
        just_tilde = !tmp.c_str()[1];
        if (!path::tilde_expand(tmp))
            just_tilde = false;
        else if (just_tilde)
            path::maybe_strip_last_separator(tmp);
    }

    // Expand an abbreviated path.
    bool override = false;
    override_match_line_state omls;
    if (g_match_expand_abbrev.get() && !s_matches->get_match_count())
    {
        const char* in = tmp.c_str();
        str_moveable expanded;
        const bool disambiguated = os::disambiguate_abbreviated_path(in, expanded);
        if (expanded.length())
        {
#ifdef DEBUG
            if (dbg_get_env_int("DEBUG_EXPANDABBREV"))
                printf("\x1b[s\x1b[H\x1b[97;48;5;22mEXPANDED:  \"%s\" + \"%s\" (%s)\x1b[m\x1b[K\x1b[u", expanded.c_str(), in, disambiguated ? "UNIQUE" : "ambiguous");
#endif
            if (disambiguated)
            {
                expanded.concat(in);
                assert(in + strlen(in) == tmp.c_str() + tmp.length());
            }

            do_slash_translation(expanded, strpbrk(tmp.c_str(), "/\\"));

            if (!disambiguated)
            {
stop:
                assert(g_rl_buffer);
                g_rl_buffer->begin_undo_group();
                g_rl_buffer->remove(start, start + in - tmp.c_str());
                g_rl_buffer->set_cursor(start);
                g_rl_buffer->insert(expanded.c_str());
                g_rl_buffer->end_undo_group();
                // Force the menu-complete family of commands to regenerate
                // matches, otherwise they'll have no matches.
                override_rl_last_func(nullptr, true/*force_when_null*/);
                return nullptr;
            }
            else
            {
                in = tmp.c_str() + tmp.length();
                if (path::is_separator(expanded[expanded.length() - 1]))
                    goto stop;
                tmp = std::move(expanded);
                // Override the input editor's line state info to generate
                // matches using the expanded path, without actually modifying
                // the Readline line buffer (since we're inside a Readline
                // callback and Readline isn't prepared for the buffer to
                // change out from under it).
                override = true;
            }
        }
    }

    if (s_matches->is_fully_qualify())
    {
        // Note that this can further adjust an expanded abbreviated path
        // using it to override the match line state.
        omls.fully_qualify(start, end, tmp);
        override = true;
    }
    else if (override)
    {
        // This applies an expanded abbreviated path.
        omls.override(start, end, tmp.c_str());
    }

    // Perform completion again after overriding match line state.
    if (override)
    {
        update_matches();
        if (matches* regen = maybe_regenerate_matches(tmp.c_str(), flags))
        {
            // It's ok to redirect s_matches here because s_matches is reset in
            // every rl_module::on_input() call.
            s_matches = regen;
        }
    }

    // Handle the match.wild setting.
    const char* pattern = nullptr;
    if (is_complete_with_wild())
    {
        if (!is_literal_wild() && !just_tilde)
            tmp.concat("*");
        pattern = tmp.c_str();
    }

    matches_iter iter = s_matches->get_iter(pattern);
    if (!iter.next())
        return nullptr;

#ifdef DEBUG
    const int32 debug_matches = dbg_get_env_int("DEBUG_MATCHES");
#endif

    // Identify common prefix.
    char* end_prefix = rl_last_path_separator(text);
    if (end_prefix)
        end_prefix++;
    else if (ISALPHA(uint8(text[0])) && text[1] == ':')
        end_prefix = (char*)text + 2;
    int32 len_prefix = end_prefix ? end_prefix - text : 0;

    // Deep copy of the generated matches.  Inefficient, but this is how
    // readline wants them.
    str<32> lcd;
    int32 count = 0;
    int32 reserved = 0;
    char** matches = nullptr;
    if (!ensure_matches_size(matches, s_matches->get_match_count(), reserved))
        return nullptr;
    matches[0] = (char*)malloc((end - start) + 1);
    memcpy(matches[0], text, end - start);
    matches[0][(end - start)] = '\0';
    do
    {
        match_type type = iter.get_match_type();

        ++count;
        if (!ensure_matches_size(matches, count, reserved))
        {
            --count;
            break;
        }

        // PACKED MATCH FORMAT is:
        //  - N bytes:  MATCH (nul terminated char string)
        //  - 1 byte:   TYPE (uint8)
        //  - 1 byte:   APPEND CHAR (char)
        //  - 1 byte:   FLAGS (uint8)
        //  - N bytes:  DISPLAY (nul terminated char string)
        //  - N bytes:  DESCRIPTION (nul terminated char string)
        //
        // WARNING:  Several things rely on this memory layout, including
        // display_match_list_internal, matches_lookaside, and
        // match_display_filter.

        uint8 flags = 0;
        if (iter.get_match_append_display())
            flags |= MATCH_FLAG_APPEND_DISPLAY;

        shadow_bool suppress_append = iter.get_match_suppress_append();
        if (suppress_append.is_explicit())
        {
            flags |= MATCH_FLAG_HAS_SUPPRESS_APPEND;
            if (suppress_append.get())
                flags |= MATCH_FLAG_SUPPRESS_APPEND;
        }

        const char* const match = iter.get_match();
        const char* const display = iter.get_match_display();
        const char* const description = iter.get_match_description();
        const size_t packed_size = calc_packed_size(match, display, description);
        char* ptr = (char*)malloc(packed_size);

        matches[count] = ptr;

        if (!pack_match(ptr, packed_size, match, type, display, description, iter.get_match_append_char(), flags))
        {
            --count;
            free(ptr);
            continue;
        }

#ifdef DEBUG
        // Set DEBUG_MATCHES=-5 to print the first 5 matches.
        if (debug_matches > 0 || (debug_matches < 0 && count - 1 < 0 - debug_matches))
            printf("%u: %s, %04.4x => %s\n", count - 1, match, type, matches[count]);
#endif
    }
    while (iter.next());
    matches[count + 1] = nullptr;

    create_matches_lookaside(matches);
    update_rl_modes_from_matches(s_matches, iter, count);

    return matches;
}

//------------------------------------------------------------------------------
static bool match_display_filter(const char* needle, char** matches, ::matches& out, display_filter_flags flags)
{
    if (!s_matches)
        return false;

    return s_matches->match_display_filter(needle, matches, &out, flags);
}

//------------------------------------------------------------------------------
static bool match_display_filter_callback(char** matches, ::matches& out)
{
    return match_display_filter(s_needle.c_str(), matches, out, display_filter_flags::none);
}

//------------------------------------------------------------------------------
static int32 compare_lcd(const char* a, const char* b)
{
    return str_compare<char, true/*compute_lcd*/>(a, b);
}

//------------------------------------------------------------------------------
// If the input text starts with a slash and doesn't have any other slashes or
// path separators, then preserve the original slash in the lcd.  Otherwise it
// converts "somecommand /" to "somecommand \" and we lose the ability to try
// completing to test if an argmatcher has defined flags for "somecommand".
static void postprocess_lcd(char* lcd, const char* text)
{
    if (*text != '/')
        return;

    while (*(++text))
        if (*text == '/' || rl_is_path_separator(*text))
            return;

    lcd[0] = '/';
}

//------------------------------------------------------------------------------
static void load_user_inputrc(const char* state_dir, bool no_user)
{
#if defined(PLATFORM_WINDOWS)
    // Remember to update clink_info() if anything changes in here.

    static const char* const env_vars[] = {
        "clink_inputrc",
        "", // Magic value handled specially below.
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    static const char* const file_names[] = {
        ".inputrc",
        "_inputrc",
        "clink_inputrc",
    };

    rollback<int32> rb_load_user_inputrc(_rl_load_user_init_file, !no_user);

    for (const char* env_var : env_vars)
    {
        str<280> path;
        if (!*env_var && state_dir && *state_dir)
            path.copy(state_dir);
        else if (!*env_var || !os::get_env(env_var, path))
            continue;

        int32 base_len = path.length();

        for (int32 j = 0; j < sizeof_array(file_names); ++j)
        {
            path.truncate(base_len);
            path::append(path, file_names[j]);

            if (!rl_read_init_file(path.c_str()))
            {
                LOG("Found Readline inputrc at '%s'", path.c_str());
                return;
            }
        }
    }
#endif // PLATFORM_WINDOWS
}

//------------------------------------------------------------------------------
typedef const char* two_strings[2];
static void bind_keyseq_list(const two_strings* list, Keymap map)
{
    for (int32 i = 0; list[i][0]; ++i)
        rl_bind_keyseq_in_map(list[i][0], rl_named_function(list[i][1]), map);
}

//------------------------------------------------------------------------------
static void init_readline_hooks()
{
    static bool s_first_time = true;

    // These hooks must be set even before calling rl_initialize(), because it
    // can invoke e.g. rl_fwrite_function which needs to intercept some escape
    // sequences even during initialization.
    //
    // And reset these for each input line because of g_debug_log_terminal.
    rl_getc_function = terminal_read_thunk;
    rl_fwrite_function = terminal_write_thunk;
    rl_log_read_key_hook = nullptr;
    if (g_debug_log_terminal.get())
    {
        rl_fwrite_function = terminal_log_write;
        rl_log_read_key_hook = terminal_log_read;
    }
    rl_fflush_function = terminal_fflush_thunk;
    rl_instream = in_stream;
    rl_outstream = out_stream;

    if (!s_first_time)
        return;
    s_first_time = false;

    // Input line (and prompt) display hooks.
    rl_redisplay_function = hook_display;
    rl_get_face_func = get_face_func;
    rl_puts_face_func = puts_face_func;

    // Input event hooks.
    rl_input_available_hook = input_available_hook;
    rl_read_key_hook = read_key_hook;
    rl_buffer_changing_hook = buffer_changing;
    rl_selection_event_hook = cua_selection_event_hook;
    rl_after_dispatch_hook = after_dispatch_hook;
    rl_can_concat_undo_hook = can_concat_undo_hook;

    // History hooks.
    rl_add_history_hook = host_add_history;
    rl_remove_history_hook = host_remove_history;
    rl_on_replace_from_history_hook = suppress_suggestions;

    // Match completion.
    rl_lookup_match_type = lookup_match_type;
    rl_override_match_append = override_match_append;
    rl_free_match_list_hook = free_match_list_hook;
    rl_ignore_some_completions_function = host_filter_matches;
    rl_attempted_completion_function = alternative_matches;
    rl_menu_completion_entry_function = filename_menu_completion_function;
    rl_adjust_completion_defaults = adjust_completion_defaults;
    rl_completion_word_break_hook = completion_word_break_hook;
    rl_adjust_completion_word = adjust_completion_word;
    rl_match_display_filter_func = match_display_filter_callback;
    rl_compare_lcd_func = compare_lcd;
    rl_postprocess_lcd_func = postprocess_lcd;

    // Match display.
#if !defined(OMIT_DEFAULT_DISPLAY_MATCHES)
    rl_completion_display_matches_func = display_matches;
    rl_completion_display_matches_hook = dummy_display_matches_hook;
#endif
    rl_is_exec_func = is_exec_ext;

    // Macro hooks (for "luafunc:" support).
    rl_macro_hook_func = macro_hook_func;
    rl_last_func_hook_func = last_func_hook_func;

    // Recognize both / and \\ as path separators, and normalize to \\.
    rl_backslash_path_sep = 1;
    rl_preferred_path_separator = PATH_SEP[0];

    // Quote spaces in completed filenames.
    rl_completer_quote_characters = "\"";
    rl_basic_quote_characters = "\"";

    // Same list CMD uses for quoting filenames.
    rl_filename_quote_characters = " &()[]{}^=;!%'+,`~";

    // Basic word break characters.
    // Readline does not currently use rl_basic_word_break_characters or
    // rl_basic_word_break_characters_without_backslash for anything.

    // Completer word break characters -- rl_basic_word_break_characters, with
    // backslash removed (because rl_backslash_path_sep) and without '$' or '%'
    // so we can let the match generators decide when '%' should start a word or
    // end a word (see :getwordbreakinfo()).
    // NOTE:  Due to adjust_completion_word(), this has no practical effect
    // anymore.  Word break characters are handled by cmd_word_tokeniser.
    rl_completer_word_break_characters = " \t\n\"'`@><=;|&{(,"; /* }) */

    // Completion and match display.
    rl_ignore_completion_duplicates = 0; // We'll handle de-duplication.
    rl_sort_completion_matches = 0; // We'll handle sorting.

}

//------------------------------------------------------------------------------
static void safe_replace_keymap(Keymap replace, Keymap with)
{
    Keymap to, from;

    for (uint32 i = 0; i < KEYMAP_SIZE; i++)
    {
        switch (replace[i].type)
        {
        case ISKMAP:
            {
                Keymap target = FUNCTION_TO_KEYMAP(replace, i);
                assert(target != vi_movement_keymap);
                assert(target != vi_insertion_keymap);
                assert(target != emacs_standard_keymap);
                if (target && target != emacs_meta_keymap && target != emacs_ctlx_keymap)
                    rl_free_keymap(target);
            }
            break;
        case ISMACR:
            free((void*)replace[i].function);
            break;
        }

        replace[i].type = with[i].type;
        switch (with[i].type)
        {
        case ISFUNC:
            replace[i].function = with[i].function;
            break;
        case ISKMAP:
            {
                from = FUNCTION_TO_KEYMAP(with, i);
                assert(from != vi_movement_keymap);
                assert(from != vi_insertion_keymap);
                assert(from != emacs_standard_keymap);
                if (from && from != emacs_meta_keymap && from != emacs_ctlx_keymap)
                {
                    to = rl_make_bare_keymap();
                    safe_replace_keymap(to, from);
                }
                else
                {
                    to = from;
                }
                replace[i].function = KEYMAP_TO_FUNCTION(to);
            }
            break;
        case ISMACR:
            replace[i].function = KEYMAP_TO_FUNCTION(savestring((const char*)with[i].function));
            break;
        }
    }
}

//------------------------------------------------------------------------------
static void save_restore_initial_state(const bool restore)
{
    // Keymaps.

    static const Keymap saved_vi_movement_keymap = rl_make_bare_keymap();
    static const Keymap saved_vi_insertion_keymap = rl_make_bare_keymap();
    static const Keymap saved_emacs_standard_keymap = rl_make_bare_keymap();
    static const Keymap saved_emacs_meta_keymap = rl_make_bare_keymap();
    static const Keymap saved_emacs_ctlx_keymap = rl_make_bare_keymap();

    if (!restore)
    {
        // Save original state of keymaps.
        safe_replace_keymap(saved_vi_movement_keymap, vi_movement_keymap);
        safe_replace_keymap(saved_vi_insertion_keymap, vi_insertion_keymap);
        safe_replace_keymap(saved_emacs_meta_keymap, emacs_meta_keymap);
        safe_replace_keymap(saved_emacs_ctlx_keymap, emacs_ctlx_keymap);
        safe_replace_keymap(saved_emacs_standard_keymap, emacs_standard_keymap);
    }
    else
    {
        // Restore saved keymaps.
        safe_replace_keymap(vi_movement_keymap, saved_vi_movement_keymap);
        safe_replace_keymap(vi_insertion_keymap, saved_vi_insertion_keymap);
        safe_replace_keymap(emacs_standard_keymap, saved_emacs_standard_keymap);
        safe_replace_keymap(emacs_meta_keymap, saved_emacs_meta_keymap);
        safe_replace_keymap(emacs_ctlx_keymap, saved_emacs_ctlx_keymap);

        // Clear global "recent" pointer, since it could have been invalidated
        // by the operations above.
        rl_binding_keymap = nullptr;
    }

    // Config variables.

    static struct {
        int32* const target;
        int32 saved;
    } c_saved_int_vars[] = {
        { &_rl_bell_preference                          },  // "bell-style"
        { &_rl_bind_stty_chars                          },  // "bind-tty-special-chars"
        { &rl_blink_matching_paren                      },  // "blink-matching-paren"
        //{ &rl_byte_oriented                             },  // "byte-oriented"
        { &_rl_colored_completion_prefix                },  // "colored-completion-prefix"
        { &_rl_colored_stats                            },  // "colored-stats"
        { &rl_completion_auto_query_items               },  // "completion-auto-query-items"
        { &_rl_completion_columns                       },  // "completion-display-width"
        { &rl_completion_query_items                    },  // "completion-query-items"
        { &_rl_completion_case_fold                     },  // "completion-ignore-case"
        { &_rl_completion_case_map                      },  // "completion-map-case"
        { &_rl_completion_prefix_display_length         },  // "completion-prefix-display-length"
        { &_rl_convert_meta_chars_to_ascii              },  // "convert-meta"
        { &rl_inhibit_completion                        },  // "disable-completion"
        { &_rl_echo_control_chars                       },  // "echo-control-characters"
        { &rl_editing_mode                              },  // "editing-mode"
        { &_rl_enable_active_region                     },  // "enable-active-region"
        { &_rl_enable_bracketed_paste                   },  // "enable-bracketed-paste"
        { &_rl_enable_keypad                            },  // "enable-keypad"
        { &_rl_enable_meta                              },  // "enable-meta-key"
        { &rl_complete_with_tilde_expansion             },  // "expand-tilde"
        { &_rl_history_point_at_end_of_anchored_search  },  // "history-point-at-end-of-anchored-search"
        { &_rl_history_preserve_point                   },  // "history-preserve-point"
        //{ nullptr                                       },  // "history-size"
        { &_rl_horizontal_scroll_mode                   },  // "horizontal-scroll-mode"
        { &_rl_meta_flag                                },  // "input-meta"
        { &_rl_keyseq_timeout                           },  // "keyseq-timeout"
        { &_rl_complete_mark_directories                },  // "mark-directories"
        { &_rl_mark_modified_lines                      },  // "mark-modified-lines"
        { &_rl_complete_mark_symlink_dirs               },  // "mark-symlinked-directories"
        { &_rl_match_hidden_files                       },  // "match-hidden-files"
        { &_rl_menu_complete_prefix_first               },  // "menu-complete-display-prefix"
        { &_rl_menu_complete_wraparound                 },  // "menu-complete-wraparound"
        { &_rl_meta_flag                                },  // "meta-flag"
        { &_rl_output_meta_chars                        },  // "output-meta"
        { &_rl_page_completions                         },  // "page-completions"
        { &_rl_bell_preference                          },  // "prefer-visible-bell"
        { &_rl_print_completions_horizontally           },  // "print-completions-horizontally"
        { &_rl_revert_all_at_newline                    },  // "revert-all-at-newline"
        { &_rl_search_case_fold                         },  // "search-ignore-case"
        { &_rl_complete_show_all                        },  // "show-all-if-ambiguous"
        { &_rl_complete_show_unmodified                 },  // "show-all-if-unmodified"
        { &_rl_show_mode_in_prompt                      },  // "show-mode-in-prompt"
        { &_rl_skip_completed_text                      },  // "skip-completed-text"
        { &rl_visible_stats                             },  // "visible-stats"
    };

    for (auto& entry : c_saved_int_vars)
    {
        if (!restore)
        {
            // Save original value.
            entry.saved = *entry.target;
        }
        else
        {
            // Restore saved value.
            *entry.target = entry.saved;
        }
    }

    static struct {
        char** const target;
        char* saved;
    } c_saved_string_vars[] = {
        { &_rl_active_region_end_color                  },  // "active-region-end-color"
        { &_rl_active_region_start_color                },  // "active-region-start-color"
        { &_rl_comment_begin                            },  // "comment-begin"
        { &_rl_emacs_mode_str                           },  // "emacs-mode-string"
        { &_rl_isearch_terminators                      },  // "isearch-terminators"
        //{ nullptr                                       },  // "keymap"
        { &_rl_vi_cmd_mode_str                          },  // "vi-cmd-mode-string"
        { &_rl_vi_ins_mode_str                          },  // "vi-ins-mode-string"
    };

    for (auto& entry : c_saved_string_vars)
    {
        if (!restore)
        {
            // Save original value.
            assert(!entry.saved);
            entry.saved = *entry.target ? savestring(*entry.target) : nullptr;
        }
        else
        {
            // Restore saved value.
            free(*entry.target);
            *entry.target = entry.saved ? savestring(entry.saved) : nullptr;
        }
    }
}

//------------------------------------------------------------------------------
void initialise_readline(const char* shell_name, const char* state_dir, const char* default_inputrc, bool no_user)
{
    // Can't give a more specific scope like "Readline initialization", because
    // realloc of some things will use "Readline" and assert on label change.
    dbg_ignore_scope(snapshot, "Readline");

    // Readline needs a tweak of its handling of 'meta' (i.e. IO bytes >=0x80)
    // so that it handles UTF-8 correctly (convert=input, output=output).
    // Because these affect key binding translations, these are set even before
    // calling rl_initialize() or binding any other keys.
    _rl_convert_meta_chars_to_ascii = 0;
    _rl_output_meta_chars = 1;

    // "::" was already in use as a common idiom as a comment prefix.
    // Note:  Depending on the CMD parser state and what follows the :: there
    // are degenerate cases where it causes a syntax error, so technically "rem"
    // would be more functionally correct.
    _rl_comment_begin = savestring("::");

    // CMD does not consider backslash to be an escape character (in particular,
    // it cannot escape a space).
    history_host_backslash_escape = 0;

    // Add commands.
    static bool s_rl_initialized = false;
    const bool initialized = s_rl_initialized;
    if (!s_rl_initialized)
    {
        s_rl_initialized = true;

        static str_moveable s_default_inputrc(default_inputrc);
        _rl_default_init_file = s_default_inputrc.empty() ? nullptr : s_default_inputrc.c_str();

        // The "default_inputrc" file was introduced in v1.3.5, but up through
        // v1.6.0 it wasn't actually loaded properly.  And the provided one
        // had syntax errors (missing the "set" keyword), so to compensate now
        // the "set" keyword is optional in the "default_inputrc" file.
        _rl_default_init_file_optional_set = 1;

        init_readline_hooks();

        clink_add_funmap_entry("clink-accept-suggested-line", clink_accept_suggested_line, keycat_misc, "If there is a suggestion, insert the suggested line and accept the input line");
        clink_add_funmap_entry("clink-backward-bigword", rl_vi_bWord, keycat_cursor, "Move back to the start of the current or previous space delimited word");
        clink_add_funmap_entry("clink-complete-numbers", clink_complete_numbers, keycat_completion, "Perform completion using numbers from the current screen");
        clink_add_funmap_entry("clink-copy-cwd", clink_copy_cwd, keycat_misc, "Copies the current working directory to the clipboard");
        clink_add_funmap_entry("clink-copy-line", clink_copy_line, keycat_misc, "Copies the input line to the clipboard");
        clink_add_funmap_entry("clink-copy-word", clink_copy_word, keycat_misc, "Copies the word at the cursor point to the clipboard, or copies the Nth word if a numeric argument is provided");
        clink_add_funmap_entry("clink-ctrl-c", clink_ctrl_c, keycat_basic, "Copies any selected text to the clipboard, otherwise cancels the input line and starts a new one");
        clink_add_funmap_entry("clink-dump-functions", clink_dump_functions, keycat_misc, "Print all of the functions and their key bindings.  If a numeric argument is supplied, formats the output so that it can be made part of an INPUTRC file");
        clink_add_funmap_entry("clink-dump-macros", clink_dump_macros, keycat_misc, "Print all of the key names bound to macros and the strings they output.  If a numeric argument is supplied, formats the output so that it can be made part of an INPUTRC file");
        clink_add_funmap_entry("clink-exit", clink_exit, keycat_misc, "Replaces the input line with 'exit' and executes it (exits the CMD instance)");
        clink_add_funmap_entry("clink-expand-doskey-alias", clink_expand_doskey_alias, keycat_misc, "Expands doskey aliases in the input line");
        clink_add_funmap_entry("clink-expand-env-var", clink_expand_env_var, keycat_misc, "Expands environment variables in the word at the cursor point");
        clink_add_funmap_entry("clink-expand-history", clink_expand_history, keycat_misc, "Performs history expansion in the input line");
        clink_add_funmap_entry("clink-expand-history-and-alias", clink_expand_history_and_alias, keycat_misc, "Performs history and doskey alias expansion in the input line");
        clink_add_funmap_entry("clink-expand-line", clink_expand_line, keycat_misc, "Performs history, doskey alias, and environment variable expansion in the input line");
        clink_add_funmap_entry("clink-find-conhost", clink_find_conhost, keycat_misc, "Invokes the 'Find...' command in a standalone CMD window");
        clink_add_funmap_entry("clink-forward-bigword", clink_forward_bigword, keycat_cursor, "Move forward to the beginning of the next space delimited word, or insert the next full suggested word up to a space");
        clink_add_funmap_entry("clink-insert-dot-dot", clink_insert_dot_dot, keycat_misc, "Inserts '..\\' at the cursor point");
        clink_add_funmap_entry("clink-insert-suggested-full-word", clink_insert_suggested_full_word, keycat_misc, "If there is a suggestion, insert the next full word from the suggested line");
        clink_add_funmap_entry("clink-insert-suggested-line", clink_insert_suggested_line, keycat_misc, "If there is a suggestion, insert the suggested line");
        clink_add_funmap_entry("clink-insert-suggested-word", clink_insert_suggested_word, keycat_misc, "If there is a suggestion, insert the next word from the suggested line");
        clink_add_funmap_entry("clink-magic-suggest-space", clink_magic_suggest_space, keycat_misc, "Insert the next full suggested word (if any) up to a space, and insert a space");
        clink_add_funmap_entry("clink-mark-conhost", clink_mark_conhost, keycat_misc, "Invokes the 'Mark' command in a standalone CMD window");
        clink_add_funmap_entry("clink-menu-complete-numbers", clink_menu_complete_numbers, keycat_completion, "Like 'menu-complete' using numbers from the current screen");
        clink_add_funmap_entry("clink-menu-complete-numbers-backward", clink_menu_complete_numbers_backward, keycat_completion, "Like 'menu-complete-backward' using numbers from the current screen");
        clink_add_funmap_entry("clink-old-menu-complete-numbers", clink_old_menu_complete_numbers, keycat_completion, "Like 'old-menu-complete' using numbers from the current screen");
        clink_add_funmap_entry("clink-old-menu-complete-numbers-backward", clink_old_menu_complete_numbers_backward, keycat_completion, "Like 'old-menu-complete-backward' using numbers from the current screen");
        clink_add_funmap_entry("clink-paste", clink_paste, keycat_basic, "Paste text from the clipboard at the cursor point");
        clink_add_funmap_entry("clink-popup-complete-numbers", clink_popup_complete_numbers, keycat_completion, "Perform interactive completion from a list of numbers from the current screen");
        clink_add_funmap_entry("clink-popup-directories", clink_popup_directories, keycat_misc, "Show recent directories in a popup list.  In the popup, use Enter to 'cd /d' to the selected directory");
        clink_add_funmap_entry("clink-popup-history", clink_popup_history, keycat_history, "Show history entries in a popup list.  Filters using any text before the cursor point.  In the popup, use Enter to execute the selected history entry");
        clink_add_funmap_entry("clink-popup-show-help", clink_popup_show_help, keycat_misc, "Show all key bindings in a searchable popup list.  In the popup, use Enter to invoke the selected key binding.  If a numeric argument of 4 is supplied, includes unbound commands");
        clink_add_funmap_entry("clink-reload", clink_reload, keycat_misc, "Reload Lua scripts and the .inputrc file");
        clink_add_funmap_entry("clink-reset-line", clink_reset_line, keycat_basic, "Clear the input line.  Can be undone, unlike 'revert-line'");
        clink_add_funmap_entry("clink-scroll-bottom", clink_scroll_bottom, keycat_scroll, "Scroll to the bottom of the terminal's scrollback buffer");
        clink_add_funmap_entry("clink-scroll-line-down", clink_scroll_line_down, keycat_scroll, "Scroll down one line");
        clink_add_funmap_entry("clink-scroll-line-up", clink_scroll_line_up, keycat_scroll, "Scroll up one line");
        clink_add_funmap_entry("clink-scroll-page-down", clink_scroll_page_down, keycat_scroll, "Scroll down one page");
        clink_add_funmap_entry("clink-scroll-page-up", clink_scroll_page_up, keycat_scroll, "Scroll up one page");
        clink_add_funmap_entry("clink-scroll-top", clink_scroll_top, keycat_scroll, "Scroll to the top of the terminal's scrollback buffer");
        clink_add_funmap_entry("clink-select-complete", clink_select_complete, keycat_completion, "Perform completion by selecting from an interactive list of possible completions; if there is only one match, insert it");
        clink_add_funmap_entry("clink-selectall-conhost", clink_selectall_conhost, keycat_misc, "Invokes the 'Select All' command in a standalone CMD window");
        clink_add_funmap_entry("clink-shift-space", clink_shift_space, keycat_misc, "Invoke the normal Space key binding, so that Shift-Space behaves the same as Space");
        clink_add_funmap_entry("clink-show-help", show_rl_help, keycat_misc, "Show all key bindings.  A numeric argument affects showing categories and descriptions:  0=neither, 1=categories, 2=descriptions, 3=both (default).  Add 4 to include unbound commands");
        clink_add_funmap_entry("clink-show-help-raw", show_rl_help_raw, keycat_misc, "Show raw key sequence strings for all key bindings");
        clink_add_funmap_entry("clink-toggle-slashes", clink_toggle_slashes, keycat_misc, "Toggle between forward and backslashes in the word at the cursor point, or in the Nth word if a numeric argument is provided");
        clink_add_funmap_entry("clink-up-directory", clink_up_directory, keycat_misc, "Execute 'cd ..' to move up one directory");
        clink_add_funmap_entry("clink-what-is", clink_what_is, keycat_misc, "Show the key binding for the next key sequence input.  If a numeric argument is supplied, the raw key sequence string is shown instead of the friendly key name");
        clink_add_funmap_entry("cua-backward-bigword", cua_backward_bigword, keycat_select, "Extend the selection backward one space delimited word");
        clink_add_funmap_entry("cua-backward-char", cua_backward_char, keycat_select, "Extend the selection backward one character");
        clink_add_funmap_entry("cua-backward-word", cua_backward_word, keycat_select, "Extend the selection backward one word");
        clink_add_funmap_entry("cua-beg-of-line", cua_beg_of_line, keycat_select, "Extend selection to the beginning of the line");
        clink_add_funmap_entry("cua-copy", cua_copy, keycat_select, "Copy the selected text to the clipboard");
        clink_add_funmap_entry("cua-cut", cua_cut, keycat_select, "Cut the selected text to the clipboard");
        clink_add_funmap_entry("cua-end-of-line", cua_end_of_line, keycat_select, "Extend the selection to the end of the line");
        clink_add_funmap_entry("cua-forward-bigword", cua_forward_bigword, keycat_select, "Extend the selection forward one space delimited word, or insert the next full suggested word up to a space");
        clink_add_funmap_entry("cua-forward-char", cua_forward_char, keycat_select, "Extend the selection forward one character, or insert the next full suggested word up to a space");
        clink_add_funmap_entry("cua-forward-word", cua_forward_word, keycat_select, "Extend the selection forward one word");
        clink_add_funmap_entry("cua-next-screen-line", cua_next_screen_line, keycat_select, "Extend the selection down one screen line");
        clink_add_funmap_entry("cua-previous-screen-line", cua_previous_screen_line, keycat_select, "Extend the selection up one screen line");
        clink_add_funmap_entry("cua-select-all", cua_select_all, keycat_select, "Extend the selection to the entire input line");
        clink_add_funmap_entry("cua-select-word", cua_select_word, keycat_select, "Select the word at the cursor point");

        clink_add_funmap_entry("win-copy-history-number", win_f9, keycat_history, "Enter a history number and replace the input line with the history entry");
        clink_add_funmap_entry("win-copy-up-to-char", win_f2, keycat_history, "Enter a character and copy up to it from the previous command");
        clink_add_funmap_entry("win-copy-up-to-end", win_f3, keycat_history, "Copy the rest of the previous command");
        clink_add_funmap_entry("win-cursor-forward", win_f1, keycat_history, "Move cursor forward, or at end of line copy character from previous command, or insert suggestion");
        clink_add_funmap_entry("win-delete-up-to-char", win_f4, keycat_misc, "Enter a character and delete up to it in the input line");
        clink_add_funmap_entry("win-history-list", win_f7, keycat_history, "Executes a history entry from a list");
        clink_add_funmap_entry("win-insert-eof", win_f6, keycat_misc, "Insert ^Z");

        clink_add_funmap_entry("edit-and-execute-command", edit_and_execute_command, keycat_misc, "Invoke an editor on the current input line, and execute the result as commands.  This attempts to invoke '%VISUAL%', '%EDITOR%', or 'notepad.exe' as the editor, in that order");
        clink_add_funmap_entry("glob-complete-word", glob_complete_word, keycat_completion, "Perform wildcard completion on the text before the cursor point, with a '*' implicitly appended");
        clink_add_funmap_entry("glob-expand-word", glob_expand_word, keycat_completion, "Insert all the wildcard completions that 'glob-list-expansions' would list.  If a numeric argument is supplied, a '*' is implicitly appended before completion");
        clink_add_funmap_entry("glob-list-expansions", glob_list_expansions, keycat_completion, "List the possible wildcard completions of the text before the cursor point.  If a numeric argument is supplied, a '*' is implicitly appended before completion");
        clink_add_funmap_entry("magic-space", magic_space, keycat_history, "Perform history expansion on the text before the cursor position and insert a space");

        clink_add_funmap_entry("clink-diagnostics", clink_diagnostics, keycat_misc, "Show internal diagnostic information");

        // IMPORTANT:  Aliased command names need to be defined after the real
        // command name, so that rl.getbinding() returns the real command name.
        {
            // Alias some Clink commands.
            // clink_add_funmap_entry("clink-popup-complete", clink_select_complete, keycat_completion, "Perform completion by selecting from an interactive list of possible completions; if there is only one match, insert it");
            rl_add_funmap_entry("clink-popup-complete", clink_select_complete);

            // Alias some command names for convenient compatibility with bash .inputrc configuration entries.
            rl_add_funmap_entry("alias-expand-line", clink_expand_doskey_alias);
            rl_add_funmap_entry("history-and-alias-expand-line", clink_expand_history_and_alias);
            rl_add_funmap_entry("history-expand-line", clink_expand_history);
            rl_add_funmap_entry("insert-last-argument", rl_yank_last_arg);
            rl_add_funmap_entry("shell-expand-line", clink_expand_line);
        }

        // Preemptively replace some commands with versions that support suggestions.
        clink_add_funmap_entry("forward-byte", clink_forward_byte, keycat_cursor, "Move forward a single byte, or insert suggestion");
        clink_add_funmap_entry("forward-char", clink_forward_char, keycat_cursor, "Move forward a character, or insert suggestion");
        clink_add_funmap_entry("forward-word", clink_forward_word, keycat_cursor, "Move forward to the end of the next word, or insert next suggested word");
        clink_add_funmap_entry("end-of-line", clink_end_of_line, keycat_basic, "Move to the end of the line, or insert suggestion");

        // Preemptively replace paste command with one that supports Unicode.
        rl_add_funmap_entry("paste-from-clipboard", clink_paste);

        // Install signal handlers so that Readline doesn't trigger process exit
        // in response to Ctrl+C or Ctrl+Break.
        rl_catch_signals = 1;
        _rl_echoctl = 1;
        _rl_intr_char = CTRL('C');

        // Do a first rl_initialize() before setting any key bindings or config
        // variables.  Otherwise it would happen when rl_module installs the
        // Readline callback, after having loaded the Lua scripts.  That would
        // mean certain key bindings would not take effect yet.  Also, Clink
        // prevents rl_init_read_line() from loading the inputrc file both so it
        // doesn't initially read the wrong inputrc file, and because
        // rl_initialize() set some default key bindings AFTER it loaded the
        // inputrc file.  Those were interfering with suppressing the
        // *-mode-string config variables.
        rl_readline_name = shell_name;
        rl_initialize();

        // Override some defaults.
        _rl_bell_preference = VISIBLE_BELL;     // Because audible is annoying.
        rl_complete_with_tilde_expansion = 1;   // Since CMD doesn't understand tilde.
    }

    // Save/restore the original keymap table definitions and original config
    // variable values so that reloading the inputrc doesn't have lingering
    // key bindings or config variables values.
    save_restore_initial_state(initialized);

    // Bind extended keys so editing follows Windows' conventions.
    static constexpr const char* const emacs_key_binds[][2] = {
        { "\\e[1;5F",       "kill-line" },               // ctrl-end
        { "\\e[1;5H",       "backward-kill-line" },      // ctrl-home
        { "\\e[5~",         "history-search-backward" }, // pgup
        { "\\e[6~",         "history-search-forward" },  // pgdn
        { "\\e[3;5~",       "kill-word" },               // ctrl-del
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        { "\\e[2~",         "overwrite-mode" },          // ins
        { "\\C-c",          "clink-ctrl-c" },            // ctrl-c
        { "\\C-v",          "clink-paste" },             // ctrl-v
        { "\\C-z",          "undo" },                    // ctrl-z
        { "\\C-x*",         "glob-expand-word" },        // ctrl-x,*
        { "\\C-xg",         "glob-list-expansions" },    // ctrl-x,g
        { "\\C-x\\C-e",     "edit-and-execute-command" }, // ctrl-x,ctrl-e
        { "\\C-x\\C-r",     "clink-reload" },            // ctrl-x,ctrl-r
        { "\\C-x\\C-z",     "clink-diagnostics" },       // ctrl-x,ctrl-z
        { "\\M-g",          "glob-complete-word" },      // alt-g
        { "\\eOP",          "win-cursor-forward" },      // F1
        { "\\eOQ",          "win-copy-up-to-char" },     // F2
        { "\\eOR",          "win-copy-up-to-end" },      // F3
        { "\\eOS",          "win-delete-up-to-char" },   // F4
        { "\\e[15~",        "previous-history" },        // F5
        { "\\e[17~",        "win-insert-eof" },          // F6
        { "\\e[18~",        "win-history-list" },        // F7
        { "\\e[19~",        "history-search-backward" }, // F8
        { "\\e[20~",        "win-copy-history-number" }, // F9
        {}
    };

    static constexpr const char* const windows_emacs_key_binds[][2] = {
        { "\\C-a",          "clink-selectall-conhost" }, // ctrl-a
        { "\\C-b",          "" },                        // ctrl-b
        { "\\C-e",          "clink-expand-line" },       // ctrl-e
        { "\\C-f",          "clink-find-conhost" },      // ctrl-f
        { "\\e[27;5;77~",   "clink-mark-conhost" },      // ctrl-m (differentiated)
        { "\\e[C",          "win-cursor-forward" },      // right
        { "\t",             "old-menu-complete" },       // tab
        { "\\e[Z",          "old-menu-complete-backward" }, // shift-tab
        {}
    };

    static constexpr const char* const bash_emacs_key_binds[][2] = {
        { "\\C-a",          "beginning-of-line" },       // ctrl-a
        { "\\C-b",          "backward-char" },           // ctrl-b
        { "\\C-e",          "end-of-line" },             // ctrl-e
        { "\\C-f",          "forward-char" },            // ctrl-f
        { "\\e[27;5;77~",   "" },                        // ctrl-m (differentiated)
        { "\\e[C",          "forward-char" },            // right
        { "\t",             "complete" },                // tab
        { "\\e[Z",          "" },                        // shift-tab
        {}
    };

    static constexpr const char* const general_key_binds[][2] = {
        { "\\e[27;5;32~",   "clink-select-complete" },   // ctrl-space
        { "\\M-a",          "clink-insert-dot-dot" },    // alt-a
        { "\\M-c",          "clink-copy-cwd" },          // alt-c
        { "\\M-h",          "clink-show-help" },         // alt-h
        { "\\M-\\C-c",      "clink-copy-line" },         // alt-ctrl-c
        { "\\M-\\C-d",      "remove-history" },          // alt-ctrl-d
        { "\\M-\\C-e",      "clink-expand-line" },       // alt-ctrl-e
        { "\\M-\\C-f",      "clink-expand-doskey-alias" }, // alt-ctrl-f
        { "\\M-\\C-k",      "add-history" },             // alt-ctrl-k
        { "\\M-\\C-n",      "clink-old-menu-complete-numbers"},// alt-ctrl-n
        { "\\e[27;8;78~",   "clink-popup-complete-numbers"},// alt-ctrl-shift-n
        { "\\M-\\C-u",      "clink-up-directory" },      // alt-ctrl-u (from Clink 0.4.9)
        { "\\M-\\C-w",      "clink-copy-word" },         // alt-ctrl-w
        { "\\e[5;5~",       "clink-up-directory" },      // ctrl-pgup (changed in Clink 1.0.0)
        { "\\e[5;7~",       "clink-popup-directories" }, // alt-ctrl-pgup
        { "\\e[1;7A",       "clink-popup-history" },     // alt-ctrl-up
        { "\\e[1;3H",       "clink-scroll-top" },        // alt-home
        { "\\e[1;3F",       "clink-scroll-bottom" },     // alt-end
        { "\\e[5;3~",       "clink-scroll-page-up" },    // alt-pgup
        { "\\e[6;3~",       "clink-scroll-page-down" },  // alt-pgdn
        { "\\e[1;3A",       "clink-scroll-line-up" },    // alt-up
        { "\\e[1;3B",       "clink-scroll-line-down" },  // alt-down
        { "\\e[1;5A",       "clink-scroll-line-up" },    // ctrl-up
        { "\\e[1;5B",       "clink-scroll-line-down" },  // ctrl-down
        { "\\e[27;5;191~",  "clink-toggle-slashes" },    // ctrl-/
        { "\\e?",           "clink-what-is" },           // alt-? (alt-shift-/)
        { "\\e[27;8;191~",  "clink-show-help" },         // ctrl-alt-? (ctrl-alt-shift-/)
        { "\\e^",           "clink-expand-history" },    // alt-^
        { "\\e[1;5D",       "backward-word" },           // ctrl-left
        { "\\e[1;5C",       "forward-word" },            // ctrl-right
        { "\\e[3~",         "delete-char" },             // del
        { "\\e[C",          "forward-char" },            // right (because of suggestions)
        { "\\e[F",          "end-of-line" },             // end
        { "\\e[H",          "beginning-of-line" },       // home
        { "\\e[1;2A",       "cua-previous-screen-line" },// shift-up
        { "\\e[1;2B",       "cua-next-screen-line" },    // shift-down
        { "\\e[1;2D",       "cua-backward-char" },       // shift-left
        { "\\e[1;2C",       "cua-forward-char" },        // shift-right
        { "\\e[1;6D",       "cua-backward-word" },       // ctrl-shift-left
        { "\\e[1;6C",       "cua-forward-word" },        // ctrl-shift-right
        { "\\e[1;2H",       "cua-beg-of-line" },         // shift-home
        { "\\e[1;2F",       "cua-end-of-line" },         // shift-end
        { "\\e[2;5~",       "cua-copy" },                // ctrl-ins
        { "\\e[2;2~",       "clink-paste" },             // shift-ins
        { "\\e[3;2~",       "cua-cut" },                 // shift-del
        { "\\e[27;2;32~",   "clink-shift-space" },       // shift-space
        // Update default bindings for commands replaced for suggestions.
        { "\\e[1;3C",       "forward-word" },            // alt-right
        {}
    };

    static constexpr const char* const vi_insertion_key_binds[][2] = {
        { "\\M-\\C-i",      "tab-insert" },              // alt-ctrl-i
        { "\\M-\\C-j",      "emacs-editing-mode" },      // alt-ctrl-j
        { "\\M-\\C-k",      "kill-line" },               // alt-ctrl-k
        { "\\M-\\C-m",      "emacs-editing-mode" },      // alt-ctrl-m
        { "\\C-_",          "vi-undo" },                 // ctrl--
        { "\\M-0",          "vi-arg-digit" },            // alt-0
        { "\\M-1",          "vi-arg-digit" },            // alt-1
        { "\\M-2",          "vi-arg-digit" },            // alt-2
        { "\\M-3",          "vi-arg-digit" },            // alt-3
        { "\\M-4",          "vi-arg-digit" },            // alt-4
        { "\\M-5",          "vi-arg-digit" },            // alt-5
        { "\\M-6",          "vi-arg-digit" },            // alt-6
        { "\\M-7",          "vi-arg-digit" },            // alt-7
        { "\\M-8",          "vi-arg-digit" },            // alt-8
        { "\\M-9",          "vi-arg-digit" },            // alt-9
        { "\\M-[",          "arrow-key-prefix" },        // arrow key prefix
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        {}
    };

    static constexpr const char* const vi_movement_key_binds[][2] = {
        { " ",              "forward-char" },            // space (because of suggestions)
        { "$",              "end-of-line" },             // end (because of suggestions)
        { "l",              "forward-char" },            // l
        { "v",              "edit-and-execute-command" }, // v
        { "\\M-\\C-j",      "emacs-editing-mode" },      // alt-ctrl-j
        { "\\M-\\C-m",      "emacs-editing-mode" },      // alt-ctrl-m
        {}
    };

    const char* bindableEsc = get_bindable_esc();
    if (bindableEsc)
    {
        // REVIEW: When not using `terminal.raw_esc`, there's no clean way via
        // just key bindings to make ESC ESC do completion without interfering
        // with ESC by itself.  But binding bindableEsc,bindableEsc to
        // `complete` and unbinding bindableEsc would let ESC ESC do completion
        // as long as it's ok for ESC by itself to have no effect.
        // NOTE: When using `terminal.raw_esc`, it's expected that ESC doesn't
        // do anything by itself (except in vi mode, where there's a hack to
        // make ESC + timeout drop into vi command mode).
        rl_unbind_key_in_map(27/*alt-ctrl-[*/, emacs_meta_keymap);
        rl_unbind_key_in_map(27, vi_insertion_keymap);
        rl_bind_keyseq_in_map("\\e[27;7;219~"/*alt-ctrl-[*/, rl_named_function("complete"), emacs_standard_keymap);
        rl_bind_keyseq_in_map(bindableEsc, rl_named_function("clink-reset-line"), emacs_standard_keymap);
        rl_bind_keyseq_in_map(bindableEsc, rl_named_function("vi-movement-mode"), vi_insertion_keymap);
    }

    rl_unbind_key_in_map(' ', emacs_meta_keymap);
    bind_keyseq_list(general_key_binds, emacs_standard_keymap);
    bind_keyseq_list(emacs_key_binds, emacs_standard_keymap);
    bind_keyseq_list(bash_emacs_key_binds, emacs_standard_keymap);
    if (g_default_bindings.get() == 1)
        bind_keyseq_list(windows_emacs_key_binds, emacs_standard_keymap);

    bind_keyseq_list(general_key_binds, vi_insertion_keymap);
    bind_keyseq_list(general_key_binds, vi_movement_keymap);
    bind_keyseq_list(vi_insertion_key_binds, vi_insertion_keymap);
    bind_keyseq_list(vi_movement_key_binds, vi_movement_keymap);

    // Finally, load the inputrc file.
#ifdef CLINK_USE_LUA_EDITOR_TESTER
    if (state_dir)
#endif
        load_user_inputrc(state_dir, no_user);

    // Override the effect of any 'set keymap' assignments in the inputrc file.
    // This mimics what rl_initialize() does.
    rl_set_keymap_from_edit_mode();
}



//------------------------------------------------------------------------------
enum {
    bind_id_input,
    bind_id_left_click,
    bind_id_double_click,
    bind_id_drag,
    bind_id_more_input,
};



//------------------------------------------------------------------------------
void mouse_info::clear()
{
    m_x = m_y = -1;
    m_tick = GetTickCount() - 0xffff;
    m_clicks = 0;
    m_anchor1 = m_anchor2 = 0;
}

//------------------------------------------------------------------------------
int32 mouse_info::on_click(const uint32 x, const uint32 y, const bool dblclk)
{
    const DWORD now = GetTickCount();

    if (dblclk)
        m_clicks = 2;
    else if (m_clicks == 2 && x == m_x && y == m_y && now - m_tick <= GetDoubleClickTime())
        m_clicks = 3;
    else
        m_clicks = 1;

    m_x = static_cast<unsigned short>(x);
    m_y = static_cast<unsigned short>(y);
    m_tick = now;

    return m_clicks;
}

//------------------------------------------------------------------------------
int32 mouse_info::clicked() const
{
    return m_clicks;
}

//------------------------------------------------------------------------------
void mouse_info::set_anchor(int32 anchor1, int32 anchor2)
{
    m_anchor1 = anchor1;
    m_anchor2 = anchor2;
}

//------------------------------------------------------------------------------
bool mouse_info::get_anchor(int32 point, int32& anchor, int32& pos) const
{
    if (point < m_anchor1)
    {
        anchor = m_anchor2;
        pos = point;
        return true;
    }
    if (point >= m_anchor2)
    {
        anchor = m_anchor1;
        pos = point;
        return true;
    }
    anchor = m_anchor1;
    pos = m_anchor2;
    return false;
}



//------------------------------------------------------------------------------
rl_module::rl_module(terminal_in* input)
: m_prev_group(-1)
, m_old_int(SIG_DFL)
, m_old_break(SIG_DFL)
{
    if (g_debug_log_terminal.get())
    {
        static bool s_first = true;
        if (s_first)
        {
            s_first = false;
            LOG("terminal size %u x %u", _rl_screenwidth, _rl_screenheight);
        }
    }

    assert(!s_direct_input);
    s_direct_input = input;

    init_readline_hooks();

    _rl_eof_char = g_ctrld_exits.get() ? CTRL('D') : -1;
}

//------------------------------------------------------------------------------
rl_module::~rl_module()
{
    s_direct_input = nullptr;
}

//------------------------------------------------------------------------------
// Readline is designed for raw terminal input, and Windows is capable of richer
// input analysis where we can avoid generating terminal input if there's no
// binding that can handle it.
//
// WARNING:  Violates abstraction and encapsulation; neither rl_ding nor
// _rl_keyseq_chain_dispose make sense in an "is bound" method.  But really this
// is more like "accept_input_key" with the ability to reject an input key, and
// rl_ding or _rl_keyseq_chain_dispose only happen on rejection.  So it's
// functionally reasonable.
//
// The trouble is, Readline doesn't natively have a way to reset the dispatching
// state other than rl_abort() or actually dispatching an invalid key sequence.
// So we have to reverse engineer how Readline responds when a key sequence is
// terminated by invalid input, and that seems to consist of clearing the
// RL_STATE_MULTIKEY state and disposing of the key sequence chain.
bool rl_module::is_bound(const char* seq, int32 len)
{
    if (!len)
    {
LNope:
        if (RL_ISSTATE (RL_STATE_MULTIKEY))
        {
            RL_UNSETSTATE(RL_STATE_MULTIKEY);
            _rl_keyseq_chain_dispose();
        }
        rl_ding();
        return false;
    }

    // `quoted-insert` must accept all input (that's its whole purpose).
    if (rl_is_insert_next_callback_pending())
        return true;

    // The F2, F4, and F9 console compatibility implementations can accept
    // input, but extended keys are meaningless so don't accept them.  The
    // intent is to allow printable textual input, control characters, and ESC.
    if (win_fn_callback_pending())
    {
        const char* bindableEsc = get_bindable_esc();
        if (bindableEsc && strcmp(seq, bindableEsc) == 0)
            return true;
        if (len > 1 && seq[0] == '\x1b')
            goto LNope;
        return true;
    }

    // Various states should only accept "simple" input, i.e. not CSI sequences,
    // so that unrecognized portions of key sequences don't bleed in as textual
    // input.
    if (RL_ISSTATE(RL_SIMPLE_INPUT_STATES))
    {
        if (seq[0] == '\x1b')
            goto LNope;
        return true;
    }

    // The intent here is to accept all UTF8 input (not sure why readline
    // reports them as not bound, but this seems good enough for now).
    if (len > 1 && uint8(seq[0]) >= ' ')
        return true;

    // NOTE:  Checking readline's keymap is incorrect when a special bind group
    // is active that should block on_input from reaching readline.  But the way
    // that blocking is achieved is by adding a "" binding that matches
    // everything not explicitly bound in the keymap.  So it works out
    // naturally, without additional effort.

    // Using nullptr for the keymap starts from the root of the current keymap,
    // but in a multi key sequence this needs to use the current dispatching
    // node of the current keymap.
    Keymap keymap = RL_ISSTATE (RL_STATE_MULTIKEY) ? _rl_dispatching_keymap : nullptr;
    if (rl_function_of_keyseq_len(seq, len, keymap, nullptr))
        return true;

    goto LNope;
}

//------------------------------------------------------------------------------
bool rl_module::accepts_mouse_input(mouse_input_type type)
{
    // `quoted-insert` only accepts keyboard input.
    if (rl_is_insert_next_callback_pending())
        return false;

    // The F2, F4, and F9 console compatibility implementations only accept
    // keyboard input.
    if (win_fn_callback_pending())
        return false;

    // Various states should only accept "simple" input.
    if (RL_ISSTATE(RL_SIMPLE_INPUT_STATES))
        return false;

    // Multi-key chords only accept keyboard input.
    if (RL_ISSTATE(RL_STATE_MULTIKEY))
        return false;

    switch (type)
    {
    case mouse_input_type::left_click:
    case mouse_input_type::double_click:
    case mouse_input_type::drag:
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
bool rl_module::translate(const char* seq, int32 len, str_base& out)
{
    const char* bindableEsc = get_bindable_esc();
    if (!bindableEsc)
        return false;

    if (RL_ISSTATE(RL_STATE_NUMERICARG))
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            // Let ESC terminate numeric arg mode (digit mode) by redirecting it
            // to 'abort'.
            if (find_abort_in_keymap(out))
                return true;
        }
    }
    else if (RL_ISSTATE(RL_STATE_ISEARCH|RL_STATE_NSEARCH|RL_STATE_READSTR))
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            // Incremental and non-incremental search modes have hard-coded
            // handlers that abort on Ctrl+G, so redirect ESC to Ctrl+G.
            char tmp[2] = { ABORT_CHAR };
            out = tmp;
            return true;
        }
    }
    else if (RL_ISSTATE(RL_SIMPLE_INPUT_STATES) ||
             rl_is_insert_next_callback_pending() ||
             win_fn_callback_pending())
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            out = "\x1b";
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
static void suppress_redisplay()
{
    // Do nothing.  This is used to suppress the rl_redisplay_function call in
    // rl_message when set_prompt restores the readstr message prompt.
}

//------------------------------------------------------------------------------
void rl_module::set_prompt(const char* prompt, const char* const rprompt, const bool _redisplay, const bool transient)
{
    assertimplies(transient, _redisplay);
    const bool redisplay = _redisplay && (g_rl_buffer && g_printer);

    // Readline needs to be told about parts of the prompt that aren't visible
    // by enclosing them in a pair of 0x01/0x02 chars.

    str<> prev_prompt;
    str<> prev_rprompt;
    if (redisplay)
    {
        prev_prompt = m_rl_prompt.c_str();
        prev_rprompt = m_rl_rprompt.c_str();
    }

    m_rl_prompt.clear();
    m_rl_rprompt.clear();

    {
        str<16> tmp;
        const char* prompt_color = build_color_sequence(g_color_prompt, tmp, true);
        if (prompt_color)
        {
            str<16> leading_newlines;
            while (*prompt == '\r' || *prompt == '\n')
            {
                leading_newlines.concat(prompt, 1);
                ++prompt;
            }
            m_rl_prompt.format("%s\x01%s\x02", leading_newlines.c_str(), prompt_color);
            if (rprompt)
                m_rl_rprompt.format("\x01%s\x02", prompt_color);
        }
    }

    ecma48_processor_flags flags = ecma48_processor_flags::bracket;
    const ansi_handler native = get_native_ansi_handler();
    if (native != ansi_handler::conemu && native != ansi_handler::winterminal)
        flags |= ecma48_processor_flags::apply_title;
    ecma48_processor(prompt, &m_rl_prompt, nullptr/*cell_count*/, flags);
    if (rprompt)
        ecma48_processor(rprompt, &m_rl_rprompt, nullptr/*cell_count*/, flags);

    m_rl_prompt.concat("\x01\x1b[m\x02");
    if (rprompt)
        m_rl_rprompt.concat("\x01\x1b[m\x02");

    // Remember the prompt so the host can retrieve it.
    {
        dbg_ignore_scope(snapshot, "s_last_prompt");
        s_last_prompt.clear();
        s_last_prompt.concat(m_rl_prompt.c_str(), m_rl_prompt.length());
    }

    if (!transient &&
        m_rl_prompt.equals(prev_prompt.c_str()) &&
        m_rl_rprompt.equals(prev_rprompt.c_str()))
        return;

    // Erase the existing prompt.
    int32 was_visible = false;
    int32 clear_lines = 0;
    if (redisplay)
    {
        was_visible = show_cursor(false);
        lock_cursor(true);

        // Erase comment row if present.
        clear_comment_row();

        // Count the number of lines the prompt takes to display.
        int32 lines = count_prompt_lines(rl_get_local_prompt_prefix());

        clear_lines = lines;
    }

    // Larger scope than the others to affect rl_forced_update_display().
    rollback<bool> dmncr(g_display_manager_no_comment_row, transient || g_display_manager_no_comment_row);

    // Update the prompt.
    if (transient)
    {
        // Make sure no mode strings in the transient prompt.
        rollback<char*> ems(_rl_emacs_mode_str, const_cast<char*>(""));
        rollback<char*> vims(_rl_vi_ins_mode_str, const_cast<char*>(""));
        rollback<char*> vcms(_rl_vi_cmd_mode_str, const_cast<char*>(""));
        rollback<int32> eml(_rl_emacs_modestr_len, 0);
        rollback<int32> viml(_rl_vi_ins_modestr_len, 0);
        rollback<int32> vcml(_rl_vi_cmd_modestr_len, 0);
        rollback<int32> mml(_rl_mark_modified_lines, 0);

        rl_set_prompt(m_rl_prompt.c_str());
        rl_set_rprompt(m_rl_rprompt.c_str());
    }
    else
    {
        rl_set_prompt(m_rl_prompt.c_str());
        rl_set_rprompt(m_rl_rprompt.c_str());
    }

    // Restore message during RL_STATE_READSTR.
    if (RL_ISSTATE(RL_STATE_READSTR))
    {
        rollback<rl_voidfunc_t*> rdf(rl_redisplay_function, suppress_redisplay);
        char* p = _rl_make_prompt_for_search(_rl_readstr_pchar);
        rl_message("%s", p);
        xfree(p);
    }

    // Display the prompt.
    if (redisplay)
    {
        g_prompt_redisplay++;
        if (transient)
            reset_display_readline();
        defer_clear_lines(clear_lines, transient);
        rl_forced_update_display();

        lock_cursor(false);
        if (was_visible)
            show_cursor(true);
    }

    if (transient)
        reset_display_readline();
}

//------------------------------------------------------------------------------
bool rl_module::is_input_pending()
{
    return is_readline_input_pending();
}

//------------------------------------------------------------------------------
bool rl_module::next_line(str_base& out)
{
    if (m_queued_lines.empty())
    {
        out.clear();
        return false;
    }

    out = m_queued_lines[0].c_str();
    m_queued_lines.erase(m_queued_lines.begin());
    return true;
}

//------------------------------------------------------------------------------
bool rl_module::is_showing_argmatchers()
{
    return !!s_argmatcher_color;
}

//------------------------------------------------------------------------------
void rl_module::clear_need_collect_words()
{
    s_need_collect_words = false;
}

//------------------------------------------------------------------------------
void rl_module::bind_input(binder& binder)
{
    int32 default_group = binder.get_group();
    binder.bind(default_group, "\x1b[$*;*L", bind_id_left_click, true/*has_params*/);
    binder.bind(default_group, "\x1b[$*;*D", bind_id_double_click, true/*has_params*/);
    binder.bind(default_group, "\x1b[$*;*M", bind_id_drag, true/*has_params*/);
    binder.bind(default_group, "", bind_id_input);

    m_catch_group = binder.create_group("readline");
    binder.bind(m_catch_group, "", bind_id_more_input);
}

//------------------------------------------------------------------------------
void rl_module::on_begin_line(const context& context)
{
    const bool log_terminal = g_debug_log_terminal.get();

    m_old_int = signal(SIGINT, clink_sighandler);
#ifdef SIGBREAK
    m_old_break = signal(SIGBREAK, clink_sighandler);
#endif
    clink_install_ctrlevent();

    // Parse the match colors before installing the Readline callback handler,
    // so that any error messages are printed before any prompt display
    // happens.
    parse_match_colors();

    // Readline only detects terminal size changes while its line editor is
    // active.  If the terminal size isn't what Readline thought, then update
    // it now.
    refresh_terminal_size();

    // Reset the fwrite function so logging changes can take effect immediately.
    rl_fwrite_function = log_terminal ? terminal_log_write : terminal_write_thunk;
    rl_log_read_key_hook = log_terminal ? terminal_log_read : nullptr;

    // Note:  set_prompt() must happen while g_rl_buffer is nullptr otherwise
    // it will tell Readline about the new prompt, but Readline isn't set up
    // until rl_callback_handler_install further below.  set_prompt() happens
    // after g_printer and g_pager are set just in case it ever needs to print
    // output with ANSI escape code support.
    assert(!g_rl_buffer);
    g_pager = &context.pager;
    set_prompt(context.prompt, context.rprompt, false/*redisplay*/);
    g_rl_buffer = &context.buffer;
    set_prev_inputline(context.buffer.get_buffer(), context.buffer.get_length());
    if (g_classify_words.get())
        s_classifications = &context.classifications;
    g_prompt_refilter = g_prompt_redisplay = 0; // Used only by diagnostic output.

    _rl_face_modmark = FACE_MODMARK;
    _rl_display_modmark_color = build_color_sequence(g_color_modmark, m_modmark_color, true);

    _rl_face_horizscroll = FACE_SCROLL;
    _rl_face_message = FACE_MESSAGE;
    s_input_color = build_color_sequence(g_color_input, m_input_color, true);
    s_selection_color = build_color_sequence(g_color_selection, m_selection_color, true);
    s_arg_color = build_color_sequence(g_color_arg, m_arg_color, true);
    s_flag_color = build_color_sequence(g_color_flag, m_flag_color, true);
    s_unrecognized_color = build_color_sequence(g_color_unrecognized, m_unrecognized_color, true);
    s_executable_color = build_color_sequence(g_color_executable, m_executable_color, true);
    s_none_color = build_color_sequence(g_color_unexpected, m_none_color, true);
    s_argmatcher_color = build_color_sequence(g_color_argmatcher, m_argmatcher_color, true);
    _rl_display_horizscroll_color = build_color_sequence(g_color_horizscroll, m_horizscroll_color, true);
    _rl_display_message_color = build_color_sequence(g_color_message, m_message_color, true);
    _rl_pager_color = build_color_sequence(g_color_interact, m_pager_color);
    _rl_hidden_color = build_color_sequence(g_color_hidden, m_hidden_color);
    _rl_readonly_color = build_color_sequence(g_color_readonly, m_readonly_color);
    _rl_command_color = build_color_sequence(g_color_cmd, m_command_color);
    _rl_alias_color = build_color_sequence(g_color_doskey, m_alias_color);
    _rl_description_color = build_color_sequence(g_color_description, m_description_color, true);
    _rl_filtered_color = build_color_sequence(g_color_filtered, m_filtered_color, true);
    _rl_arginfo_color = build_color_sequence(g_color_arginfo, m_arginfo_color, true);
    _rl_selected_color = build_color_sequence(g_color_selected, m_selected_color);
    s_suggestion_color = build_color_sequence(g_color_suggestion, m_suggestion_color, true);
    s_histexpand_color = build_color_sequence(g_color_histexpand, m_histexpand_color, true);

    if (!s_selection_color && s_input_color)
    {
        m_selection_color.format("%s\x1b[7m", s_input_color);
        s_selection_color = m_selection_color.c_str();
    }

    if (!_rl_selected_color)
    {
        m_selected_color.format("0;1;7");
        _rl_selected_color = m_selected_color.c_str();
    }

    if (!_rl_display_message_color)
        _rl_display_message_color = "\x1b[m";

    lock_cursor(true); // Suppress cursor flicker.
    auto handler = [] (char* line) { rl_module::get()->done(line); };
    rl_set_rprompt(m_rl_rprompt.length() ? m_rl_rprompt.c_str() : nullptr);
    rl_callback_handler_install(m_rl_prompt.c_str(), handler);
    lock_cursor(false);

    // Apply the remembered history position from the previous command, if any.
    restore_sticky_search_position();

    m_done = !m_queued_lines.empty();
    m_eof = false;
    m_prev_group = -1;

    m_mouse.clear();
}

//------------------------------------------------------------------------------
void rl_module::on_end_line()
{
    s_suggestion.clear();

    if (!m_done)
        done(rl_line_buffer);

#ifdef USE_MEMORY_TRACKING
    // Force freeing any cached matches, to avoid the appearance of a leak.
    rl_menu_complete(-1, -1);
    rl_old_menu_complete(-1, -1);
#endif

    // When 'sticky' mode is enabled, remember the history position for the next
    // input line prompt.
    save_sticky_search_position();

    s_classifications = nullptr;
    s_input_color = nullptr;
    s_selection_color = nullptr;
    s_arg_color = nullptr;
    s_argmatcher_color = nullptr;
    s_flag_color = nullptr;
    s_unrecognized_color = nullptr;
    s_executable_color = nullptr;
    s_none_color = nullptr;
    s_suggestion_color = nullptr;
    _rl_display_modmark_color = nullptr;
    _rl_display_horizscroll_color = nullptr;
    _rl_display_message_color = nullptr;
    _rl_pager_color = nullptr;
    _rl_hidden_color = nullptr;
    _rl_readonly_color = nullptr;
    _rl_command_color = nullptr;
    _rl_alias_color = nullptr;
    _rl_filtered_color = nullptr;
    _rl_arginfo_color = nullptr;
    _rl_selected_color = nullptr;

    // This prevents any partial Readline state leaking from one line to the
    // next.  One case where this is necessary is CTRL-BREAK (not CTRL-C) at
    // the pager's "-- More --" prompt.
    RL_UNSETSTATE(RL_RESET_STATES);

    g_rl_buffer = nullptr;
    g_pager = nullptr;

    set_prev_inputline(nullptr);

    clink_shutdown_ctrlevent();
#ifdef SIGBREAK
    signal(SIGBREAK, m_old_break);
    m_old_break = SIG_DFL;
#endif
    signal(SIGINT, m_old_int);
    m_old_int = SIG_DFL;
}

//------------------------------------------------------------------------------
void rl_module::on_input(const input& input, result& result, const context& context)
{
    assert(!g_result);

    switch (input.id)
    {
    case bind_id_left_click:
    case bind_id_double_click:
    case bind_id_drag:
        {
            uint32 p0, p1;
            input.params.get(0, p0);
            input.params.get(1, p1);
            int32 pos;
            const bool drag = (input.id == bind_id_drag);
            if (translate_xy_to_readline(p0, p1, pos, drag && m_mouse.clicked()))
            {
                const int32 clicks = drag ? m_mouse.clicked() : m_mouse.on_click(p0, p1, input.id == bind_id_double_click);
                if (clicks == 3)
                {
                    cua_select_all(0, 0);
                }
                else if (clicks)
                {
                    if (drag)
                    {
                        int32 anchor;
                        if (m_mouse.get_anchor(pos, anchor, pos) && clicks == 2)
                        {
                            rollback<int32> rb(rl_point, pos);
                            if (pos < anchor)
                            {
                                rl_forward_word(1, 0);
                                rl_backward_word(1, 0);
                                if (rl_point > pos)
                                {
                                    rl_point = pos;
                                    rl_backward_word(1, 0);
                                }
                            }
                            else
                            {
                                rl_backward_word(1, 0);
                                rl_forward_word(1, 0);
                                if (rl_point <= pos)
                                {
                                    rl_point = pos;
                                    rl_forward_word(1, 0);
                                }
                            }
                            pos = rl_point;
                        }
                        g_rl_buffer->set_selection(anchor, pos);
                    }
                    else
                    {
                        const bool moved = (pos != rl_point);
                        g_rl_buffer->set_cursor(pos);
                        m_mouse.set_anchor(pos, pos);
                        if (moved)
                            g_rl_buffer->set_need_draw();
                        if (clicks == 2)
                        {
                            cua_select_word(0, 0);
                            m_mouse.set_anchor(g_rl_buffer->get_anchor(), g_rl_buffer->get_cursor());
                        }
                    }
                }
            }
            else
            {
                m_mouse.clear();
            }
            return;
        }
    }

    g_result = &result;

    // Setup the terminal.
    struct shim_in : public terminal_in
    {
        shim_in(const char* keys, terminal_in* old) : data(keys), old(old) { s_processed_input = this; }
        ~shim_in() { s_processed_input = old; }
        virtual int32   begin(bool) override                { assert(false); return 1; }
        virtual int32   end(bool) override                  { assert(false); return 0; }
        virtual bool    available(uint32 timeout) override  { assert(false); return false; }
        virtual void    select(input_idle*, uint32) override{ assert(false); }
        virtual int32   read() override                     { if (*data) return *(uint8*)(data++);
                                                              else if (old) return old->read();
                                                              else if (s_direct_input) return s_direct_input->read();
                                                              else return 0; }
        virtual int32   peek() override                     { assert(false); return 0; }
        virtual key_tester* set_key_tester(key_tester*) override { assert(false); return nullptr; }
        const char*  data;
        terminal_in* old;
    } term_in(input.keys, s_processed_input);

    s_matches = &context.matches;

    // Call Readline's until there's no characters left.
    uint32 len = input.len;
    rollback<uint32*> rb_input_len_ptr(s_input_len_ptr, &len);
    rollback<bool> rb_input_more(s_input_more, input.more);
    while (len && !m_done)
    {
        // Reset the scroll mode right before handling input so that "scroll
        // mode" can be deduced based on whether the most recently invoked
        // command called `console.scroll()` or `ScrollConsoleRelative()`.
        reset_scroll_mode();

        clear_pending_lastfunc();
        reset_command_states();

        // The history search position gets invalidated as soon as a non-
        // history search command is used.  So to make sticky search work
        // properly for history searches it's necessary to capture it on each
        // input, so that by the time rl_newline() is invoked the most recent
        // history search position has been cached.
        capture_sticky_search_position();

        // Capture the previous binding group.  This must be captured before
        // Readline handles the input, so that Readline commands can set the
        // binding group (e.g. clink-select-complete).
        if (m_prev_group < 0)
        {
            m_prev_group = result.set_bind_group(0);
            result.set_bind_group(m_prev_group);
        }

        // Always make sure result has the real prev group, so that Readline
        // commands can get the real prev group (not m_catch_group).
        if (m_prev_group >= 0)
            result.set_bind_group(m_prev_group);

        // Let Readline handle the next input char.
        --len;
        rl_callback_read_char();

        // Using `rl.invokecommand()` inside a "luafunc:" key binding should
        // set rl_last_func to reflect the last function that was invoked.
        // However, since Readline doesn't set rl_last_func until AFTER the
        // invoked function or macro returns, setting rl_last_func won't
        // "stick" unless it's set after rl_callback_read_char() returns.
        apply_pending_lastfunc();

        // NOTE:  There's ambiguity for quoted-insert.  Ideally the whole
        // console input key sequence could be inserted as quoted text (e.g.
        // CTRL-Q then UP).  But in a recorded key macro there's no way to
        // know how many characters should be quoted.  For consistency between
        // direct console input and recorded macros, the implementation here
        // no longer quotes the whole console input key sequence.
        //
        // Related commits:
        //  - 3a64c92f55ab8d55a979c6f9515eba99a82bb4a8, 2022/09/21 15:43:42
        //  - 62c44d2c75f998242c59af11db9cac78059189a5, 2021/09/18 11:22:51
        //  - f28e6018aa6b6b1e26c3a734ec82719abdf0109e, 2021/09/18  3:23:16
        //  - 7a2236ad48e742be6552a60e32ed26a11581dd8c, 2020/10/07 18:14:49
    }

    g_result = nullptr;
    s_matches = nullptr;

    if (is_force_reload_scripts())
    {
        end_prompt(false);
        reset_cached_font(); // Force discarding cached font info.
        readline_internal_teardown(true);
    }

    if (m_done)
    {
        result.done(m_eof);
        return;
    }

    // Check if Readline wants more input or if we're done.
    if (RL_ISSTATE(RL_MORE_INPUT_STATES))
    {
        assert(m_prev_group >= 0);
        int32 group = result.set_bind_group(m_catch_group);
        assert(group == m_prev_group || group == m_catch_group);
        suppress_unused_var(group);
    }
    else if (m_prev_group >= 0)
    {
        m_prev_group = -1;
    }
}

//------------------------------------------------------------------------------
void rl_module::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
    dbg_ignore_scope(snapshot, "rl_module needle");
    s_needle = needle;
}

//------------------------------------------------------------------------------
void rl_module::done(const char* line)
{
    if (line)
        m_queued_lines.emplace_back(line);
    m_done = true;
    m_eof = (line == nullptr);

    rl_callback_handler_remove();
}

//------------------------------------------------------------------------------
void rl_module::on_terminal_resize(int32, int32, const context& context)
{
    signal_terminal_resized();
    resize_readline_display(context.prompt, context.buffer, m_rl_prompt.c_str(), m_rl_rprompt.c_str());
}

//------------------------------------------------------------------------------
void rl_module::on_signal(int32 sig)
{
    rl_callback_handler_remove();
}
