/*

    Custom display routines for the Readline prompt and input buffer,
    as well as the Clink auto-suggestions.

*/

#include "pch.h"
#include <assert.h>

#define READLINE_LIBRARY
#define BUILD_READLINE

#ifdef DEBUG
// Define REPORT_REDISPLAY to show how many times display_manager::display()
// is called and how many times update_line() skips identical lines.  To show
// the statistics, set the envvar DEBUG_REPORT_REDISPLAY=1.
#define REPORT_REDISPLAY
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
#undef REPORT_REDISPLAY
#endif

#include "display_readline.h"
#include "line_buffer.h"
#include "ellipsify.h"
#include "line_editor_integration.h"
#include "rl_integration.h"
#include "suggestionlist_impl.h"
#include "hinter.h"
#include "recognizer.h"
#include "clink_ctrlevent.h"

#include <core/base.h>
#include <core/os.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/debugheap.h>
#include <core/callstack.h>
#include <terminal/ecma48_iter.h>
#include <terminal/wcwidth.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/screen_buffer.h>
#include <terminal/scroll.h>

#include <memory>

extern "C" {

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

/* System-specific feature definitions and include files. */
#include "readline/rldefs.h"
#include "readline/rlmbutil.h"

/* Some standard library routines. */
#include "readline/readline.h"
#include "readline/history.h"
#include "readline/xmalloc.h"
#include "readline/rlprivate.h"

#if defined (COLOR_SUPPORT)
#  include "readline/colors.h"
#endif

#include "hooks.h"

extern void (*rl_fwrite_function)(FILE*, const char*, int);
extern void (*rl_fflush_function)(FILE*);

extern char* tgetstr(const char*, char**);
} // extern "C"

#ifndef HANDLE_MULTIBYTE
#error HANDLE_MULTIBYTE is required.
#endif

#ifdef WIDE_HORZ_SCROLL_MARKERS
const uint32 c_horz_scroll_indicator_chars = 2;
#else
const uint32 c_horz_scroll_indicator_chars = 1;
#endif

//------------------------------------------------------------------------------
extern "C" int32 is_CJK_codepage(UINT cp);
extern void end_task_manager();
extern int32 g_prompt_redisplay;
static uint32 s_defer_clear_lines = 0;
static uint32 s_defer_erase_extra_lines = 0;
static bool s_want_redisplay = false;
static bool s_force_redisplay = false;
static bool s_ever_input_hint = false;
static bool s_transient_prompt_context = false;
bool g_display_manager_no_comment_row = false;

//------------------------------------------------------------------------------
static setting_int g_input_rows(
    "clink.max_input_rows",
    "Maximum rows for the input line",
    "This limits how many rows the input line can use, up to the terminal height.\n"
    "When this is 0, the terminal height is the limit.",
    0);

setting_bool g_history_show_preview(
    "history.show_preview",
    "Show preview of history expansion at cursor",
    "When the text at the cursor is subject to history expansion, this shows a\n"
    "preview of the expanded result below the input line.",
    true);

static setting_bool g_rl_hide_stderr(
    "readline.hide_stderr",
    "Suppress stderr from the Readline library",
    false);

extern setting_bool g_debug_log_terminal;
extern setting_bool g_history_autoexpand;
extern setting_enum g_expand_mode;
extern setting_color g_color_comment_row;
extern setting_bool g_suggestionlist_hide_hints;
#ifdef _MSC_VER
extern setting_bool g_debug_log_output_callstacks;
#endif

