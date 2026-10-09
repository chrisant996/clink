// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "rl_module.h"
#include <tib.h>
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
#include "host_callbacks.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_compare.h>
#include <core/str_hash.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/log.h>
#include <core/debugheap.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>
#include <terminal/terminal.h>          // for find_key_name()
#include <terminal/terminal_in.h>
#include <terminal/terminal_helpers.h>
#include <terminal/key_tester.h>
#include <terminal/screen_buffer.h>
#include <terminal/scroll.h>

#include <signal.h>
#include <unordered_set>
#include <vector>

extern "C" {
#include <compat/config.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/histlib.h>
#include <readline/keymaps.h>
#include <readline/xmalloc.h>
#include <compat/dirent.h>
#include <readline/posixdir.h>
extern int _rl_default_init_file_optional_set;
}

//------------------------------------------------------------------------------
const int32 RL_RESET_STATES = ~(RL_STATE_INITIALIZED|       // NOT these...
                                RL_STATE_TERMPREPPED|
                                RL_STATE_MACROINPUT|
                                RL_STATE_MACRODEF|
                                RL_STATE_OVERWRITE|
                                RL_STATE_CALLBACK|
                                RL_STATE_VICMDONCE|
                                RL_STATE_DONE|
                                RL_STATE_TIMEOUT|
                                RL_STATE_EOF);