//------------------------------------------------------------------------------
static bool has_erase_in_line(const char* text, uint32 len)
{
    ecma48_state state;
    ecma48_iter iter(text, state, len);
    ecma48_code::csi<32> csi;
    while (const ecma48_code& code = iter.next())
    {
        if (code.decode_csi(csi) && csi.final == 'K')
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------
static bool s_force_signaled_redisplay = false;
void force_signaled_redisplay()
{
    s_force_signaled_redisplay = true;
}

//------------------------------------------------------------------------------
static void clear_to_end_of_screen()
{
    static const char* const termcap_cd = tgetstr("cd", nullptr);
    clink_write(termcap_cd, strlen(termcap_cd));
    notify_suggestion_list_cleared();
}

//------------------------------------------------------------------------------
static void tputs(const char* s)
{
    rl_fwrite_function(_rl_out_stream, s, strlen(s));
}

//------------------------------------------------------------------------------
static void append_expand_ctrl(tib::cstring& out, const char* in, uint32 len=-1)
{
    wcwidth_iter iter(in, len);
    while (const uint32 c = iter.next())
    {
        if (iter.character_wcwidth_signed() < 0)
        {
            char sz[3] = "^?";
            if (CTRL_CHAR(c))
                sz[1] = UNCTRL(c);
            out.append(sz, 2);
        }
        else
        {
            out.append(iter.character_pointer(), iter.character_length());
        }
    }
}

//------------------------------------------------------------------------------
int32 prompt_contains_problem_codes(const char* prompt, std::vector<prompt_problem_details>* out)
{
    const char* const lf = strrchr(prompt, '\n');
    const char* const last_line = lf ? lf + 1 : prompt;

    int32 ret = 0;
    ecma48_state state;
    ecma48_iter iter(prompt, state);
    const char* begin = prompt;
    while (const ecma48_code& code = iter.next())
    {
        if (code.get_type() == ecma48_code::type_c1 &&
            code.get_code() == ecma48_code::c1_csi)
        {
            ecma48_code::csi<32> csi;
            if (code.decode_csi(csi))
            {
                int32 problem = 0;
                switch (csi.final)
                {
                case 'A':               // CUU  Cursor Up
                case 'B':               // CUD  Cursor Down
                case 'C':               // CUF  Cursor Forward
                case 'D':               // CUB  Cursor Back
                case 'E':               // CNL  Cursor Next Line
                case 'F':               // CPL  Cursor Previous Line
                case 'G':               // CHA  Cursor Horizontal Absolute
                case 'H':               // CUP  Cursor Position
                case 'd':               // VPA  Vertical Line Position Absolute
                case 'f':               // HVP  Horizontal Vertical Position
                case 's':               // SCP  Save Cursor Position
                case 'u':               // RCP  Restore Cursor Position
                    problem = BIT_PROMPT_MAYBE_PROBLEM;
                    break;
                case 'S':               // SU   Scroll Up
                case 'T':               // SD   Scroll Down
                    problem = BIT_PROMPT_PROBLEM;
                    break;
                case 'J':               // ED   Erase In Display
                case 'K':               // EL   Erase In Line
                case 'L':               // IL   Insert Line
                case 'M':               // DL   Delete Line
                case 'P':               // DCH  Delete Character
                case 'X':               // ECH  Erase Character
                    if (begin >= last_line)
                        problem = BIT_PROMPT_PROBLEM;
                    else
                        problem = BIT_PROMPT_MAYBE_PROBLEM;
                    break;
                }

                if (problem)
                {
                    ret |= problem;
                    if (!out)
                        goto done;

                    prompt_problem_details details;
                    details.type = problem;
                    details.code.concat(code.get_pointer(), code.get_length());
                    details.offset = int32(begin - prompt);
                    out->emplace_back(std::move(details));
                }
            }
        }
        else if (code.get_type() == ecma48_code::type_c0)
        {
            if (begin >= last_line)
            {
                int32 problem = 0;
                switch (code.get_code())
                {
                case '\x08':    // BS   Backspace
                case '\x09':    // HT   Tab
                case '\x0c':    // FF   Form Feed
                    problem = BIT_PROMPT_PROBLEM;
                    break;
                }

                if (problem)
                {
                    ret |= problem;
                    if (!out)
                        goto done;

                    prompt_problem_details details;
                    details.type = problem;
                    details.code.concat(code.get_pointer(), code.get_length());
                    details.offset = int32(begin - prompt);
                    out->emplace_back(std::move(details));
                }
            }
        }

        begin = iter.get_pointer();
    }

done:
    return ret;
}



//------------------------------------------------------------------------------
class measure_columns
{
public:
    enum measure_mode { print, resize };
                    measure_columns(measure_mode mode, uint32 width=0);
    void            measure(const char* text, uint32 len, bool is_prompt);
    void            measure(const char* text, bool is_prompt);
    void            apply_join_count(const measure_columns& mc);
    void            reset_column() { m_col = 0; }
    int32           get_column() const { return m_col; }
    int32           get_line_count() const { return m_line_count - m_join_count; }
    bool            get_force_wrap() const { return m_force_wrap; }
    bool            has_autowrap_at_end() const { return m_has_autowrap_at_end; }
private:
    const measure_mode m_mode;
    const uint32    m_width;
    int32           m_col = 0;
    int32           m_line_count = 1;
    int32           m_join_count = 0;
    bool            m_force_wrap = false;
    bool            m_has_autowrap_at_end = false;
};

//------------------------------------------------------------------------------
measure_columns::measure_columns(measure_mode mode, uint32 width)
: m_mode(mode)
, m_width(width ? width : _rl_screenwidth)
{
}

//------------------------------------------------------------------------------
void measure_columns::measure(const char* text, uint32 length, bool is_prompt)
{
    ecma48_state state;
    ecma48_iter iter(text, state, length);
    const char* last_lf = nullptr;
    bool wrapped = false;
    m_has_autowrap_at_end = false;
    while (const ecma48_code &code = iter.next())
    {
        switch (code.get_type())
        {
        case ecma48_code::type_chars:
            for (wcwidth_iter i(code.get_pointer(), code.get_length()); i.more();)
            {
                const uint32 c = i.next();
                assert(c != '\n');          // See ecma48_code::c0_lf below.
                assert(!CTRL_CHAR(c)); // See ecma48_code::type_c0 below.
                if (!is_prompt && i.character_wcwidth_signed() < 0)
                {
                    // Control characters.
                    goto ctrl_char;
                }
                else
                {
                    if (wrapped)
                    {
                        wrapped = false;
                        ++m_line_count;
                    }
                    int32 n = i.character_wcwidth_onectrl();
                    m_col += n;
                    if (m_col >= m_width)
                    {
                        if (is_prompt && m_mode == print && m_col == m_width)
                            wrapped = true; // Defer, for accurate measurement.
                        else
                            ++m_line_count;
                        m_col = (m_col > m_width) ? n : 0;
                    }
                }
            }
            break;

        case ecma48_code::type_c0:
            if (!is_prompt)
            {
#if defined(DISPLAY_TABS)
                if (code.get_code() != ecma48_code::c0_ht)
#endif
                {
ctrl_char:
                    assert(!is_prompt);
                    m_col += 2;
                    while (m_col >= m_width)
                    {
                        m_col -= m_width;
                        ++m_line_count;
                    }
                    break;
                }
            }
            switch (code.get_code())
            {
            case ecma48_code::c0_lf:
                last_lf = iter.get_pointer();
                ++m_line_count;
                // fall through
            case ecma48_code::c0_cr:
                m_col = 0;
                if (wrapped)
                {
                    if (m_mode == print)
                        ++m_join_count;
                    wrapped = false;
                }
                break;

            case ecma48_code::c0_ht:
#if !defined(DISPLAY_TABS)
                if (!is_prompt)
                    goto ctrl_char;
#endif
                if (wrapped)
                {
                    wrapped = false;
                    ++m_line_count;
                }
                if (int32 n = 8 - (m_col & 7))
                {
                    m_col = min<int32>(m_col + n, m_width);
                    m_col = min<int32>(m_col + n, m_width);
                    // BUGBUG:  What wrapping behavior does TAB ellicit?
                }
                break;

            case ecma48_code::c0_bs:
                // Doesn't consider full-width.
                if (m_col > 0)
                    --m_col;
                break;
            }
            break;
        }
    }

    if (wrapped)
    {
        wrapped = false;
        m_has_autowrap_at_end = true;
        ++m_line_count;
    }

    m_force_wrap = (m_col == 0 && m_line_count > 1 && last_lf != iter.get_pointer());
}

//------------------------------------------------------------------------------
void measure_columns::measure(const char* text, bool is_prompt)
{
    return measure(text, -1, is_prompt);
}

//------------------------------------------------------------------------------
void measure_columns::apply_join_count(const measure_columns& mc)
{
    m_join_count = mc.m_join_count;
}

//------------------------------------------------------------------------------
COORD measure_readline_display(const char* prompt, const char* buffer, uint32 len)
{
    measure_columns mc(measure_columns::print);

    if (prompt)
        mc.measure(prompt, true);
    if (buffer)
        mc.measure(buffer, len, false);

    COORD ret;
    ret.X = mc.get_column();
    ret.Y = mc.get_line_count();
    return ret;
}

//------------------------------------------------------------------------------
void init_display_accumulator()
{
    bool coalesce = true;

    str<> value;
#ifdef DEBUG
    if (os::get_env("DEBUG_NO_DISPLAY_ACCUMULATOR", value))
        coalesce = (atoi(value.c_str()) == 0);
    else
#endif
    if (os::get_env("CLINK_NO_DISPLAY_ACCUMULATOR", value))
        coalesce = (atoi(value.c_str()) != 0);

    tib::g_coalesce_output = coalesce;
    tib::display_accumulator::synchronize_output(terminal_has_synchronize_output());
}



//------------------------------------------------------------------------------
FILE* const thunk_null_stream = (FILE*)1;
FILE* const thunk_in_stream = (FILE*)2;
FILE* const thunk_out_stream = (FILE*)3;

//------------------------------------------------------------------------------
void terminal_fwrite_thunk(FILE* stream, const char* chars, int32 char_count)
{
    if (stream == thunk_out_stream)
    {
        assert(g_terminal);
        tib::term_out(chars, char_count);
        return;
    }

    if (stream == thunk_null_stream)
        return;

    if (stream == stderr || stream == stdout)
    {
        if (stream == stderr && g_rl_hide_stderr.get())
            return;

        tib::display_accumulator::flush();

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
void terminal_log_fwrite_thunk(FILE* stream, const char* chars, int32 char_count)
{
    suppress_implicit_write_console_logging nolog;

    if (stream == thunk_out_stream)
    {
        // Logging happens inside tib_terminal_bridge.
        tib::term_out(chars, char_count);
        return;
    }

    if (stream == thunk_null_stream)
        return;

    if (stream == stderr || stream == stdout)
    {
        if (stream == stderr && g_rl_hide_stderr.get())
            return;

        tib::display_accumulator::flush();

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
    LOGCURSORPOS(GetStdHandle(STD_OUTPUT_HANDLE));
    LOG("FWRITE \"%.*s\", %d", char_count, chars, char_count);
    fwrite(chars, char_count, 1, stream);
}

//------------------------------------------------------------------------------
void terminal_fflush_thunk(FILE* stream)
{
    static int32 s_depth = 0;
    if (stream != thunk_out_stream && stream != thunk_null_stream)
    {
        ++s_depth;
        assert(s_depth < 5);
        tib::display_accumulator::flush();
#pragma push_macro("fflush")
#undef fflush // Break out of the BUILD_READLINE cycle that defines fflush.
        fflush(stream);
#pragma pop_macro("fflush")
        --s_depth;
    }
}

//------------------------------------------------------------------------------
extern int32 terminal_getc_thunk(FILE* stream);
extern void terminal_log_read_key(int c, const char* _src);
void init_rl_terminal_thunks()
{
    const bool log = g_debug_log_terminal.get();
    rl_getc_function = terminal_getc_thunk;
    rl_log_read_key_hook = log ? terminal_log_read_key : nullptr;
    rl_fwrite_function = log ? terminal_log_fwrite_thunk : terminal_fwrite_thunk;
    rl_fflush_function = terminal_fflush_thunk;
    rl_instream = thunk_in_stream;
    rl_outstream = thunk_out_stream;
}



//------------------------------------------------------------------------------
transient_prompt_context::transient_prompt_context(bool is_transient)
: m_rollback(s_transient_prompt_context, is_transient)
{
}



//------------------------------------------------------------------------------
class display_manager
{
public:
    void                initialize() { m_initialized = true; assert(g_tib); }
    void                uninitialize() { m_initialized = false; }
    bool                is_initialized() const { return m_initialized; }
    bool                is_displayed() const { return is_initialized() && g_tib->is_displayed(); }

    void                begin_display();
    void                display();
    void                clear_comment_row();
    void                end_prompt_lf();
    void                clear_to_end_of_screen_on_next_display();

    void                set_history_expansions(history_expansion* list);
    void                force_comment_row(const char* text);
    void                measure(measure_columns& mc);

    void                on_terminal_resize();
#ifdef DEBUG
    void                ignore_column_on_uninit() { m_ignore_column_on_uninit = true; }
#endif

private:
    int32               write_with_clear(const char* text, int32 length);

    bool                m_initialized = false;

    history_expansion*  m_histexpand = nullptr;
    bool                m_clear_to_end_of_screen_on_next_display = false;
    bool                m_is_transient = false;

    bool                m_has_comment_row_text = false;
    str_moveable        m_comment_row;
    str_moveable        m_forced_comment_row;
    int32               m_forced_comment_row_cursorpos = -1;

    bool                m_modal_input = false;

#ifdef DEBUG
    bool                m_ignore_column_on_uninit = false;
#endif
};

//------------------------------------------------------------------------------
static display_manager s_display_manager;
static const bool s_autowrap_bug = tib::is_autowrap_bug_present();

//------------------------------------------------------------------------------
void display_manager::begin_display()
{
    assert(!s_defer_clear_lines);
    assert(!s_defer_erase_extra_lines);

// TODO-TIB: ?
#ifdef TIB_TODO
    m_last_prompt_line.clear();
    m_last_prompt_line_width = -1;
    m_last_prompt_line_botlin = -1;
#endif

    m_clear_to_end_of_screen_on_next_display = false;
    m_is_transient = false;

    m_has_comment_row_text = false;
    m_comment_row.clear();
    m_forced_comment_row.clear();
    m_forced_comment_row_cursorpos = -1;

    m_modal_input = false;

    g_tib->set_origin(1);
    g_tib->begin_display();

    if (!g_tib->get_length())
        history_free_expansions(&m_histexpand);
}

//------------------------------------------------------------------------------
void display_manager::end_prompt_lf()
{
    if (!m_initialized)
        return;

    // Erase comment row if present.
    clear_comment_row();

    g_tib->end_display_lf();

    // Hide suggestion list until the input line is changed by something else.
    hide_suggestion_list();
}

//------------------------------------------------------------------------------
void display_manager::clear_to_end_of_screen_on_next_display()
{
    if (!m_initialized)
        return;

    m_clear_to_end_of_screen_on_next_display = true;
}

//------------------------------------------------------------------------------
void display_manager::display()
{
    static bool s_busy = false;
    if (s_busy || !s_display_manager.is_initialized())
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

    // Terminal shell integration.  The caller doesn't have to worry about
    // redundant calls; the terminal_begin_command and terminal_end_command
    // functions internally track the state and ensure that only one begin
    // code is printed, and that an end code is only printed if a command
    // scope is currently active (begin without end yet).
    terminal_end_command();

    assert(g_terminal);

// TODO-TIB: special states.
    if (RL_ISSTATE(RL_STATE_NSEARCH|RL_STATE_READSTR))
    {
        m_modal_input = true;
        allow_suggestion_list(0);
    }
    else if (m_modal_input)
    {
        m_modal_input = false;
        allow_suggestion_list(1);
    }

#ifdef REPORT_REDISPLAY
    {
        str<> value;
        const bool report = (os::get_env("DEBUG_REPORT_REDISPLAY", value) && atoi(value.c_str()) != 0);
        tib::show_display_manager_statistics(report);
        if (report)
        {
            char stk[DEFAULT_CALLSTACK_LEN];
            format_callstack(1, 16, stk, _countof(stk), true);
            LOG("DISPLAY() CALLSTACK:");
            LOG("%s", stk);
        }
    }
#endif

// TODO-TIB: _rl_quick_redisplay could disable optimization on a single call;
// maybe tib should support that, and maybe Clink should use it?

    tib::preserve_window_horiz_scroll_position preserve(tib::is_horizpos_workaround_needed());

#ifdef DEBUG
    tib::g_can_optimize_display_lines = !dbg_get_env_int("DEBUG_NOOPT_UPDATE_LINE");
#endif

#ifdef TIB_TODO
    // Block keyboard interrupts because this function manipulates global data
    // structures.
// TODO-TIB: sigint.
    _rl_block_sigint();
    RL_SETSTATE(RL_STATE_REDISPLAYING);
#endif

    tib::display_accumulator coalesce;

    // Latch the display_manager into transient mode until the next reset.
    // This ensures the comment row cannot accidentally show up during a
    // transient prompt (e.g. from an input hint).
    if (s_transient_prompt_context)
        m_is_transient = true;

// TODO-TIB: history expansion preview.
    // Is history expansion preview desired?
    const bool can_show_comment_row = (!m_is_transient &&
                                       !g_display_manager_no_comment_row &&
                                       !RL_ISSTATE(RL_STATE_NSEARCH|RL_STATE_READSTR) &&
                                       (!is_suggestion_list_enabled() || !g_suggestionlist_hide_hints.get()));
    const bool want_histexpand_preview = (can_show_comment_row &&
                                          g_history_show_preview.get() &&
                                          g_history_autoexpand.get() &&
                                          g_expand_mode.get() > 0);

    // Max number of rows to use when displaying the input line.
    uint32 max_rows = g_input_rows.get();
    if (!max_rows)
        max_rows = 999999;
    max_rows = min<uint32>(max_rows, _rl_screenheight - (want_histexpand_preview && _rl_screenheight > 1));
    max_rows = max<uint32>(max_rows, 1);

    // FUTURE:  Maybe support defining a region in which to display the input
    // line; configurable left starting column, configurable right ending
    // column, and configurable max row count (with vertical scrolling and
    // optional scroll bar).

    const char* prompt = g_prompt.c_str();
    const char* prompt_prefix = g_prompt_prefix.c_str();

    const bool forced_display = s_force_redisplay;
    s_force_redisplay = false;

    if (m_clear_to_end_of_screen_on_next_display)
    {
        m_clear_to_end_of_screen_on_next_display = false;
        clear_to_end_of_screen();
    }

    assert(s_defer_clear_lines >= 0);
    if (s_defer_clear_lines > 0)
    {
        // Clear the lines within the display_accumulator scope.
        rl_fwrite_function(_rl_out_stream, "\r", 1);
        for (int32 lines = s_defer_clear_lines; lines--;)
        {
            tputs(tib::term_erase_to_eol());
            if (lines)
                rl_fwrite_function(_rl_out_stream, "\n", 1);
        }
        // Go back up to where the cursor was before clearing lines.
        if (s_defer_clear_lines > 1)
            tputs(tib::term_move_up(s_defer_clear_lines - 1));
        s_defer_clear_lines = 0;
    }

    // At this point, forced_display means to print a whole new prompt.  The
    // caller is responsible for positioning the cursor appropriately already.
    // The last line of the prompt is handled by tib in set_left_text().
    int32 prompt_prefix_lines = 0;
    if (prompt_prefix && *prompt_prefix && forced_display)
    {
        prompt_prefix_lines = write_with_clear(prompt_prefix, strlen(prompt_prefix));
        // TODO-TIB: handle potential wrapping of prompt string.
    }

// TODO-TIB: how to fit this into the display optimizations properly?
    // Let the application have a chance to do processing; for example to parse
    // the input line and update font faces for the line.
    if (rl_before_display_function)
        rl_before_display_function();

// TODO-TIB: modmark.
#ifdef TIB_TODO
    // Modmark.
    const bool modmark = has_modmark();

    // If someone thought that the redisplay was handled, but the currently
    // visible line has a different modification state than the one about to
    // become visible, then correct the caller's misconception.
    if (modmark != m_last_modmark)
        rl_display_fixed = 0;

// TODO-TIB: adjust the left text to include a modmark.
    if (modmark)
    {
        if (_rl_display_modmark_color)
            rl_fwrite_function(_rl_out_stream, _rl_display_modmark_color, strlen(_rl_display_modmark_color));

        rl_fwrite_function(_rl_out_stream, "*", 1);

        if (_rl_display_modmark_color)
            rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);
    }
#endif

#ifdef TIB_TODO
    // Is update needed?
    forced_display |= (m_last_prompt_line_width < 0 ||
                       modmark != m_last_modmark ||
                       !m_last_prompt_line.equals(prompt));
    bool invalidate_display = forced_display;
#endif

// TODO-TIB: this is where the wrapping is handled for the last line.
#ifdef TIB_TODO
    // Calculate ending row and column, accounting for wrapping (including
    // double width characters that don't fit).
    bool force_wrap = false;
    if (forced_display)
    {
        measure_columns mc(measure_columns::print);
        if (modmark)
            mc.measure("*", true);
        mc.measure(prompt, true);
        force_wrap = mc.get_force_wrap();
        m_last_prompt_line_width = mc.get_column();
        m_last_prompt_line_botlin = mc.get_line_count() - 1;

// TODO-TIB: print everything up to but not including the last row; the last
// row will be given to set_left_text().
// TODO-TIB: use write_with_clear() and deal properly with force_wrap.
        clink_write(prompt, mc.???());
        m_pending_wrap = force_wrap;
    }
#endif

#ifdef TIB_TODO
    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    const bool need_update = (!rl_display_fixed || forced_display || was_horz_scroll != m_horz_scroll || rl_point != m_last_point);
#endif

#ifdef TIB_TODO
        // TODO-TIB: CJK issues -- this needs to happen inside tib itself to
        // detect the actual left_text width.
        if (is_CJK_codepage(GetConsoleOutputCP()) && /* g_prompt contains any EAA width codepoints */)
        {
            COORD cursor;
            coalesce.flush();
            if (g_terminal && g_terminal->get_cursor_pos(cursor.X, cursor.Y) &&
                m_last_prompt_line_width != cursor.X)
            {
                // TODO-TIB: this minus origin defines left_text width.
                m_last_prompt_line_width = cursor.X;
            }
        }
#endif

#ifdef TIB_TODO
        dbg_ignore_scope(snapshot, "display_readline");
        m_last_prompt_line = prompt;
        m_last_modmark = modmark;
#endif

#ifdef TIB_TODO
    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
#endif

    // Maybe show input hint.
// TODO-TIB: optimize to avoid regenerating comment row when unnecessary.
    if (can_show_comment_row)
    {
        const tib::textpos_t caret = g_tib->get_caret();
        const tib::textpos_t end = g_tib->get_length();

        tib::cstring in;
        if (m_forced_comment_row_cursorpos == caret)
            in = m_forced_comment_row.c_str();
        else
        {
            m_forced_comment_row.free();
            m_forced_comment_row_cursorpos = -1;
        }

        const input_hint* hint = in.empty() ? get_input_hint() : nullptr;
        const int32 pos = hint ? hint->pos() : -1;

        if (want_histexpand_preview && in.empty())
        {
            const history_expansion* e;
            for (e = m_histexpand; e; e = e->next)
            {
                if (e->start <= caret && caret <= e->start + e->len)
                {
                    if (e->start >= pos)
                    {
                        const char* expanded = e->result;
                        if (!expanded || !*expanded)
                            expanded = "(empty)";
                        in.append("History expansion for \"");
                        append_expand_ctrl(in, g_tib->get_text().c_str() + e->start, e->len);
                        in.append("\": ");
                        append_expand_ctrl(in, expanded);
                    }
                    break;
                }
            }
        }

        if (hint || !in.empty() || s_ever_input_hint)
        {
            dbg_ignore_scope(snapshot, "display_readline");

            if (hint && in.empty())
            {
                if (m_has_comment_row_text || int32(hint->get_timeout()) <= 0)
                    in = hint->c_str();
                if (!in.empty())    // History expansion doesn't count.
                    s_ever_input_hint = true;
            }

            // To avoid recurring jitter on the bottom row, if an input hint
            // has been shown in this session before, then force reserving
            // space for the comment row even if it's blank.
            m_has_comment_row_text = false;
            if (!in.empty() || s_ever_input_hint)
            {
                tib::additional_display_line addl;
                if (!in.empty())
                {
                    str<> out;
                    const int32 limit = _rl_screenwidth - 1;
                    addl.width = ellipsify(in.c_str(), limit, out, false);
                    addl.text.append_color(g_color_comment_row.get());
                    addl.text.append(out.c_str(), out.length());
                    addl.text.append_color("");
                    m_has_comment_row_text = !!addl.width;
                }
                std::vector<tib::additional_display_line> addls;
                addls.emplace_back(std::move(addl));
                g_tib->set_additional_lines(addls);
            }
        }
    }

    // Update the display.

    const uint32 old_height = min<uint32>(_rl_screenheight, s_defer_erase_extra_lines + get_input_height());

    {
        dbg_ignore_scope(snapshot, "display_readline");
        g_tib->display();
    }

    // Erase lingering extra lines.  This handles when the number of lines
    // used by the prompt prefix shrinks (e.g. from 1 line to 0 lines).
    bool clear_suggestion_list = false;
    if (s_defer_erase_extra_lines)
    {
        assert(forced_display);
        const uint32 new_height = prompt_prefix_lines + get_input_height();
        if (old_height > new_height)
        {
            g_tib->move_to_end_of_display(true);
            clink_write("\n", 1);
#ifdef DEBUG
            const int32 dbgrow = dbg_get_env_int("DEBUG_ERASE_EXTRA_LINES");
            if (dbgrow)
            {
                dbg_printf_row(dbgrow, "old_height %u (%u), new_height %u (%u)", old_height, s_defer_erase_extra_lines, new_height, prompt_prefix_lines);
                if (dbgrow < 0)
                {
                    dbg_printf_row(dbgrow, "%s", prompt_prefix);
                }
                else
                {
                    // Note: this is printed through the display accumulator,
                    // so bugs in the erase loop below can potentially affect
                    // what row it ends up on.
                    str<16> tmp;
                    tmp.format("\x1b[s\x1b[%uH", dbgrow + 1);
                    clink_write(tmp.c_str(), tmp.length());
                    write_with_clear(prompt_prefix, strlen(prompt_prefix));
                    clink_write("\x1b[u", 3);
                }
            }
#endif
            const int32 delta = old_height - new_height;
            for (int32 lines = delta; lines--;)
            {
                clink_write(tib::term_erase_to_eol());
                if (lines)
                    clink_write("\n", 1);
            }
            if (delta > 1)
                clink_write(tib::term_move_up(delta - 1));
        }
        clear_suggestion_list = true;
        s_defer_erase_extra_lines = 0;
    }

// TODO-TIB: suggestion list.
    if (is_suggestion_list_active(false/*even_if_hidden*/))
    {
// TODO-TIB: figure out the correct conditions for clearing the existing
// suggestion list area.
#ifdef TIB_TODO
        if (invalidate_display || old_botlin != _rl_vis_botlin)
#endif
            clear_suggestion_list = true;
        update_suggestion_list_display(clear_suggestion_list);
    }

    coalesce.flush();
    coalesce.end();

// TODO-TIB: deduce scroll mode.
    init_deduce_scroll_mode();

// TODO-TIB: is this needed?
    RL_UNSETSTATE(RL_STATE_REDISPLAYING);
// TODO-TIB: sigint.
    _rl_release_sigint();
}

//------------------------------------------------------------------------------
void display_manager::set_history_expansions(history_expansion* list)
{
    if (!m_initialized)
        return;

    history_free_expansions(&m_histexpand);
    m_histexpand = list;
}

//------------------------------------------------------------------------------
void display_manager::force_comment_row(const char* text)
{
    if (!m_initialized)
        return;

    if (text && *text)
    {
        m_forced_comment_row = text;
        m_forced_comment_row_cursorpos = g_tib->get_caret();
        display();
    }
}

//------------------------------------------------------------------------------
void display_manager::measure(measure_columns& mc)
{
    assert(m_initialized);

    assert(false && "display_manager::measure was reached");
#ifdef TIB_TODO
    // FUTURE:  Ideally this would remember what prompt it displayed and use
    // that here, rather than using whatever is the current prompt content.
    const char* prompt = rl_get_local_prompt();
    const char* prompt_prefix = rl_get_local_prompt_prefix();
    assert(prompt);

    // When the OS resizes a terminal wider, the line un-wrapping logic doesn't
    // seem to know about explicit line feeds, and joins lines if the right edge
    // contains text.  So, attempt to account for that.
    if (m_curr.width() && m_curr.width() > _rl_screenwidth)
    {
        // This is a simplistic approach; it does NOT fully accurately account
        // for the difference, but it's 95% effective for 5% the cost of trying
        // to do it accurately (which still wouldn't really be accurate because
        // resizing the terminal happens asynchronously).
        measure_columns jc(measure_columns::print, m_curr.width());
        if (prompt_prefix)
            jc.measure(prompt_prefix, true);
        if (prompt)
            jc.measure(prompt, true);
        mc.apply_join_count(jc);
    }

    // Measure the prompt.
    if (prompt_prefix)
        mc.measure(prompt_prefix, true);
    if (prompt)
        mc.measure(prompt, true);

    // Measure the input buffer that was previously displayed.
    // FUTURE:  Ideally this would remember the cursor point and use that here,
    // rather than using whatever is the current cursor point.
    uint32 rows = m_last_prompt_line_botlin;
    for (uint32 i = m_top; auto d = m_curr.get(i); ++i)
    {
        if (rows++ > _rl_vis_botlin)
            break;

        // Reset the column if the first display line starts at column 0, which
        // happens when scrolling (vert or horz) is active.
        if (i == m_top && d->m_x == 0)
            mc.reset_column();

        if (rl_point < d->m_start)
            break;

        uint32 len = d->m_len;
        if (rl_point >= d->m_start && rl_point < d->m_end)
            len = d->m_lead + rl_point - d->m_start;
        mc.measure(d->m_chars, len, false);
    }
#endif
}

//------------------------------------------------------------------------------
int32 display_manager::write_with_clear(const char* text, int32 length)
{
    const char* const orig_text = text;
    int32 remaining = length;
    int32 lines = 0;
    while (remaining > 0)
    {
        bool erase_in_line = true;
        uint32 erase_length = _rl_screenwidth;

        const char* eol = strpbrk(text, "\r\n");
        length = eol ? int(eol - text) : remaining;
        if (length > 0)
        {
            measure_columns mc(measure_columns::print);
            mc.measure(text, length, true/*is_prompt*/);
            if (eol)
                lines += mc.get_line_count() - 1;
            if (!mc.get_column() && mc.get_line_count() > 1)
                erase_in_line = false;
            else
                erase_length -= mc.get_column();
            if (eol && erase_in_line && has_erase_in_line(text, length))
                erase_in_line = false;
            clink_write(text, length);
            text += length;
            remaining -= length;

            // Windows 8.1 autowrap issue:  if a line in a multiline prompt
            // reaches the right edge of the terminal and is followed by a CR
            // or LF then the cursor ends on the wrong line.  Compensate by
            // first moving the cursor up a line.
            if (eol && s_autowrap_bug && mc.has_autowrap_at_end())
                clink_write(tib::term_move_up(1));
        }

        if (eol)
        {
            ++lines;
            while (remaining > 0 && (*text == '\r' || *text == '\n'))
                ++text, --remaining;
            length = int(text - eol);
            if (erase_in_line)
                clink_write(tib::term_erase_to_eol());
            if (length > 0)
            {
                clink_write(eol, length);

                // Make sure we are at column zero even after a newline,
                // regardless of the state of terminal output processing.
                if (remaining <= 0)
                {
                    const int32 total_length = int32(eol + length - orig_text);
                    if (total_length > 0 && orig_text[total_length - 1] == '\n')
                    {
                        if (total_length < 2 || orig_text[total_length - 2] != '\r')
                            clink_write("\r", 1);
                    }
                }
            }
        }
    }

    return lines;
}

//------------------------------------------------------------------------------
void display_manager::clear_comment_row()
{
    if (!m_comment_row.empty())
    {
        m_has_comment_row_text = false;
        m_comment_row.clear();
        m_forced_comment_row.clear();
        m_forced_comment_row_cursorpos = -1;
        display();
    }
}



//------------------------------------------------------------------------------
extern "C" void clear_comment_row()
{
    s_display_manager.clear_comment_row();
}

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
// TODO-TIB: ?
void defer_clear_lines(uint32 prompt_lines, bool transient)
{
    g_tib->move_to_origin(true);
    if (prompt_lines > 0)
        clink_write(tib::term_move_up(prompt_lines));

    if (transient)
    {
        s_defer_clear_lines = prompt_lines + get_input_height() + 1;
        s_defer_erase_extra_lines = 0;
    }
    else
    {
        if (is_sparse_prompt_spacing())
            s_defer_clear_lines = max<uint32>(s_defer_clear_lines, 1);
        s_defer_erase_extra_lines = s_display_manager.is_displayed() ? prompt_lines : 0;
    }
}

//------------------------------------------------------------------------------
extern "C" void reset_display_readline(void)
{
    s_transient_prompt_context = false;
    s_display_manager.begin_display();

    // Terminal shell integration.
    terminal_end_command();
}

//------------------------------------------------------------------------------
void move_to_caret_position(bool force_column)
{
    assert(g_tib);
    if (g_tib)
        g_tib->move_to_caret_position(force_column);
}

//------------------------------------------------------------------------------
extern "C" void move_to_end_of_display(int cr)
{
    assert(g_tib);
    if (g_tib)
        g_tib->move_to_end_of_display(!!cr);
}

//------------------------------------------------------------------------------
int32 get_input_height()
{
    assert(g_tib);
    return g_tib ? g_tib->get_extent().y : 0;
}

//------------------------------------------------------------------------------
int32 get_relative_cursor_row()
{
    assert(g_tib);
    return g_tib ? g_tib->get_relative_cursor().y : 0;
}

//------------------------------------------------------------------------------
int32 get_relative_cursor_column()
{
    assert(g_tib);
    return g_tib ? g_tib->get_relative_cursor().x : 0;
}

//------------------------------------------------------------------------------
extern "C" void end_prompt_lf()
{
    s_display_manager.end_prompt_lf();
}

//------------------------------------------------------------------------------
void end_prompt(int32 crlf)
{
    allow_suggestion_list(0);
    clear_suggestion();
    clear_comment_row();

    if (crlf < 0)
    {
        end_task_manager();
        end_recognizer();
    }

    if (!is_display_readline_initialized())
        return;

    host_filter_transient_prompt(crlf);

    move_to_end_of_display(0);
    if (crlf != 0)
        end_prompt_lf();
#ifdef TIB_TODO
    if (crlf > 0)
        _rl_last_c_pos = 0;
#endif

    // Must ensure display_manager gets reset, so it doesn't try to optimize
    // away printing the next prompt.
    reset_display_readline();

    // Block any further prompt display if this is final.
    if (crlf < 0)
        uninit_display_readline();

    // Terminal shell integration.
    terminal_begin_command();
}

//------------------------------------------------------------------------------
extern "C" void rl_end_prompt(int32 crlf)
{
    end_prompt(crlf);
}

//------------------------------------------------------------------------------
extern "C" void _rl_refresh_line(void)
{
    refresh_input_line();
}

//------------------------------------------------------------------------------
void refresh_input_line()
{
    display_readline();
    g_tib->clear_auto_deactivate_mark();
}

//------------------------------------------------------------------------------
extern "C" void _rl_erase_entire_line(void)
{
    tib::term_out("\r", 1);
    tib::term_erase_to_eol();
}


//------------------------------------------------------------------------------
void fixup_prompt(str_moveable& prompt)
{
    bool need_fixup = false;
    const char* p = prompt.c_str();
    while (*p)
    {
        if (*p == RL_PROMPT_START_IGNORE || *p == RL_PROMPT_END_IGNORE)
        {
            need_fixup = true;
            break;
        }
        ++p;
    }

    if (need_fixup)
    {
        str_moveable fixup;
        fixup.concat(prompt.c_str(), int32(p - prompt.c_str()));
        while (*(++p))
        {
            if (*p != RL_PROMPT_START_IGNORE && *p != RL_PROMPT_END_IGNORE)
                fixup.concat(p, 1);
        }
        prompt = std::move(fixup);
    }
}

//------------------------------------------------------------------------------
void fixup_rprompt(str_moveable& rprompt)
{
    for (const char* p = rprompt.c_str(); *p; ++p)
    {
        if (*p == '\r' || *p == '\n')
        {
            rprompt.clear();
            return;
        }
    }

    fixup_prompt(rprompt);
}

//------------------------------------------------------------------------------
bool has_modmark()
{
#ifdef TIB_TODO
    const bool is_message = (rl_display_prompt == rl_get_message_buffer() &&
                             !RL_ISSTATE(RL_STATE_NSEARCH|RL_STATE_READSTR));
    return (!is_message && _rl_mark_modified_lines && current_history() && rl_undo_list);
#else
    return false;
#endif
}

//------------------------------------------------------------------------------
void refresh_terminal_size()
{
    assert(g_terminal);
    if (!g_terminal)
        return;

    const int32 width = g_terminal->get_columns();
    const int32 height = g_terminal->get_rows();

    if (_rl_screenheight != height || _rl_screenwidth != width)
    {
        _rl_get_screen_size(0, 0);
        if (g_debug_log_terminal.get())
            LOG("terminal size %u x %u", _rl_screenwidth, _rl_screenheight);
    }
}

//------------------------------------------------------------------------------
void clear_to_end_of_screen_on_next_display()
{
    s_display_manager.clear_to_end_of_screen_on_next_display();
}

//------------------------------------------------------------------------------
extern "C" void init_display_readline(void)
{
    s_display_manager.initialize();
}

//------------------------------------------------------------------------------
extern "C" void uninit_display_readline(void)
{
    s_display_manager.uninitialize();
}

//------------------------------------------------------------------------------
extern "C" int32 is_display_readline_initialized(void)
{
    return s_display_manager.is_initialized();
}

//------------------------------------------------------------------------------
#ifdef DEBUG
void ignore_column_in_uninit_display_readline()
{
    s_display_manager.ignore_column_on_uninit();
}
#endif

//------------------------------------------------------------------------------
void display_readline()
{
    s_display_manager.display();
    s_want_redisplay = false;
}

//------------------------------------------------------------------------------
void want_redisplay_readline()
{
    s_want_redisplay = true;
    g_tib->invalidate();
}

//------------------------------------------------------------------------------
void maybe_redisplay_readline()
{
    if (s_want_redisplay)
    {
        display_readline();
        assert(!s_want_redisplay);
    }
}

//------------------------------------------------------------------------------
void force_redisplay_readline()
{
    s_force_redisplay = true;
    s_want_redisplay = true;
    g_tib->force_redisplay();
}

//------------------------------------------------------------------------------
extern "C" int rl_reset_line_state(void)
{
// TODO-TIB: start over on the current line.
// TODO-TIB: reset left prompt text to clear any message.
    // rl_display_prompt = rl_prompt ? rl_prompt : "";
    force_redisplay_readline();
    return 0;
}


//------------------------------------------------------------------------------
extern "C" int rl_forced_update_display(void)
{
// TODO-TIB: start over on the current line.
    force_redisplay_readline();
    display_readline();
    return 0;
}

//------------------------------------------------------------------------------
void set_history_expansions(history_expansion* list)
{
    s_display_manager.set_history_expansions(list);
}

//------------------------------------------------------------------------------
void force_comment_row(const char* text)
{
    s_display_manager.force_comment_row(text);
}

//------------------------------------------------------------------------------
void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt)
{
    assert(g_terminal);

    if (!s_display_manager.is_initialized())
        return;

    // Clink tries to put the cursor on the original top row, compensating for
    // terminal wrapping behavior, and redisplay the prompt and input buffer.
    //
    // DISCLAIMER:  Windows captures various details about output it received in
    // order to improve its line wrapping behavior.  Those supplemental details
    // are not available outside conhost itself, and its wrapping algorithm is
    // complex and inconsistent, so there's no reliable way for Clink to predict
    // the actual exact wrapping that will occur.

    // Coalesce all output in this scope into a single WriteConsoleW call.
    // This avoids the vast majority of race conditions that can occur between
    // the OS async terminal resize and cursor movement while refreshing the
    // display.  The result is near-perfect resize behavior; but perfection is
    // beyond reach, due to the inherent async execution.
    tib::display_accumulator coalesce;

// TODO-TIB: update tib.
    // Update Readline's perception of the terminal dimensions.
    COORD cursor;
    const bool has_cursor = g_terminal->get_cursor_pos(cursor.X, cursor.Y);
    refresh_terminal_size();

    // Measure what was previously displayed.
    measure_columns mc(measure_columns::resize);
    s_display_manager.measure(mc);
    int32 cursor_line = mc.get_line_count() - 1;

    // WORKAROUND FOR OS ISSUE:  If the buffer ends with one trailing space and
    // the cursor is at the end of the input line, then the OS can wrap the line
    // strangely and end up inserting an extra blank line between the cursor and
    // the preceding text.  Test for a blank line above the cursor, and
    // increment cursor_line to compensate.
    if (has_cursor && cursor_line > 0 && cursor.X == 1)
    {
        const uint32 cur = buffer.get_cursor();
        const uint32 len = buffer.get_length();
        const char* buffer_ptr = buffer.get_buffer();
        if (len > 0 &&
            cur == len &&
            buffer_ptr[len - 1] == ' ' &&
            (len == 1 || buffer_ptr[len - 2] != ' '))
        {
            ++cursor_line;
        }
    }

    // Move cursor to where the top line should be.
    if (cursor_line > 0)
        clink_write(tib::term_move_up(cursor_line));
    clink_write("\r", 1);

    // Clear to end of screen.
    clear_to_end_of_screen();
    g_prompt_redisplay++;
    force_redisplay_readline();
    display_readline();
}

//------------------------------------------------------------------------------
SHORT calc_max_y_scroll_pos(SHORT y)
{
// TODO-TIB: test to make sure this is accurate (might be off by 1 or 2-ish).
    return y + (get_input_height() - g_tib->get_relative_cursor().y) + max<uint32>(s_ever_input_hint, get_suggestion_list_height());
}