const int32 RL_MORE_INPUT_STATES = (RL_STATE_READCMD|       // All of these...
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
const int32 RL_SIMPLE_INPUT_STATES = (RL_STATE_MOREINPUT|   // All of these...
                                      RL_STATE_NSEARCH|
                                      RL_STATE_CHARSEARCH|
                                      RL_STATE_READSTR);

extern "C" {
extern char*        _rl_comment_begin;
} // extern "C"

extern bool get_command_bindings(const char* command, bool friendly, str_base& desc, str_base& category, std::vector<str_moveable>& keys);

extern setting_color g_color_interact;
extern int32 g_prompt_refilter;
extern int32 g_prompt_redisplay;

static terminal_in* s_direct_input = nullptr;       // for read_key_hook and read thunk

// TODO: Refactor to avoid globals.
line_buffer*        g_rl_buffer = nullptr;
pager*              g_pager = nullptr;
editor_module::result* g_result = nullptr;

static str<>        s_last_prompt;
static str_moveable s_needle;

static bool s_build_suggestion_hint = false;
static uint16 s_suggestion_hint_width = 0;
static str_moveable s_suggestion_hint_text;
static suggestion_manager s_suggestion;

static std::shared_ptr<tib::key_table_list> s_emacs_standard_bindings;

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

setting_color g_color_description(
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

setting_enum g_default_bindings(
    "clink.default_bindings",
    "Selects default key bindings",
    "Clink uses bash key bindings when this is set to 'bash' (the default).\n"
    "When this is set to 'windows' Clink overrides some of the bash defaults with\n"
    "familiar Windows key bindings for Tab, Ctrl+F, Ctrl+M, and some others.",
    "bash,windows",
    0);

extern setting_bool g_debug_log_terminal;
extern setting_bool g_terminal_raw_esc;

extern setting_bool g_autosuggest_enable;
extern setting_bool g_autosuggest_inline;
extern setting_bool g_autosuggest_hint;

extern bool g_debug_log_input_pipeline;



//------------------------------------------------------------------------------
std::shared_ptr<const tib::key_table_list> get_emacs_standard_bindings()
{
    return s_emacs_standard_bindings;
}



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
                        tib::ding();
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
static bool s_input_more = false;
extern "C" int32 input_available_hook(void)
{
    assert(s_direct_input);
    if (s_direct_input)
    {
        // These are in order of next-ness:

        // Any remaining read-but-not-processed bytes in input chord?
        if (rl_has_clink_input())
            return true;

        // Any read-but-not-processed bytes not yet in input chord?  The binding
        // resolver may have more bytes pending.
        if (s_input_more)
            return true;

        // Any pending input from Readline?
        if (rl_has_queued_input())
            return true;

        // Any unread input available from stdin?
        // Passing -1 returns the current timeout without changing it.  The
        // timeout is in microseconds (µsec) so divide by 1000 for milliseconds.
        const int32 timeout = rl_set_keyboard_input_timeout(-1);
        if (s_direct_input->available(timeout > 0 ? timeout / 1000 : 0))
        {
            // Buffers the available input so it's available to Readline for
            // reading.  Clink's terminal_getc_thunk is designed to require a
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
    return terminal_in::is_input_byte(key) ? key : 0;
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
static bool s_in_command_output = false;
extern "C" void terminal_begin_command()
{
    // Windows Terminal built-in shell integration (WT v1.18).
    // https://gitlab.freedesktop.org/Per_Bothner/specifications/blob/master/proposals/semantic-prompts.md

    // Only print begin code once per block of command output.
    if (s_in_command_output)
        return;

    // Emit the shell integration code (which is a nop if disabled).
    str<> s;
    if (make_ftsc("133;C", s))
    {
        clink_write(s.c_str(), s.length());
        s_in_command_output = true;
    }
}

//------------------------------------------------------------------------------
extern "C" void terminal_end_command()
{
    // Only print end code one per block of command output.
    if (!s_in_command_output)
        return;

    // Clear the shell integration mode.
    s_in_command_output = false;

    // Emit the shell integration code.
    str<> s;
    if (make_ftsc("133;D", s))
        clink_write(s.c_str(), s.length());
}

//------------------------------------------------------------------------------
int32 terminal_getc_thunk(FILE* stream)
{
    if (stream == thunk_in_stream)
    {
#ifdef TIB_TODO
        assert(s_direct_input);
        if (rl_has_clink_input())
        {
            return rl_read_key();
        }
        else
        {
            assert(!s_input_more);
            assert(!rl_has_queued_input());
            assertimplies(_rl_reading_for_typeahead, s_direct_input->available(0));
            s_direct_input->select();
            return s_direct_input->read();
        }
#else
retry:
        const auto c = tib::term_in();
        if (c < 0 || c == tib::c_input_eof || c == tib::c_input_error)
            return EOF;
        if (terminal_in::is_input_event(c))
            goto retry;
        return c;
#endif
    }

    if (stream == thunk_null_stream)
        return 0;

    assert(false);
    return fgetc(stream);
}

//------------------------------------------------------------------------------
void terminal_log_read_key(int c, const char* _src)
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
static const word_classifications* s_classifications = nullptr;
static const char* s_input_color = nullptr;
static const char* s_selection_color = nullptr;
static const char* s_argmatcher_color = nullptr;
static const char* s_executable_color = nullptr;
static const char* s_command_color = nullptr;
static const char* s_alias_color = nullptr;
static const char* s_arg_color = nullptr;
static const char* s_flag_color = nullptr;
static const char* s_unrecognized_color = nullptr;
static const char* s_none_color = nullptr;
static const char* s_suggestion_color = nullptr;
static const char* s_histexpand_color = nullptr;
int32 g_suggestion_offset = -1;

//------------------------------------------------------------------------------
void rl_module::provide_faces(const tib::input_buffer& buffer, tib::cstring& faces)
{
    if (s_classifications)
    {
        for (uint32 i = 0; i < faces.length(); ++i)
        {
            char face = s_classifications->get_face(i);
            if (face != FACE_SPACE)
                faces.set_at(i, face);
        }
    }
}

//------------------------------------------------------------------------------
inline const char* fallback_color(const char* preferred, const char* fallback)
{
    return preferred ? preferred : fallback;
}

//------------------------------------------------------------------------------
const char* rl_module::get_face_def(char face)
{
    static const char c_normal[] = "\x1b[m";
#ifdef TIB_TODO
    static const char c_hyperlink[] = "\x1b]8;;";
    static const char c_BEL[] = "\a";
    static const char c_doc_histexpand[] = "https://chrisant996.github.io/clink/clink.html#using-history-expansion";
#endif

    switch (face)
    {
    default:
        if (s_classifications)
        {
            const char* color = s_classifications->get_face_output(face);
            if (color)
            {
                static str<32> s_out;
                s_out.clear();
                s_out << "\x1b[";
                if (color[0] != '0' || color[1] != ';')
                    s_out << "0;";
                s_out << color << "m";
                return s_out.c_str();
            }
        }
        return nullptr;

    // case FACE_NORMAL:           return c_normal;

    case FACE_MODMARK:          return fallback_color(_rl_display_modmark_color, c_normal);
    case FACE_MESSAGE:          return fallback_color(_rl_display_message_color, c_normal);

#if 0
    case tib::FACE_INPUT:       return fallback_color(s_input_color, c_normal);
    case tib::FACE_MARK:        return fallback_color(_rl_active_region_start_color, "\x1b[0;7m");
    case tib::FACE_SCROLLER:    return fallback_color(_rl_display_horizscroll_color, c_normal);
    case tib::FACE_SELECTION:   return fallback_color(s_selection_color, "\x1b[0;7m");
    case tib::FACE_SUGGESTION:
        assert(g_autosuggest_enable.get());
        assert(s_suggestion_color);
        return s_suggestion_color;
#endif

    case FACE_HISTEXPAND1:
    case FACE_HISTEXPAND2:
#ifdef TIB_TODO
        hyperlink.set(c_hyperlink);
        hyperlink.append(c_doc_histexpand);
        hyperlink.append(c_BEL);
#endif
        return fallback_color(s_histexpand_color, "\x1b[0;97;45m");


    case FACE_OTHER:        return fallback_color(s_input_color, c_normal);
    case FACE_UNRECOGNIZED: return fallback_color(s_unrecognized_color, fallback_color(s_input_color, c_normal));
    case FACE_EXECUTABLE:   return fallback_color(s_executable_color, fallback_color(s_input_color, c_normal));
    case FACE_COMMAND:      return fallback_color(s_command_color, c_normal);
    case FACE_ALIAS:        return fallback_color(s_alias_color, c_normal);
    case FACE_ARGMATCHER:   return fallback_color(s_argmatcher_color, c_normal);
    case FACE_ARGUMENT:     return fallback_color(s_arg_color, fallback_color(s_input_color, c_normal));
    case FACE_FLAG:         return fallback_color(s_flag_color, c_normal);
    case FACE_NONE:         return fallback_color(s_none_color, c_normal);
    }
}



//------------------------------------------------------------------------------
void set_suggestion_started(const char* line)
{
    s_suggestion.set_started(line);
}

//------------------------------------------------------------------------------
void set_suggestions(const char* line, uint32 endword_offset, suggestions* suggestions)
{
    s_suggestion.set(line, endword_offset, suggestions);
}

//------------------------------------------------------------------------------
bool get_suggestions(suggestions& out)
{
    return s_suggestion.get(out);
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
bool get_visible_suggestion(str_base& suffix, const char** usage, uint16* width)
{
    return s_suggestion.get_visible(suffix, usage, width);
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
bool is_locked_against_suggestions()
{
    return s_suggestion.is_locked_against_suggestions();
}

//------------------------------------------------------------------------------
extern "C" void lock_against_suggestions(int lock)
{
    s_suggestion.lock_against_suggestions(!!lock);
}

//------------------------------------------------------------------------------
extern "C" void clear_suggestion()
{
    if (!is_locked_against_suggestions())
        s_suggestion.clear();
}

//------------------------------------------------------------------------------
static void append_face(str_base& s, char face, uint32 count)
{
    while (count--)
        s.concat(&face, 1);
}

//------------------------------------------------------------------------------
const char* get_suggestion_hint_text(uint16* width)
{
    if (width)
        *width = s_suggestion_hint_width;
    return s_suggestion_hint_text.c_str();
}

//------------------------------------------------------------------------------
bool can_show_suggestion_hint()
{
    if (s_build_suggestion_hint)
    {
        s_suggestion_hint_text.clear();
        s_suggestion_hint_width = 0;
        if (g_autosuggest_hint.get())
        {
            static const char c_normal[] = "\x1b[m";
            static const char c_hyperlink[] = "\x1b]8;;";
            static const char c_BEL[] = "\a";
            static const char c_doc_autosuggest[] = DOC_HYPERLINK_AUTOSUGGEST;

            int32 type;
            str_moveable tmp;

            auto resolved = lookup_keyseq(*g_tib, "\x1b[C", 3);
            const bool has_right = (resolved.outcome == tib::dispatch_outcome::match &&
                                    g_autosuggest_inline.get() &&
                                    (resolved.is_func_name("win-cursor-forward") ||
                                     resolved.is_func_name("forward-char") ||
                                     resolved.is_func_name("forward-byte") ||
                                     resolved.is_func_name("end-of-line")));
            auto func_f2 = lookup_keyseq(*g_tib, "\x1bOQ", 3);
            const char* toggle_key_name = nullptr;
            if (func_f2.is_func_name("clink-toggle-suggestion-list"))
            {
                toggle_key_name = "F2";
            }
            else
            {
                str<> desc;
                str<> cat;
                std::vector<str_moveable> keys;
                if (get_command_bindings("clink-toggle-suggestion-list", true/*friendly*/, desc, cat, keys) && keys.size())
                {
                    tmp = std::move(keys[0]);
                    toggle_key_name = tmp.c_str();
                }
            }
            if (has_right || toggle_key_name)
            {
                s_suggestion_hint_text.concat("    ");
            }
            if (has_right)
            {
                s_suggestion_hint_text << s_suggestion_color << "\x1b[7m" << "Right" << "\x1b[27m=";
                s_suggestion_hint_text << c_hyperlink << c_doc_autosuggest << c_BEL;
                if (toggle_key_name)
                    s_suggestion_hint_text << "Insert";
                else
                    s_suggestion_hint_text << "Insert Suggestion";
                s_suggestion_hint_text << c_hyperlink << c_BEL;
                if (toggle_key_name)
                    s_suggestion_hint_text << " ";
            }
            if (toggle_key_name)
            {
                s_suggestion_hint_text << s_suggestion_color << "\x1b[7m" << toggle_key_name << "\x1b[27m=";
                s_suggestion_hint_text << c_hyperlink << c_doc_autosuggest << c_BEL;
                s_suggestion_hint_text << "List";
                if (!has_right)
                    s_suggestion_hint_text << " Suggestions";
                s_suggestion_hint_text << c_hyperlink << c_BEL;
            }
            s_suggestion_hint_width = cell_count(s_suggestion_hint_text.c_str());
            if (s_suggestion_hint_width)
                ++s_suggestion_hint_width;  // Pads it with trailing space to avoid wrap issues.
        }
        s_build_suggestion_hint = false;
    }
    return !s_suggestion_hint_text.empty();
}



//------------------------------------------------------------------------------
static const matches* s_matches = nullptr;

//------------------------------------------------------------------------------
extern "C" void free_match_list_hook(char** matches)
{
    destroy_matches_lookaside(matches);
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
            g_terminal->write(tmp.c_str(), tmp.length());
            tmp.format("\x1b[0;37;7m%.*s\x1b[m", word_len, buffer + word_break);
            g_terminal->write(tmp.c_str(), tmp.length());
            g_terminal->write("\x1b[K\x1b[u");
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
void init_prompt(const str_base& prompt, const str_base& rprompt)
{
// TODO-TIB: keep track of the prompt pieces better; set_left_text can't handle wrapping.
    const char* last_line = strrchr(prompt.c_str(), '\n');
    last_line = last_line ? last_line + 1 : prompt.c_str();
    g_prompt_prefix.clear();
    g_prompt.clear();
    g_rprompt.clear();
    g_prompt_prefix.concat(prompt.c_str(), int32(last_line - prompt.c_str()));
    g_prompt = last_line;
    g_rprompt.concat(rprompt.c_str(), rprompt.length());
    g_tib->set_left_text(g_prompt.c_str(), uint16_t(min<uint32>(cell_count(g_prompt.c_str()), tib::int16_max)));
    g_tib->set_right_text(g_rprompt.c_str(), cell_count(g_rprompt.c_str()));

#if 0
LOG("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
LOG("%s", transient ? "SET TRANSIENT PROMPT" : "SET NORMAL PROMPT");
LOG("m_rl_prompt = \"%s\"", m_rl_prompt.c_str());
LOG("m_rl_rprompt = \"%s\"", m_rl_rprompt.c_str());
LOG("g_prompt_prefix = \"%s\"", g_prompt_prefix.c_str());
LOG("g_prompt = \"%s\"", g_prompt.c_str());
LOG("g_rprompt = \"%s\"", g_rprompt.c_str());
#endif
}

//------------------------------------------------------------------------------
static int can_concat_undo_hook(UNDO_LIST* undo, const char* string)
{
    const double clock = os::clock();
    const double delta = clock - undo->clock;

    assert(undo->end > undo->start);
    const int was_space = whitespace(g_tib->get_text().c_str()[undo->end - 1]);
    const int is_space = whitespace(string[0]) && !string[1];

    const int can = ((delta < 0.1) ||
                     (delta < 5.0 && !(was_space && !is_space) && undo->end - undo->start < 20));

    if (can)
        undo->clock = clock;
    return can;
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
    // called, rl_find_completion_word() has already modified the caret.
    if (s_matches)
    {
        // Override Readline's word break position.  Often it's the same as
        // what Clink chose (possibly with help from generators), but Clink must
        // override it otherwise things go wrong in edge cases such as issue #59
        // (https://github.com/chrisant996/clink/issues/59).
        assert(s_matches->get_word_break_position() >= 0);
        if (s_matches->get_word_break_position() >= 0)
        {
            const auto old_caret = g_tib->get_caret();
            g_tib->set_caret(min<tib::textpos_t>(s_matches->get_word_break_position(), g_tib->get_length()));

            const char* pqc = nullptr;
            if (g_tib->get_caret() > 0)
            {
                // Check if the preceding character is a quote.
                pqc = strchr(rl_completer_quote_characters, g_tib->get_text().c_str()[g_tib->get_caret() - 1]);
                if (g_tib->get_caret() < old_caret && !(pqc && *pqc))
                {
                    // If the preceding character is not a quote, but the
                    // caret got moved and it points at a quote, then advance
                    // the caret so that lua scripts don't have to do quote
                    // handling.
                    pqc = strchr(rl_completer_quote_characters, g_tib->get_text().c_str()[g_tib->get_caret()]);
                    if (pqc && *pqc)
                        g_tib->set_caret(g_tib->get_caret() + 1);
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
static void buffer_changing(int32 event)
{
    // Reset the history position for the next input line prompt, upon changing
    // the input text at all.
    if (event != CHG_REPLACE && event != CHG_REPLACEEMPTY && has_sticky_search_position())
    {
        clear_sticky_search_position();
        if (!g_tib->get_length())
        {
            assert(!_rl_saved_line_for_history);
            using_history();
        }
    }

    // Lock against suggestions when rl_replace_text() is used.
    if (event == CHG_REPLACE || event == CHG_REPLACEEMPTY)
        lock_against_suggestions(event == CHG_REPLACE);
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
    assert(!need_collect_words());

    const display_filter_flags flags = display_filter_flags::none;
    if (matches* regen = maybe_regenerate_matches(text, flags))
    {
        // It's ok to redirect s_matches here because s_matches is reset in
        // every rl_module::on_input() call.
        s_matches = regen;
    }
    else
    {
        update_matches();
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
                override_last_command(nullptr, true/*force_when_null*/);
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
        omls.fully_qualify(start, end, tmp, s_matches->is_command_word());
        override = true;
    }
    else if (override)
    {
        // This applies an expanded abbreviated path.
        omls.override(start, end, tmp.c_str(), s_matches->is_command_word());
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

    // The command_word status is about the word index for which matches were
    // generated, not about the individual matches.  Either every match will
    // include the flag or every match will omit the flag.  If any match has
    // the flag, then the matches_lookaside table marks itself accordingly.
    const uint8 base_flags = (s_matches->is_command_word() ? MATCH_FLAG_COMMAND_WORD : 0);

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

        uint8 flags = base_flags;
        // if (s_matches->is_command_word())
        //     flags |= MATCH_FLAG_COMMAND_WORD;
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
void load_user_inputrc(const char* state_dir, bool no_user)
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
static void bind_keyseq_translated(const char* keys, int32 keys_len, const char* target, const std::shared_ptr<tib::key_table>& t)
{
#ifdef TIB_TODO // For now it's accepted that non-existent commands are in the lists.
    assert(tib::editor_context::lookup_command(target));
#endif
    if (target &&
        !is_luafunc_command(target) &&
        !tib::editor_context::lookup_command(target))
        return;

    if (!target || !*target)
    {
        tib::cstring seq;
        seq.set(keys, keys_len);
        t->remove(seq);
    }
    else if (stricmp(target, "do-lowercase-version") == 0)
    {
        t->add(keys, keys_len, tib::binding_target_lowercase_version());
    }
    else
    {
        t->add(keys, keys_len, tib::binding_target_func(target));
    }
}

//------------------------------------------------------------------------------
static void bind_keyseq(const char* keyseq, const char* target, const std::shared_ptr<tib::key_table>& t)
{
    assert(keyseq && *keyseq);

    const size_t need = 1 + (2 * strlen(keyseq));
    char* keys = (char*)malloc(need);
    if (!keys)
        return;

    int32 keys_len;
    if (rl_translate_keyseq(keyseq, keys, &keys_len))
    {
        assert(false);
        free(keys);
        return;
    }

    bind_keyseq_translated(keys, keys_len, target, t);

    free(keys);
}

//------------------------------------------------------------------------------
static void bind_keyseq_list(const two_strings* list, const std::shared_ptr<tib::key_table>& t)
{
    for (int32 i = 0; list[i][0]; ++i)
        bind_keyseq(list[i][0], list[i][1], t);
}

//------------------------------------------------------------------------------
static void init_emacs_standard_binds(bool force=false)
{
    if (s_emacs_standard_bindings && !force)
        return;

    if (!s_emacs_standard_bindings)
        init_editor_commands();

    static constexpr const char* const emacs_standard_binds[][2] = {
        // NORMAL KEY SEQUENCES
        { "\\C-@",          "set-mark" },               // Ctrl-@ (Ctrl-2)
        { "\\C-a",          "beginning-of-line" },      // Ctrl-A
        { "\\C-b",          "backward-char" },          // Ctrl-B
        { "\\C-d",          "delete-char" },            // Ctrl-D
        { "\\C-e",          "end-of-line" },            // Ctrl-E
        { "\\C-f",          "forward-char" },           // Ctrl-F
        { "\\C-g",          "abort" },                  // Ctrl-G
        // { "\\C-h",          "backward-kill-word" },     // VT sends 0x08 for Ctrl-Backspace.
        { "\\C-h",          "backward-delete-char" },   // Clink sends 0x08 for Backspace.
        { "\\C-i",          "complete" },               // Ctrl-I / TAB
        { "\\C-j",          "accept-line" },            // Ctrl-J
        { "\\C-k",          "kill-line" },              // Ctrl-K
        { "\\C-l",          "clear-screen" },           // Ctrl-L
        { "\\C-m",          "accept-line" },            // Ctrl-M / Enter
        { "\\C-n",          "next-history" },           // Ctrl-N
        { "\\C-o",          "operate-and-get-next" },   // Ctrl-O
        { "\\C-p",          "previous-history" },       // Ctrl-P
        { "\\C-q",          "quoted-insert" },          // Ctrl-Q
        { "\\C-e",          "reverse-search-history" }, // Ctrl-R
        { "\\C-s",          "forward-search-history" }, // Ctrl-S
        { "\\C-t",          "transpose-chars" },        // Ctrl-T
        { "\\C-u",          "unix-line-discard" },      // Ctrl-U
        { "\\C-v",          "quoted-insert" },          // Ctrl-V
        { "\\C-w",          "unix-word-rubout" },       // Ctrl-W
        { "\\C-y",          "yank" },                   // Ctrl-Y
        { "\\C-]",          "character-search" },       // Ctrl-]
        { "\\C-_",          "undo" },                   // Ctrl-_
        // { "\x7f",           "backward-delete-char" },   // RUBOUT / VT sends 0x7F for Backspace.
        { "\x7f",           "backward-kill-word" },     // RUBOUT / Clink sends 0x7F for Ctrl-Backspace.

        // META KEY SEQUENCES
        { "\\M-\\C-g",      "abort" },                  // Alt-Ctrl-G
        { "\\M-\\C-h",      "backward-kill-word" },     // Alt-Ctrl-H
        // { "\\M-\\C-i",      "tab-insert" },             // Alt-Ctrl-I
        // { "\\M-\\C-j",      "vi-editing-mode" },        // Alt-Ctrl-J
        { "\\M-\\C-l",      "clear-display" },          // Alt-Ctrl-L
        // { "\\M-\\C-m",      "vi-editing-mode" },        // Alt-Ctrl-M
        { "\\M-\\C-r",      "revert-line" },            // Alt-Ctrl-R
        { "\\M-\\C-y",      "yank-nth-arg" },           // Alt-Ctrl-Y
        { "\\M-\\C-[",      "complete" },               // Alt-ESC / ESC,ESC
        { "\\M-\\C-]",      "backward-character-search" }, // Alt-Ctrl-]
        { "\\M- ",          "set-mark" },               // Alt-SPACE
        { "\\M-#",          "insert-comment" },         // Alt-#
        { "\\M-&",          "tilde-expand" },           // Alt-&
        { "\\M-*",          "insert-completions" },     // Alt-*
        { "\\M--",          "digit-argument" },         // Alt--
        { "\\M-.",          "yank-last-arg" },          // Alt-.
        { "\\M-0",          "digit-argument" },         // Alt-0
        { "\\M-1",          "digit-argument" },         // Alt-1
        { "\\M-2",          "digit-argument" },         // Alt-2
        { "\\M-3",          "digit-argument" },         // Alt-3
        { "\\M-4",          "digit-argument" },         // Alt-4
        { "\\M-5",          "digit-argument" },         // Alt-5
        { "\\M-6",          "digit-argument" },         // Alt-6
        { "\\M-7",          "digit-argument" },         // Alt-7
        { "\\M-8",          "digit-argument" },         // Alt-8
        { "\\M-9",          "digit-argument" },         // Alt-9
        { "\\M-A",          "do-lowercase-version" },   // Alt-A
        { "\\M-B",          "do-lowercase-version" },   // Alt-B
        { "\\M-C",          "do-lowercase-version" },   // Alt-C
        { "\\M-D",          "do-lowercase-version" },   // Alt-D
        { "\\M-E",          "do-lowercase-version" },   // Alt-E
        { "\\M-F",          "do-lowercase-version" },   // Alt-F
        { "\\M-G",          "do-lowercase-version" },   // Alt-G
        { "\\M-H",          "do-lowercase-version" },   // Alt-H
        { "\\M-I",          "do-lowercase-version" },   // Alt-I
        { "\\M-J",          "do-lowercase-version" },   // Alt-J
        { "\\M-K",          "do-lowercase-version" },   // Alt-K
        { "\\M-L",          "do-lowercase-version" },   // Alt-L
        { "\\M-M",          "do-lowercase-version" },   // Alt-M
        { "\\M-N",          "do-lowercase-version" },   // Alt-N
        { "\\M-O",          "do-lowercase-version" },   // Alt-O
        { "\\M-P",          "do-lowercase-version" },   // Alt-P
        { "\\M-Q",          "do-lowercase-version" },   // Alt-Q
        { "\\M-R",          "do-lowercase-version" },   // Alt-R
        { "\\M-S",          "do-lowercase-version" },   // Alt-S
        { "\\M-T",          "do-lowercase-version" },   // Alt-T
        { "\\M-U",          "do-lowercase-version" },   // Alt-U
        { "\\M-V",          "do-lowercase-version" },   // Alt-V
        { "\\M-W",          "do-lowercase-version" },   // Alt-W
        { "\\M-X",          "do-lowercase-version" },   // Alt-X
        { "\\M-Y",          "do-lowercase-version" },   // Alt-Y
        { "\\M-Z",          "do-lowercase-version" },   // Alt-Z
        { "\\M-\\",         "delete-horizontal-space" }, // Alt-\ (don't end with \ or the compiler joins lines)
        { "\\M-_",          "yank-last-arg" },          // Alt-_
        { "\\M-b",          "backward-word" },          // Alt-b
        { "\\M-c",          "capitalize-word" },        // Alt-c
        { "\\M-d",          "kill-word" },              // Alt-d
        { "\\M-f",          "forward-word" },           // Alt-f
        { "\\M-l",          "downcase-word" },          // Alt-l
        { "\\M-n",          "non-incremental-forward-search-history" }, // Alt-n
        { "\\M-p",          "non-incremental-backward-search-history" }, // Alt-p
        { "\\M-r",          "revert-line" },            // Alt-r
        { "\\M-t",          "transpose-words" },        // Alt-t
        { "\\M-u",          "upcase-word" },            // Alt-u
        { "\\M-x",          "execute-named-command" },  // Alt-x
        { "\\M-y",          "yank-pop" },               // Alt-y
        { "\\M-~",          "tilde-expand" },           // Alt-~
        { "\\M-\x7f",       "backward-kill-word" },     // Alt-RUBOUT

        // CTRL-X KEY SEQUENCES
        { "\\C-x\\C-g",     "abort" },                  // Ctrl-X,Ctrl-G
        { "\\C-x\\C-r",     "re-read-init-file" },      // Ctrl-X,Ctrl-R
        { "\\C-x\\C-u",     "undo" },                   // Ctrl-X,Ctrl-U
        { "\\C-x\\C-x",     "exchange-point-and-mark" }, // Ctrl-X,Ctrl-X
        { "\\C-x\\C-(",     "start-kbd-macro" },        // Ctrl-X,Ctrl-(
        { "\\C-x\\C-)",     "end-kbd-macro" },          // Ctrl-X,Ctrl-)
        { "\\C-xA",         "do-lowercase-version" },   // Ctrl-X,A
        { "\\C-xB",         "do-lowercase-version" },   // Ctrl-X,B
        { "\\C-xC",         "do-lowercase-version" },   // Ctrl-X,C
        { "\\C-xD",         "do-lowercase-version" },   // Ctrl-X,D
        { "\\C-xE",         "do-lowercase-version" },   // Ctrl-X,E
        { "\\C-xF",         "do-lowercase-version" },   // Ctrl-X,F
        { "\\C-xG",         "do-lowercase-version" },   // Ctrl-X,G
        { "\\C-xH",         "do-lowercase-version" },   // Ctrl-X,H
        { "\\C-xI",         "do-lowercase-version" },   // Ctrl-X,I
        { "\\C-xJ",         "do-lowercase-version" },   // Ctrl-X,J
        { "\\C-xK",         "do-lowercase-version" },   // Ctrl-X,K
        { "\\C-xL",         "do-lowercase-version" },   // Ctrl-X,L
        { "\\C-xM",         "do-lowercase-version" },   // Ctrl-X,M
        { "\\C-xN",         "do-lowercase-version" },   // Ctrl-X,N
        { "\\C-xO",         "do-lowercase-version" },   // Ctrl-X,O
        { "\\C-xP",         "do-lowercase-version" },   // Ctrl-X,P
        { "\\C-xQ",         "do-lowercase-version" },   // Ctrl-X,Q
        { "\\C-xR",         "do-lowercase-version" },   // Ctrl-X,R
        { "\\C-xS",         "do-lowercase-version" },   // Ctrl-X,S
        { "\\C-xT",         "do-lowercase-version" },   // Ctrl-X,T
        { "\\C-xU",         "do-lowercase-version" },   // Ctrl-X,U
        { "\\C-xV",         "do-lowercase-version" },   // Ctrl-X,V
        { "\\C-xW",         "do-lowercase-version" },   // Ctrl-X,W
        { "\\C-xX",         "do-lowercase-version" },   // Ctrl-X,X
        { "\\C-xY",         "do-lowercase-version" },   // Ctrl-X,Y
        { "\\C-xZ",         "do-lowercase-version" },   // Ctrl-X,Z
        { "\\C-xe",         "call-last-kbd-macro" },    // Ctrl-X,e
        { "\\C-x\x7f",      "backward-kill-line" },     // Ctrl-X,RUBOUT
        {}
    };

    auto t = std::make_shared<tib::key_table>(true/*can_self_insert*/);
    bind_keyseq_list(emacs_standard_binds, t);

    s_emacs_standard_bindings = std::make_shared<tib::key_table_list>();
    s_emacs_standard_bindings->emplace_back(std::move(t));
}

//------------------------------------------------------------------------------
extern "C" void set_key_table(int table)
{
    switch (table)
    {
    case emacs_table:   g_tib->set_bindings(s_emacs_standard_bindings); break;
    default:            assert(false); return;
    }
}

//------------------------------------------------------------------------------
extern "C" int get_key_table(void)
{
    return emacs_table;
}

//------------------------------------------------------------------------------
extern "C" void clink_bind_translated(int is_macro, const char* keys, int keys_len, const char* target, int table)
{
    assert(s_emacs_standard_bindings);
    if (!s_emacs_standard_bindings)
        return;

    std::shared_ptr<tib::key_table> t;
    switch (table)
    {
    case emacs_table:   t = s_emacs_standard_bindings->at(0); break;
    default:            return;
    }

    if (!is_macro || !target)
    {
        bind_keyseq_translated(keys, keys_len, target, t);
    }
    else if (strnicmp(target, "luafunc:", 8) == 0)
    {
        t->add(keys, keys_len, tib::binding_target_func(target));
    }
    else
    {
        t->add(keys, keys_len, tib::binding_target_macro(target));
    }
}

//------------------------------------------------------------------------------
extern "C" void clink_bind(const char* keyseq, const char* target, int table)
{
    assert(keyseq && *keyseq);

    const size_t need = 1 + (2 * strlen(keyseq));
    char* keys = (char*)malloc(need);
    char* macro = nullptr;
    if (!keys)
        return;

    int32 keys_len;
    if (rl_translate_keyseq(keyseq, keys, &keys_len))
    {
        assert(false);
        free(keys);
        return;
    }

    clink_bind_translated(false, keys, keys_len, target, table);

    free(macro);
    free(keys);
}

//------------------------------------------------------------------------------
extern "C" void clink_bind_macro(const char* keyseq, const char* target, int table)
{
    assert(keyseq && *keyseq);

    const size_t need = 1 + (2 * strlen(keyseq));
    char* keys = (char*)malloc(need);
    char* macro = nullptr;
    if (!keys)
        return;

    int32 keys_len;
    if (rl_translate_keyseq(keyseq, keys, &keys_len))
    {
        assert(false);
        free(keys);
        return;
    }

    {
        int macro_len;
        macro = (char*)malloc((2 * strlen(target)) + 1);
        if (rl_translate_keyseq(target, macro, &macro_len))
            goto out;
        target = macro;
    }

    clink_bind_translated(true, keys, keys_len, target, table);

out:
    free(macro);
    free(keys);
}

//------------------------------------------------------------------------------
extern "C" void clink_bind_list(const two_strings* list, int table)
{
    for (int32 i = 0; list[i][0]; ++i)
        clink_bind(list[i][0], list[i][1], table);
}

//------------------------------------------------------------------------------
#ifdef UNDO_LIST_HEAP_DIAGNOSTICS
static bool undo_list_root_is_interior(UNDO_LIST* undo, UNDO_LIST* root)
{
    if (root)
    {
        for (undo = undo ? undo->next : nullptr; undo; undo = undo->next)
            if (undo == root)
                return true;
    }
    return false;
}

static void assert_no_undo_list_interior_roots(UNDO_LIST* undo)
{
    if (!undo)
        return;

    assert(!undo_list_root_is_interior(undo, rl_undo_list));

    HIST_ENTRY** const history = history_list();
    for (int32 i = 0; history && i < history_length; i++)
        if (history[i])
            assert(!undo_list_root_is_interior(undo, (UNDO_LIST*)history[i]->data));

    if (_rl_saved_line_for_history)
        assert(!undo_list_root_is_interior(undo, (UNDO_LIST*)_rl_saved_line_for_history->data));

    assert(!undo_list_root_is_interior(undo, _rl_get_saved_search_undo_list()));
    assert(!undo_list_root_is_interior(undo, _rl_get_saved_readstr_undo_list()));
}
#endif

//------------------------------------------------------------------------------
static void init_readline_hooks()
{
    static bool s_first_time = true;

    // The Readline terminal hooks must be set even before calling
    // rl_initialize(), because it can invoke e.g. rl_fwrite_function which
    // needs to intercept some escape sequences even during initialization.
    // And reset these for each input line because of g_debug_log_terminal.
    init_rl_terminal_thunks();

    if (!s_first_time)
        return;
    s_first_time = false;

    // Input event hooks.
    rl_input_available_hook = input_available_hook;
    rl_read_key_hook = read_key_hook;
    rl_buffer_changing_hook = buffer_changing;
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
    rl_is_exec_func = is_exec_ext;

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

    // Undo list heap diagnostics.
#ifdef UNDO_LIST_HEAP_DIAGNOSTICS
    rl_on_free_undo_list_func = assert_no_undo_list_interior_roots;
#endif
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

    if (restore)
    {
        init_emacs_standard_binds(true/*force*/);
#ifdef TIB_TODO
        init_vi_movement_binds(true/*force*/);
        init_vi_insertion_binds(true/*force*/);
#endif
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
        // { &_rl_convert_meta_chars_to_ascii              },  // "convert-meta"
        { &rl_inhibit_completion                        },  // "disable-completion"
        // { &_rl_echo_control_chars                       },  // "echo-control-characters"
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
        // { &_rl_meta_flag                                },  // "input-meta"
        { &_rl_keyseq_timeout                           },  // "keyseq-timeout"
        { &_rl_complete_mark_directories                },  // "mark-directories"
        { &_rl_mark_modified_lines                      },  // "mark-modified-lines"
        { &_rl_complete_mark_symlink_dirs               },  // "mark-symlinked-directories"
        { &_rl_match_hidden_files                       },  // "match-hidden-files"
        { &_rl_menu_complete_prefix_first               },  // "menu-complete-display-prefix"
        { &_rl_menu_complete_wraparound                 },  // "menu-complete-wraparound"
        // { &_rl_meta_flag                                },  // "meta-flag"
        // { &_rl_output_meta_chars                        },  // "output-meta"
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
void rl_preinit(const char* default_inputrc)
{
    _rl_default_init_file = (default_inputrc && *default_inputrc) ? default_inputrc : nullptr;

    // The "default_inputrc" file was introduced in v1.3.5, but up through
    // v1.6.0 it wasn't actually loaded properly.  And the provided one
    // had syntax errors (missing the "set" keyword), so to compensate now
    // the "set" keyword is optional in the "default_inputrc" file.
    _rl_default_init_file_optional_set = 1;
}

//------------------------------------------------------------------------------
void rl_postinit()
{
    // Override some defaults.
    _rl_bell_preference = VISIBLE_BELL;     // Because audible is annoying.
    rl_complete_with_tilde_expansion = 1;   // Since CMD doesn't understand tilde.
}

//------------------------------------------------------------------------------
void initialise_readline(bool no_user)
{
    // Can't give a more specific scope like "Readline initialization", because
    // realloc of some things will use "Readline" and assert on label change.
    dbg_ignore_scope(snapshot, "Readline");

    int32 id;
    host_context context;
    host_get_app_context(id, context);
    const char* const state_dir = context.profile.empty() ? nullptr : context.profile.c_str();
    const char* const default_inputrc = context.default_inputrc.empty() ? nullptr : context.default_inputrc.c_str();

#if 0
    // Readline needs a tweak of its handling of 'meta' (i.e. IO bytes >=0x80)
    // so that it handles UTF-8 correctly (convert=input, output=output).
    // Because these affect key binding translations, these are set even before
    // calling rl_initialize() or binding any other keys.
    _rl_convert_meta_chars_to_ascii = 0;
    _rl_output_meta_chars = 1;
#endif

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
        rl_preinit(s_default_inputrc.c_str());

        init_readline_hooks();
        init_emacs_standard_binds();

        // Clink manages showing and hiding the cursor; tib should not.
        tib::g_show_hide_cursor = false;

        // Wait until after registering the editor commands, so it doesn't
        // trigger auto-registering tib's list of commands.
        g_tib = std::make_shared<tib::input_box>();

        // Install signal handlers so that Readline doesn't trigger process exit
        // in response to Ctrl+C or Ctrl+Break.
        rl_catch_signals = 1;
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
        rl_readline_name = "clink";
        rl_initialize();

        rl_postinit();
    }

    // Save/restore the original keymap table definitions and original config
    // variable values so that reloading the inputrc doesn't have lingering
    // key bindings or config variables values.
    save_restore_initial_state(initialized);

    // Bind extended keys so editing follows Windows' conventions.
    static constexpr const char* const emacs_key_binds[][2] = {
        { "\\e[1;5F",       "kill-line" },               // ctrl-end
        { "\\e[1;5H",       "backward-kill-line" },      // ctrl-home
        { "\\d",            "backward-kill-word" },      // ctrl-backspace
        { "\\C-v",          "clink-paste" },             // ctrl-v
        { "\\C-z",          "undo" },                    // ctrl-z
        { "\\C-x*",         "glob-expand-word" },        // ctrl-x,*
        { "\\C-xg",         "glob-list-expansions" },    // ctrl-x,g
        { "\\C-x\\C-e",     "edit-and-execute-command" }, // ctrl-x,ctrl-e
        { "\\C-x\\C-r",     "clink-reload" },            // ctrl-x,ctrl-r
        { "\\C-x\\C-z",     "clink-diagnostics" },       // ctrl-x,ctrl-z
        { "\\C-x\\e[27;6;90~", "clink-diagnostics-output" }, // ctrl-x,ctrl-shift-z
        { "\\M-f",          "forward-word" },            // alt-f (because of suggestions)
        { "\\M-g",          "glob-complete-word" },      // alt-g
        { "\\eOP",          "win-cursor-forward" },      // F1
        //{ "\\eOQ",          "win-copy-up-to-char" },     // F2 (superseded by F2 for toggle suggestion list)
        { "\\eOR",          "win-copy-up-to-end" },      // F3
        { "\\eOS",          "win-delete-up-to-char" },   // F4
        { "\\e[15~",        "previous-history" },        // F5
        { "\\e[17~",        "win-insert-eof" },          // F6
        { "\\e[18~",        "clink-popup-history" },     // F7
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
        { "\\C-e",          "end-of-line" },             // ctrl-e (because of suggestions)
        { "\\C-f",          "forward-char" },            // ctrl-f (because of suggestions)
        { "\\e[27;5;77~",   "" },                        // ctrl-m (differentiated)
        { "\\e[C",          "forward-char" },            // right (because of suggestions)
        { "\t",             "complete" },                // tab
        { "\\e[Z",          "" },                        // shift-tab
        {}
    };

    static constexpr const char* const general_key_binds[][2] = {
        { "\\e[A",          "get-previous-history" },    // up
        { "\\e[B",          "get-next-history" },        // down
        { "\\e[C",          "forward-char" },            // right
        { "\\e[D",          "backward-char" },           // left
        { "\\e[F",          "end-of-line" },             // end
        { "\\e[H",          "beginning-of-line" },       // home
        { "\\e[3~",         "delete-char" },             // del
        { "\\e[2~",         "overwrite-mode" },          // ins
        { "\\e[5~",         "history-search-backward" }, // pgup
        { "\\e[6~",         "history-search-forward" },  // pgdn
        { "\\C-c",          "clink-ctrl-c" },            // ctrl-c
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
        { "\\eOQ",          "clink-toggle-suggestion-list" }, // F2
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
        { "\\e[1;3D",       "backward-word" },           // alt-left
        { "\\e[1;3C",       "forward-word" },            // alt-right
        { "\\e[1;2A",       "cua-previous-screen-line" },// shift-up
        { "\\e[1;2B",       "cua-next-screen-line" },    // shift-down
        { "\\e[1;2D",       "cua-backward-char" },       // shift-left
        { "\\e[1;2C",       "cua-forward-char" },        // shift-right
        { "\\e[1;6D",       "cua-backward-word" },       // ctrl-shift-left
        { "\\e[1;6C",       "cua-forward-word" },        // ctrl-shift-right
        { "\\e[1;2H",       "cua-beg-of-line" },         // shift-home
        { "\\e[1;2F",       "cua-end-of-line" },         // shift-end
        { "\\e[2;5~",       "cua-copy" },                // ctrl-ins
        { "\\e[3;5~",       "kill-word" },               // ctrl-del
        { "\\e[2;2~",       "clink-paste" },             // shift-ins
        { "\\e[3;2~",       "cua-cut" },                 // shift-del
        { "\\e[27;2;32~",   "clink-shift-space" },       // shift-space
        {}
    };

#ifdef TIB_TODO
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
        { "l",              "forward-char" },            // l (because of suggestions)
        { "v",              "edit-and-execute-command" }, // v
        { "\\M-\\C-j",      "emacs-editing-mode" },      // alt-ctrl-j
        { "\\M-\\C-m",      "emacs-editing-mode" },      // alt-ctrl-m
        {}
    };
#endif

#ifdef DEBUG
    static constexpr const char* const temporary_R_and_D[][2] = {
        { "\\C-xf",         "clink-dump-functions" },
        { "\\C-xm",         "clink-dump-macros" },
        {}
    };
#endif

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
        clink_bind("\\M-\\C-["/*alt-ctrl-[*/, nullptr, emacs_table);
#ifdef TIB_TODO
        clink_bind("\\e", nullptr, vi_insertion_table);
#endif
        clink_bind(bindableEsc, "clink-reset-line", emacs_table);
#ifdef TIB_TODO
        clink_bind(bindableEsc, "vi-movement-mode", vi_insertion_table);
#endif
        if (g_default_bindings.get() == 0/*bash*/)
        {
            str<16> tmp;
            tmp.concat(bindableEsc);
            tmp.concat(bindableEsc);
            clink_bind(tmp.c_str()/*Esc,Esc*/, "complete", emacs_table);
        }
    }

    clink_bind("\\e ", nullptr, emacs_table);
    clink_bind_list(general_key_binds, emacs_table);
    clink_bind_list(emacs_key_binds, emacs_table);
    clink_bind_list(bash_emacs_key_binds, emacs_table);
    if (g_default_bindings.get() == 1)
        clink_bind_list(windows_emacs_key_binds, emacs_table);

#ifdef DEBUG
    clink_bind_list(temporary_R_and_D, emacs_table);
#endif

    clink_bind(")", nullptr, emacs_table);
    clink_bind("]", nullptr, emacs_table);
    clink_bind("}", nullptr, emacs_table);

// TODO-TIB: vi modes.
#ifdef TIB_TODO
    clink_bind_list(general_key_binds, vi_insertion_table);
    clink_bind_list(general_key_binds, vi_movement_table);
    clink_bind_list(vi_insertion_key_binds, vi_insertion_table);
    clink_bind_list(vi_movement_key_binds, vi_movement_table);
#endif

    g_tib->set_bindings(s_emacs_standard_bindings);

    // Finally, load the inputrc file.
    load_user_inputrc(state_dir, no_user);

    g_bell_preference = static_cast<bell_preference>(_rl_bell_preference);

    if (rl_blink_matching_paren)
    {
        clink_bind(")", "insert-close", emacs_table);
        clink_bind("]", "insert-close", emacs_table);
        clink_bind("}", "insert-close", emacs_table);
    }

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
rl_module::rl_module()
: m_catch_group(-1)
, m_done(false)
, m_eof(false)
, m_has_pending_line(false)
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
    assert(tib_terminal_bridge::get());
    s_direct_input = tib_terminal_bridge::get()->get_in();

    init_readline_hooks();

#ifdef TIB_TODO
// TODO-TIB: this is not how to hook up CTRL-D EOF handling; that needs to be
// built into tib itself, not as a key binding.
    tib::editor_context::register_command("clink-eof",
        [](tib::editor_context& ctx, int32_t, const char*, const tib::binding_params*) -> int32_t {
            if (ctx.get_text().empty() && g_ctrld_exits.get())
            {
                ctx.set_done();
                rl_module::get()->done(nullptr);
            }
            else
                ctx.del();
            return 0;
        });

    _rl_eof_char = g_ctrld_exits.get() ? CTRL('D') : -1;
#endif
}

//------------------------------------------------------------------------------
rl_module::~rl_module()
{
    s_direct_input = nullptr;
}

//------------------------------------------------------------------------------
bool rl_module::is_bound(const char* seq, int32 len)
{
    return m_terminal->is_bound(seq, len);
}

//------------------------------------------------------------------------------
bool rl_module::accepts_mouse_input(mouse_input_type)
{
// TODO-TIB: Clink's private mouse encoding still needs an adapter to tib.
#ifdef TIB_TODO
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
#endif

    return false;
}

//------------------------------------------------------------------------------
bool rl_module::translate(const char* seq, int32 len, str_base& out)
{
    const char* bindableEsc = get_bindable_esc();
    if (!bindableEsc)
        return false;

#ifdef TIB_TODO
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
#else
    if (quoted_insert_pending())
#endif
    {
        if (len == int32(strlen(bindableEsc)) &&
            memcmp(seq, bindableEsc, len) == 0)
        {
            out = "\x1b";
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
void rl_module::set_prompt(const char* prompt, const char* rprompt, bool redisplay, bool transient)
{
    assertimplies(transient, redisplay);
#ifdef TIB_TODO
    const bool redisplay = _redisplay && (g_rl_buffer && g_terminal);
#endif

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
            m_rl_prompt.concat(leading_newlines.c_str(), leading_newlines.length());
            m_rl_prompt.concat(prompt_color);
            if (rprompt)
                m_rl_rprompt.concat(prompt_color);
        }
    }

    ecma48_processor_flags flags = ecma48_processor_flags::bracket;
    const ansi_handler term = get_current_ansi_handler();
    if (term != ansi_handler::conemu && term != ansi_handler::winterminal)
        flags |= ecma48_processor_flags::apply_title;
    ecma48_processor(prompt, &m_rl_prompt, nullptr/*cell_count*/, flags);
    if (rprompt)
        ecma48_processor(rprompt, &m_rl_rprompt, nullptr/*cell_count*/, flags);

    m_rl_prompt.concat("\x1b[m");
    if (rprompt)
        m_rl_rprompt.concat("\x1b[m");

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
    bool nested_coalesce = tib::display_accumulator::active();
    int32 was_visible = false;
    int32 clear_lines = 0;
    if (redisplay)
    {
        was_visible = !nested_coalesce && show_cursor(false);
        lock_cursor(true);

        // Erase comment row if present and transient prompt.
        if (transient)
// TODO-TIB: not the right way.
            clear_comment_row();

        // Count the number of lines the prompt takes to display.
// TODO-TIB: account for top border and wrapping of prompt left text.
        int32 lines = count_prompt_lines(g_prompt_prefix.c_str());

        clear_lines = lines;
    }

    // Larger scope than the others to affect rl_forced_update_display().
// TODO-TIB: this won't correctly remove the comment row.
    rollback<bool> dmncr(g_display_manager_no_comment_row, transient || g_display_manager_no_comment_row);

    // Update the prompt.
    if (transient)
    {
        // Make sure no mode strings in the transient prompt.
#ifdef TIB_TODO
        rollback<char*> ems(_rl_emacs_mode_str, const_cast<char*>(""));
        rollback<char*> vims(_rl_vi_ins_mode_str, const_cast<char*>(""));
        rollback<char*> vcms(_rl_vi_cmd_mode_str, const_cast<char*>(""));
        rollback<int32> eml(_rl_emacs_modestr_len, 0);
        rollback<int32> viml(_rl_vi_ins_modestr_len, 0);
        rollback<int32> vcml(_rl_vi_cmd_modestr_len, 0);
        rollback<int32> mml(_rl_mark_modified_lines, 0);
#endif
        fixup_prompt(m_rl_prompt);
        fixup_rprompt(m_rl_rprompt);
    }
    else
    {
// TODO-TIB: expand prompt and inject mode string.
        fixup_prompt(m_rl_prompt);
        fixup_rprompt(m_rl_rprompt);
    }

    init_prompt(m_rl_prompt, m_rl_rprompt);

    // Restore message during RL_STATE_READSTR.
#ifdef TIB_TODO
    if (RL_ISSTATE(RL_STATE_READSTR))
    {
        char* p = _rl_make_prompt_for_search(_rl_readstr_pchar);
        rl_message_append("%s", p);
        xfree(p);
    }
#endif

    // Display the prompt.
// TODO-TIB: why was m_active added?
    if (redisplay && m_active)
    {
        g_prompt_redisplay++;
        if (transient)
            reset_display_readline();
        defer_clear_lines(clear_lines, transient);

        {
            // Let readline_display know whether it's a transient prompt, so
            // it can keep the comment row disabled until the display manager
            // gets reset.
            transient_prompt_context tpc(transient);

            force_redisplay_readline();
            display_readline();
        }

        lock_cursor(false);
        if (!nested_coalesce && was_visible)
            show_cursor(true);
    }
}

//------------------------------------------------------------------------------
bool rl_module::next_line(str_base& out)
{
    if (!m_has_pending_line)
    {
        out.clear();
        return false;
    }

    out = m_pending_line.c_str();
    m_pending_line.clear();
    m_has_pending_line = false;
    return true;
}

//------------------------------------------------------------------------------
bool rl_module::is_showing_argmatchers()
{
    return !!s_argmatcher_color;
}

//------------------------------------------------------------------------------
void rl_module::bind_input(binder& binder)
{
    init_emacs_standard_binds();

#ifdef TIB_TODO
    const int32 default_group = binder.get_group();
    assert(default_group == 1);
    binder.bind(default_group, "\x1b[$*;*L", bind_id_left_click, true/*has_params*/);
    binder.bind(default_group, "\x1b[$*;*D", bind_id_double_click, true/*has_params*/);
    binder.bind(default_group, "\x1b[$*;*M", bind_id_drag, true/*has_params*/);
    binder.bind(default_group, "", bind_id_input);
#else
    binder.bind(binder.get_group(), "", bind_id_input);
#endif

    m_catch_group = binder.create_group("tib");
    binder.bind(m_catch_group, "", bind_id_more_input);
}

//------------------------------------------------------------------------------
void rl_module::on_begin_line(const context& context)
{
    s_build_suggestion_hint = true;

    assert(g_terminal);
    m_terminal = g_terminal;

// TODO-TIB: this seems bizarre; why did AI do this?
    auto handler = +[](int32 sig) {
        // The outer Clink loop performs tib cleanup on the main thread.
        clink_set_signaled(sig);
    };
    m_old_int = signal(SIGINT, handler);
#ifdef SIGBREAK
    m_old_break = signal(SIGBREAK, handler);
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

    // Reset the Readline fwrite/etc functions so logging changes can take
    // effect immediately.
    init_rl_terminal_thunks();

    // Note:  set_prompt() must happen while g_rl_buffer is nullptr otherwise
    // it will tell Readline about the new prompt, but Readline isn't set up
    // until rl_callback_handler_install further below.  set_prompt() happens
    // after g_terminal and g_pager are set just in case it ever needs to print
    // output with ANSI escape code support.
    assert(!g_rl_buffer);
    g_pager = &context.pager;
    set_prompt(context.prompt, context.rprompt, false/*redisplay*/);
    g_rl_buffer = &context.buffer;
    set_prev_inputline(context.buffer.get_buffer(), context.buffer.get_length());
    if (g_classify_words.get())
        s_classifications = &context.classifications;
    g_prompt_refilter = g_prompt_redisplay = 0; // Used only by diagnostic output.

#if 0
// TODO-TIB: proper integration for the full prompt.
    // Clink prints complete prompt lines; tib owns the final line so its
    // width participates in input wrapping and final cursor placement.
    const char* last_line = strrchr(m_rl_prompt.c_str(), '\n');
    if (last_line)
    {
        clink_write(m_rl_prompt.c_str(), int32(last_line + 1 - m_rl_prompt.c_str()));
    }
#endif

    s_input_color = build_color_sequence(g_color_input, m_input_color, true);
    s_selection_color = build_color_sequence(g_color_selection, m_selection_color, true);
    s_argmatcher_color = build_color_sequence(g_color_argmatcher, m_argmatcher_color, true);
    s_executable_color = build_color_sequence(g_color_executable, m_executable_color, true);
    s_command_color = build_color_sequence(g_color_cmd, m_command_color, true);
    s_alias_color = build_color_sequence(g_color_doskey, m_alias_color, true);
    s_arg_color = build_color_sequence(g_color_arg, m_arg_color, true);
    s_flag_color = build_color_sequence(g_color_flag, m_flag_color, true);
    s_unrecognized_color = build_color_sequence(g_color_unrecognized, m_unrecognized_color, true);
    s_none_color = build_color_sequence(g_color_unexpected, m_none_color, true);
    s_suggestion_color = build_color_sequence(g_color_suggestion, m_suggestion_color, true);
    s_histexpand_color = build_color_sequence(g_color_histexpand, m_histexpand_color, true);

    _rl_display_modmark_color = build_color_sequence(g_color_modmark, m_modmark_color, true);
    _rl_display_horizscroll_color = build_color_sequence(g_color_horizscroll, m_horizscroll_color, true);
    _rl_display_message_color = build_color_sequence(g_color_message, m_message_color, true);
    _rl_pager_color = build_color_sequence(g_color_interact, m_sgr_pager_color);
    _rl_hidden_color = build_color_sequence(g_color_hidden, m_sgr_hidden_color);
    _rl_readonly_color = build_color_sequence(g_color_readonly, m_sgr_readonly_color);
    _rl_command_color = build_color_sequence(g_color_cmd, m_sgr_command_color);
    _rl_alias_color = build_color_sequence(g_color_doskey, m_sgr_alias_color);
    _rl_description_color = build_color_sequence(g_color_description, m_description_color, true);
    _rl_filtered_color = build_color_sequence(g_color_filtered, m_filtered_color, true);
    _rl_arginfo_color = build_color_sequence(g_color_arginfo, m_arginfo_color, true);
    _rl_selected_color = build_color_sequence(g_color_selected, m_sgr_selected_color);

    if (!s_selection_color && s_input_color)
    {
        m_selection_color.format("%s\x1b[7m", s_input_color);
        s_selection_color = m_selection_color.c_str();
    }

    if (!s_suggestion_color)
    {
#ifdef AUTO_DETECT_CONSOLE_COLOR_THEME
        switch (get_console_theme())
        {
        case console_theme::light:
        case console_theme::dark:
            {
                static str<32> s_out;
                const uint8 faint = get_console_faint_text();
                s_out.format("\x1b[0;38;2;%u;%u;%um", faint, faint, faint);
                s_suggestion_color = s_out.c_str();
            }
            break;
        default:
            s_suggestion_color = "\x1b[0;90m";
            break;
        }
#else
        s_suggestion_color = "\x1b[0;90m";
#endif
    }

    if (!_rl_selected_color)
    {
        m_sgr_selected_color.format("0;7");
        _rl_selected_color = m_sgr_selected_color.c_str();
    }

    if (!_rl_display_message_color)
        _rl_display_message_color = "\x1b[m";

    init_display_readline();

    lock_cursor(true); // Suppress cursor flicker.
#ifdef TIB_TODO
    auto handler = [] (char* line) { rl_module::get()->done(line); };
    rl_set_rprompt(m_rl_rprompt.length() ? m_rl_rprompt.c_str() : nullptr);
    rl_callback_handler_install(m_rl_prompt.c_str(), handler);
#else
    init_prompt(m_rl_prompt, m_rl_rprompt);
    force_redisplay_readline();
    display_readline();
#endif
    lock_cursor(false);

#ifdef TIB_TODO
    // Apply the remembered history position from the previous command, if any.
    restore_sticky_search_position();
#endif

    m_done = m_has_pending_line;
    m_eof = false;

#ifdef TIB_TODO
    m_mouse.clear();
#else
    g_tib->initialize();
    g_tib->set_bindings(s_emacs_standard_bindings);
    g_tib->set_border(nullptr);
    // g_tib->set_border(&tib::c_light_border);
    g_tib->set_max_width(tib::int16_max);
    g_tib->set_max_height(tib::int16_max);
    g_tib->set_variable_height(true);

    static const char c_normal[] = "\x1b[m";
    std::shared_ptr<tib::color_table> colors = std::make_shared<tib::color_table>();
    colors->set_color(tib::color_element::base, c_normal);
    colors->set_color(tib::color_element::border, c_normal);
    colors->set_color(tib::color_element::message, fallback_color(_rl_display_message_color, c_normal));
    colors->set_color(tib::color_element::input, fallback_color(s_input_color, c_normal));
    colors->set_color(tib::color_element::input_selection, fallback_color(s_selection_color, "\x1b[0;7m"));
    colors->set_color(tib::color_element::input_mark, fallback_color(_rl_active_region_start_color, "\x1b[0;7m"));
    colors->set_color(tib::color_element::input_scroller, fallback_color(_rl_display_horizscroll_color, c_normal));
    colors->set_color(tib::color_element::suggestion, s_suggestion_color);
    g_tib->set_color_table(colors);

#if 0
    case FACE_MODMARK:      return fallback_color(_rl_display_modmark_color, c_normal);

    case FACE_HISTEXPAND1:
    case FACE_HISTEXPAND2:
        hyperlink.set(c_hyperlink);
        hyperlink.append(c_doc_histexpand);
        hyperlink.append(c_BEL);
        return fallback_color(s_histexpand_color, "\x1b[0;97;45m");

    case FACE_OTHER:        return fallback_color(s_input_color, c_normal);
    case FACE_UNRECOGNIZED: return fallback_color(s_unrecognized_color, fallback_color(s_input_color, c_normal));
    case FACE_EXECUTABLE:   return fallback_color(s_executable_color, fallback_color(s_input_color, c_normal));
    case FACE_COMMAND:      return fallback_color(s_command_color, c_normal);
    case FACE_ALIAS:        return fallback_color(s_alias_color, c_normal);
    case FACE_ARGMATCHER:   return fallback_color(s_argmatcher_color, c_normal);
    case FACE_ARGUMENT:     return fallback_color(s_arg_color, fallback_color(s_input_color, c_normal));
    case FACE_FLAG:         return fallback_color(s_flag_color, c_normal);
    case FACE_NONE:         return fallback_color(s_none_color, c_normal);
#endif

    reset_display_readline();

    m_active = true;
    m_previous_group = -1;
#endif
}

//------------------------------------------------------------------------------
void rl_module::on_end_line()
{
// TODO-TIB: ???
    if (!m_active)
        return;

    s_suggestion.clear(false/*redraw*/);

    if (!m_done)
        done(g_tib->get_text().c_str());

#ifdef DEBUG
    ignore_column_in_uninit_display_readline();
#endif

    uninit_display_readline();

// TODO-TIB: ?
    m_active = false;

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
    s_argmatcher_color = nullptr;
    s_executable_color = nullptr;
    s_command_color = nullptr;
    s_alias_color = nullptr;
    s_arg_color = nullptr;
    s_flag_color = nullptr;
    s_unrecognized_color = nullptr;
    s_none_color = nullptr;
    s_suggestion_color = nullptr;
    s_histexpand_color = nullptr;

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

    m_terminal = nullptr;

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
void rl_module::on_need_input(int32& bind_group)
{
#if 0
// TODO-TIB: ?
    if (pending_input())
    {
        if (m_previous_group < 0)
            m_previous_group = bind_group;
        bind_group = m_catch_group;
    }
    else if (m_previous_group >= 0)
    {
        bind_group = m_previous_group;
        m_previous_group = -1;
    }
#endif
}

//------------------------------------------------------------------------------
void rl_module::on_input(const input& input, result& result, const context& context)
{
    assert(!g_result);

#ifdef TIB_TODO
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
            add_to_rl_macro(input);
            return;
        }
    }
#endif

    g_result = &result;
    s_matches = &context.matches;

#ifdef TIB_TODO
    // Tell Readline about the input chord, and whether the binding resolver
    // has more bytes pending.
    struct shim_in
    {
        shim_in(const char* input, int32 len) { rl_set_clink_input(input, len); }
        ~shim_in() { rl_set_clink_input(nullptr, 0); }
    } rl_in(input.keys, input.len);

    // Call Readline's until there's no characters left.
    rollback<bool> rb_input_more(s_input_more, input.more);
    while (rl_has_clink_input() && !m_done)
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

        // Let Readline handle the next input char.
        rl_callback_read_char();

#if 0
        // Readline allows rl_undo_list to be identical to a HISTENTRY's data.
        // In certain places Readline has some special case logic to
        // compensate and avoid falling into cross-linking.  I don't
        // understand quite how/why the state doesn't cause more widespread
        // problems.  But in any case, this assertion fails in many general
        // case scenarios, so it can't be enabled.
#ifdef DEBUG
        if (rl_undo_list)
        {
            for (int32 i = 0; i <= history_length; ++i)
            {
                HIST_ENTRY* const h = history_get(i);
                if (i)
                    assert(rl_undo_list != h->data);
            }
        }
#endif
#endif

        // Using `rl.invokecommand()` inside a "luafunc:" key binding should
        // set rl_last_func to reflect the last function that was invoked.
        // However, since Readline doesn't set rl_last_func until AFTER the
        // invoked function or macro returns, setting rl_last_func won't
        // "stick" unless it's set after rl_callback_read_char() returns.
        apply_pending_lastfunc();
    }
#else
    // Expose the remaining chord to tib's self-insert lookahead before it
    // reads new bytes from Clink's driver.  Keep the optimization enabled.
    m_terminal->set_chord(input.keys, input.len);
    while (m_terminal->has_chord() && !m_done)
    {
        const int32 key = m_terminal->read();
        if (g_debug_log_input_pipeline)
        {
            LOG("INPUT rl.on_input key=%d (0x%02x '%c') has_chord=%d",
                key, key, key, m_terminal->has_chord());
        }
        m_terminal->dispatch(uint8(key));
        if (g_tib->is_done())
            done(g_tib->get_text().c_str());
    }
    m_terminal->set_chord(nullptr, 0);

    int32 group = result.set_bind_group(m_catch_group);
    on_need_input(group);
    result.set_bind_group(group);
#endif

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
    assert(!m_done);
    assert(!m_has_pending_line);

    if (m_done)
        return;

    m_pending_line = line;
    m_has_pending_line = !!line;
    m_done = true;
    m_eof = (line == nullptr);

#ifdef TIB_TODO
    rl_callback_handler_remove();
#endif
}

//------------------------------------------------------------------------------
void rl_module::on_terminal_resize(int32, int32, const context& context)
{
    signal_terminal_resized();
    resize_readline_display(context.prompt, context.buffer, m_rl_prompt.c_str(), m_rl_rprompt.c_str());
}

//------------------------------------------------------------------------------
void rl_module::on_signal(int32)
{
#ifdef TIB_TODO
    rl_callback_handler_remove();
#endif
}

//------------------------------------------------------------------------------
bool rl_module::quoted_insert_pending() const
{
    return m_terminal->quoted_insert_pending();
}

bool rl_module::pending_input() const
{
    return m_terminal->pending_input();
}

//------------------------------------------------------------------------------
void rl_module::accept_line()
{
    g_tib->set_done();
    done(g_tib->get_text().c_str());
}
