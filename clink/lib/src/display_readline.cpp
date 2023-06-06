/*

    Custom display routines for the Readline prompt and input buffer,
    as well as the Clink auto-suggestions.

*/

#include "pch.h"
#include <assert.h>

#define READLINE_LIBRARY
#define BUILD_READLINE

#include "display_readline.h"
#include "line_buffer.h"
#include "ellipsify.h"

#include <core/base.h>
#include <core/os.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/debugheap.h>
#include <terminal/ecma48_iter.h>
#include <terminal/terminal_helpers.h>

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

unsigned int cell_count(const char* in);

extern void (*rl_fwrite_function)(FILE*, const char*, int);
extern void (*rl_fflush_function)(FILE*);

extern char* tgetstr(const char*, char**);
extern char* tgoto(const char* base, int x, int y);

extern int rl_get_forced_display(void);
extern void rl_set_forced_display(int force);

extern int _rl_last_v_pos;
extern int _rl_rprompt_shown_len;

} // extern "C"

#ifdef INCLUDE_CLINK_DISPLAY_READLINE

#ifndef HANDLE_MULTIBYTE
#error HANDLE_MULTIBYTE is required.
#endif

//------------------------------------------------------------------------------
extern "C" int is_CJK_codepage(UINT cp);
extern int g_prompt_redisplay;

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

extern setting_bool g_debug_log_terminal;
extern setting_bool g_history_autoexpand;
extern setting_color g_color_comment_row;

//------------------------------------------------------------------------------
static bool s_use_eol_optimization = false;

//------------------------------------------------------------------------------
static bool is_autowrap_bug_present()
{
#pragma warning(push)
#pragma warning(disable:4996)
    OSVERSIONINFO ver = {sizeof(ver)};
    if (GetVersionEx(&ver))
        return ver.dwMajorVersion < 10;
    return false;
#pragma warning(pop)
}

//------------------------------------------------------------------------------
static void clear_to_end_of_screen()
{
    static const char* const termcap_cd = tgetstr("cd", nullptr);
    rl_fwrite_function(_rl_out_stream, termcap_cd, strlen(termcap_cd));
}

//------------------------------------------------------------------------------
static unsigned int raw_measure_cols(const char* s, unsigned int len)
{
    unsigned int cols = 0;

    str_iter iter(s, len);
    while (const int c = iter.next())
    {
        const int w = clink_wcwidth(c);
        cols += w;
    }

    return cols;
}

//------------------------------------------------------------------------------
static void tputs(const char* s)
{
    rl_fwrite_function(_rl_out_stream, s, strlen(s));
}

//------------------------------------------------------------------------------
static bool get_console_screen_buffer_info(CONSOLE_SCREEN_BUFFER_INFO* info)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    return !!GetConsoleScreenBufferInfo(h, info);
}

//------------------------------------------------------------------------------
static void append_expand_ctrl(str_base& out, const char* in, unsigned int len=-1)
{
    for (const char* walk = in; len-- && *walk; ++walk)
    {
        if (CTRL_CHAR(*walk) || *walk == RUBOUT)
        {
            char sz[3] = "^?";
            if (*walk != RUBOUT)
                sz[1] = UNCTRL(*walk);
            out.concat(sz, 2);
        }
        else
        {
            out.concat(walk, 1);
        }
    }
}

//------------------------------------------------------------------------------
int prompt_contains_problem_codes(const char* prompt, std::vector<prompt_problem_details>* out)
{
    const char* const lf = strrchr(prompt, '\n');
    const char* const last_line = lf ? lf + 1 : prompt;

    int ret = 0;
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
                int problem = 0;
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
                    details.offset = int(begin - prompt);
                    out->emplace_back(std::move(details));
                }
            }
        }
        else if (code.get_type() == ecma48_code::type_c0)
        {
            if (begin >= last_line)
            {
                int problem = 0;
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
                    details.offset = int(begin - prompt);
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
struct display_line
{
                        display_line() = default;
                        ~display_line();
                        display_line(display_line&& d);
    display_line&       operator=(display_line&& d);

    void                clear();
    void                append(char c, char face);
    void                appendspace();
    void                appendnul();

    char*               m_chars = nullptr;  // Characters in line.
    char*               m_faces = nullptr;  // Faces for characters in line.
    unsigned int        m_len = 0;          // Bytes used in m_chars and m_faces.
    unsigned int        m_allocated = 0;    // Bytes allocated in m_chars and m_faces.

    unsigned int        m_start = 0;        // Index of start in line buffer.
    unsigned int        m_end = 0;          // Index of end in line buffer.
    unsigned int        m_x = 0;            // Column at which the display line starts.
    unsigned int        m_lastcol = 0;      // Column at which the display line ends.
    unsigned int        m_lead = 0;         // Number of leading columns (e.g. wrapped part of ^X or \123).
    unsigned int        m_trail = 0;        // Number of trailing columns of spaces past m_lastcol.

    bool                m_newline = false;  // Line ends with LF.
    bool                m_toeol = false;    // Line extends to right edge of terminal (an optimization for clearing spaces).
    signed char         m_scroll_mark = 0;  // Number of columns for scrolling indicator (positive at left, negative at right).

private:
    void                appendinternal(char c, char face);
};

//------------------------------------------------------------------------------
display_line::~display_line()
{
    free(m_chars);
    free(m_faces);
}

//------------------------------------------------------------------------------
display_line::display_line(display_line&& d)
{
    memcpy(this, &d, sizeof(d));
    memset(&d, 0, sizeof(d));
}

//------------------------------------------------------------------------------
display_line& display_line::operator=(display_line&& d)
{
    memcpy(this, &d, sizeof(d));
    memset(&d, 0, sizeof(d));
    return *this;
}

//------------------------------------------------------------------------------
void display_line::clear()
{
    m_len = 0;

    m_start = 0;
    m_end = 0;
    m_x = 0;
    m_lastcol = 0;
    m_lead = 0;
    m_trail = 0;

    m_newline = false;
    m_toeol = false;
    m_scroll_mark = 0;
}

//------------------------------------------------------------------------------
void display_line::appendinternal(char c, char face)
{
    if (m_len >= m_allocated)
    {
#ifdef DEBUG
        const unsigned int min_alloc = 40;
#else
        const unsigned int min_alloc = 160;
#endif

        const unsigned int alloc = max<unsigned int>(min_alloc, m_allocated * 3 / 2);
        char* chars = static_cast<char*>(realloc(m_chars, alloc));
        char* faces = static_cast<char*>(realloc(m_faces, alloc));
        if (!chars || !faces)
        {
            free(chars);
            free(faces);
            return;
        }

        m_chars = chars;
        m_faces = faces;
        m_allocated = alloc;
    }

    m_chars[m_len] = c;
    m_faces[m_len] = face;
    ++m_len;
}

//------------------------------------------------------------------------------
void display_line::append(char c, char face)
{
    assert(!c || !m_trail);
    appendinternal(c, face);
}

//------------------------------------------------------------------------------
void display_line::appendspace()
{
    appendinternal(' ', FACE_NORMAL);
    m_trail++;
}

//------------------------------------------------------------------------------
void display_line::appendnul()
{
    appendinternal(0, 0);
    --m_len;
}



//------------------------------------------------------------------------------
class display_lines
{
public:
                        display_lines() = default;
                        ~display_lines() = default;

    void                parse(unsigned int prompt_botlin, unsigned int col, const char* buffer, unsigned int len);
    void                horz_parse(unsigned int prompt_botlin, unsigned int col, const char* buffer, unsigned int point, unsigned int len, const display_lines& ref);
    void                apply_scroll_markers(unsigned int top, unsigned int bottom);
    void                set_top(unsigned int top);
    void                set_comment_row(str_moveable&& s);
    void                clear_comment_row();
    void                swap(display_lines& d);
    void                clear();

    const display_line* get(unsigned int index) const;
    unsigned int        count() const;
    unsigned int        width() const;
    bool                can_show_rprompt() const;
    bool                is_horz_scrolled() const;
    const char*         comment_row() const;

    unsigned int        vpos() const { return m_vpos; }
    unsigned int        cpos() const { return m_cpos; }
    unsigned int        top() const { return m_top; }

    void                shift_CJK_cursor(int cpos);

private:
    display_line*       next_line(unsigned int start);
    bool                adjust_columns(unsigned int& point, int delta, const char* buffer, unsigned int len) const;

    std::vector<display_line> m_lines;
    unsigned int        m_width = 0;
    unsigned int        m_count = 0;
    unsigned int        m_prompt_botlin;
    unsigned int        m_vpos = 0;
    unsigned int        m_cpos = 0;
    unsigned int        m_top = 0;
    unsigned int        m_horz_start = 0;
    bool                m_horz_scroll = false;
    str_moveable        m_comment_row;
};

//------------------------------------------------------------------------------
void display_lines::parse(unsigned int prompt_botlin, unsigned int col, const char* buffer, unsigned int len)
{
    assert(col < _rl_screenwidth);
    dbg_ignore_scope(snapshot, "display_readline");

    clear();
    m_width = _rl_screenwidth;

    m_prompt_botlin = prompt_botlin;
    while (prompt_botlin--)
        next_line(0);

    display_line* d = next_line(0);
    d->m_x = col;
    m_cpos = col;

    int hl_begin = -1;
    int hl_end = -1;

    if (rl_mark_active_p())
    {
        if (rl_point >= 0 && rl_point <= rl_end && rl_mark >= 0 && rl_mark <= rl_end)
        {
            hl_begin = (rl_mark < rl_point) ? rl_mark : rl_point;
            hl_end = (rl_mark < rl_point) ? rl_point : rl_mark;
        }
    }

    str<16> tmp;
    unsigned int index = 0;

    const char* chars = buffer;
    const char* end = nullptr;
    str_iter iter(chars, len);
    for (; true; chars = end)
    {
        const int c = iter.next();
        if (!c)
            break;

        end = iter.get_pointer();

        if (c == '\n' && !_rl_horizontal_scroll_mode && _rl_term_up && *_rl_term_up)
        {
            d->m_lastcol = col;
            d->m_end = static_cast<unsigned int>(index);
            d->appendnul();
            d->m_newline = true;

            if (index == rl_point)
            {
                m_vpos = m_count - 1;
                m_cpos = col;
            }

            ++index;
            d = next_line(index);
            col = 0;
            continue;
        }
#ifdef DISPLAY_TABS
        else if (c == '\t')
        {
            // Display as spaces to the next tab stop.
            unsigned int target = ((col | 7) + 1) - col;
            for (tmp.clear(); target--;)
                tmp.concat(" ", 1);
        }
#endif
        else if (CTRL_CHAR(*chars) || *chars == RUBOUT)
        {
            // Display control characters as ^X.
            assert(end == chars + 1);
            tmp.clear();
            tmp.format("^%c", CTRL_CHAR(*chars) ? UNCTRL(*chars) : '?');
        }
        else
        {
            const unsigned int chars_len = static_cast<unsigned int>(end - chars);
            const unsigned int wc_width = clink_wcwidth(c);

            if (col + wc_width > _rl_screenwidth)
            {
                d->m_lastcol = col;
                d->m_end = static_cast<unsigned int>(chars - buffer);

                while (col < _rl_screenwidth)
                {
                    d->appendspace();
                    ++col;
                }
                d->appendnul();

                assert(d->m_lead <= d->m_lastcol);
                assert(d->m_lastcol + d->m_trail == _rl_screenwidth);

                d = next_line(static_cast<unsigned int>(chars - buffer));
                col = 0;
            }

            if (index <= rl_point && rl_point < index + chars_len)
            {
                m_vpos = m_count - 1;
                m_cpos = col;
            }

            for (; chars < end; ++chars, ++index)
                d->append(*chars, rl_get_face_func(index, hl_begin, hl_end));
            col += wc_width;
            continue;
        }

        bool wrapped = false;
        const char* add = tmp.c_str();
        const char face = rl_get_face_func(static_cast<unsigned int>(chars - buffer), hl_begin, hl_end);

        if (index == rl_point)
        {
            m_vpos = m_count - 1;
            m_cpos = col;
        }

        index = static_cast<unsigned int>(end - buffer);

        for (unsigned int x = tmp.length(); x--; ++add)
        {
            if (col >= _rl_screenwidth)
            {
                d->m_lastcol = col;
                d->m_end = index;
                d->appendnul();

                assert(d->m_lead <= d->m_lastcol);
                assert(d->m_lastcol == _rl_screenwidth);
                assert(d->m_trail == 0);

                wrapped = true;
                d = next_line(index);
                col = 0;

                // Only update the cursor position if the beginning of the text
                // wraps to the next line.
                if (add == tmp.c_str() && index == rl_point)
                {
                    m_vpos = m_count - 1;
                    m_cpos = col;
                }
            }

            assert(*add >= 0 && *add <= 0x7f); // Only ASCII characters are generated.
            d->append(*add, face);
            ++col;
        }

        if (wrapped)
            d->m_lead = col;
    }

    assert(static_cast<unsigned int>(iter.get_pointer() - buffer) == index);

    d->m_lastcol = col;
    d->m_end = index;
    d->appendnul();

    if (d->m_lastcol + d->m_trail >= _rl_screenwidth)
    {
        assert(d->m_lead <= d->m_lastcol);
        assert(d->m_lastcol == _rl_screenwidth);
        assert(d->m_trail == 0);

        d = next_line(index);
        d->m_end = index;
        col = 0;
    }

    if (index == rl_point)
    {
        m_vpos = m_count - 1;
        m_cpos = col;
    }
}

//------------------------------------------------------------------------------
void display_lines::horz_parse(unsigned int prompt_botlin, unsigned int col, const char* buffer, unsigned int point, unsigned int len, const display_lines& ref)
{
    assert(col < _rl_screenwidth);
    dbg_ignore_scope(snapshot, "display_readline");

    clear();
    m_width = _rl_screenwidth;
    m_horz_start = ref.m_horz_start;

    m_prompt_botlin = prompt_botlin;
    while (prompt_botlin--)
        next_line(0);

    const int scroll_stride = _rl_screenwidth / 3;
    const int limit = _rl_screenwidth - 2; // -1 for `>` marker, -1 for space.

    // Adjust horizontal scroll offset to ensure point is visible.
    if (point < m_horz_start)
    {
        m_horz_start = point;
        adjust_columns(m_horz_start, 0 - scroll_stride, buffer, len);
    }
    else
    {
        const int range = limit - (m_horz_start ? 1 : col);
        unsigned int end = m_horz_start;
        if (adjust_columns(end, range, buffer, len) && point >= end)
        {
            m_horz_start = point;
            if (!adjust_columns(m_horz_start, 0 - scroll_stride*2, buffer, len))
                m_horz_start++;
        }
    }

    display_line* d = next_line(0);
    d->m_start = m_horz_start;
    m_horz_scroll = true;

    if (m_horz_start)
    {
        d->m_x = 0;
        d->m_lead = 1;
        d->append('<', FACE_SCROLL);
        d->appendnul();
        col = 1;
    }
    else
    {
        d->m_x = col;
    }
    m_vpos = m_prompt_botlin;
    m_cpos = col;

    int hl_begin = -1;
    int hl_end = -1;

    if (rl_mark_active_p())
    {
        if (rl_point >= 0 && rl_point <= rl_end && rl_mark >= 0 && rl_mark <= rl_end)
        {
            hl_begin = (rl_mark < rl_point) ? rl_mark : rl_point;
            hl_end = (rl_mark < rl_point) ? rl_point : rl_mark;
        }
    }

    str<16> tmp;
    unsigned int index = m_horz_start;

    bool overflow = false;
    const char* chars = buffer + m_horz_start;
    const char* end = nullptr;
    str_iter iter(chars, len - m_horz_start);
    for (; true; chars = end)
    {
        const int c = iter.next();
        if (!c)
            break;

        end = iter.get_pointer();

        if (CTRL_CHAR(*chars) || *chars == RUBOUT)
        {
            // Display control characters as ^X.
            assert(end == chars + 1);
            tmp.clear();
            tmp.format("^%c", CTRL_CHAR(*chars) ? UNCTRL(*chars) : '?');
        }
        else
        {
            const unsigned int chars_len = static_cast<unsigned int>(end - chars);
            const unsigned int wc_width = clink_wcwidth(c);

            if (col + wc_width > limit)
            {
                overflow = true;
                break;
            }

            if (index <= rl_point && rl_point < index + chars_len)
                m_cpos = col;

            for (; chars < end; ++chars, ++index)
                d->append(*chars, rl_get_face_func(index, hl_begin, hl_end));
            col += wc_width;
            continue;
        }

        const char* add = tmp.c_str();
        const char face = rl_get_face_func(static_cast<unsigned int>(chars - buffer), hl_begin, hl_end);

        if (index == rl_point)
            m_cpos = col;

        index = static_cast<unsigned int>(end - buffer);

        for (unsigned int x = tmp.length(); x--; ++add)
        {
            if (col >= limit)
                break;

            assert(*add >= 0 && *add <= 0x7f); // Only ASCII characters are generated.
            d->append(*add, face);
            ++col;
        }

        if (col >= limit)
            break;
    }

    d->m_lastcol = col;
    d->m_end = static_cast<unsigned int>(chars - buffer);

    if (iter.more() || overflow)
    {
        d->append('>', FACE_SCROLL);
        d->m_lastcol++;
        d->m_toeol = false;
    }

    d->appendnul();

    if (index == rl_point)
    {
        m_vpos = m_count - 1;
        m_cpos = col;
    }
}

//------------------------------------------------------------------------------
void display_lines::apply_scroll_markers(unsigned int top, unsigned int bottom)
{
    assert(top >= m_prompt_botlin);
    assert(top <= bottom);
    assert(top < m_count);

    int c;

    if (top > m_prompt_botlin)
    {
        display_line& d = m_lines[top];

        if (!d.m_len)
        {
            d.append('<', FACE_SCROLL);
            d.appendnul();
        }
        else
        {
            str_iter iter_top(d.m_chars, d.m_len);
            c = iter_top.next();
            while (c)
            {
                int wc = clink_wcwidth(c);
                if (!wc)
                {
                    c = iter_top.next();
                    continue;
                }

                int bytes = static_cast<int>(iter_top.get_pointer() - d.m_chars);
                assert(bytes >= wc);
                if (bytes < wc)
                    break;

                unsigned int i = 0;
                d.m_chars[i] = '<';
                d.m_faces[i] = FACE_SCROLL;
                bytes--;
                i++;
                d.m_scroll_mark = 1;
                if (bytes > 0)
                {
                    memmove(d.m_chars + i, d.m_chars + i + bytes, d.m_len - (i + bytes));
                    memmove(d.m_faces + i, d.m_faces + i + bytes, d.m_len - (i + bytes));
                    d.m_len -= bytes;
                }
                while (--wc > 0)
                    d.appendspace();
                d.appendnul();
                break;
            }
        }
    }

    if (bottom + 1 < m_count)
    {
        // The approach here doesn't support horizontal scroll mode.
        assert(top != bottom);

        display_line& d = m_lines[bottom];

        if (d.m_lastcol - d.m_x > 2)
        {
            d.m_len -= d.m_trail;
            while (d.m_x + d.m_lastcol + 1 >= _rl_screenwidth)
            {
                const int bytes = _rl_find_prev_mbchar(d.m_chars, d.m_len, MB_FIND_NONZERO);
                d.m_lastcol -= raw_measure_cols(d.m_chars + bytes, d.m_len - bytes);
                d.m_len = bytes;
            }

            while (d.m_x + d.m_lastcol + 2 < _rl_screenwidth)
            {
                d.append(' ', FACE_NORMAL);
                d.m_lastcol++;
            }
            d.append('>', FACE_SCROLL);
            d.m_scroll_mark = -1;
            d.m_lastcol++;
            d.m_toeol = false;
            d.appendnul();
        }
    }
}

//------------------------------------------------------------------------------
void display_lines::set_top(unsigned int top)
{
    m_top = top;
}

//------------------------------------------------------------------------------
void display_lines::set_comment_row(str_moveable&& s)
{
    m_comment_row = std::move(s);
}

//------------------------------------------------------------------------------
void display_lines::clear_comment_row()
{
    m_comment_row.clear();
}

//------------------------------------------------------------------------------
void display_lines::swap(display_lines& d)
{
    m_lines.swap(d.m_lines);
    std::swap(m_width, d.m_width);
    std::swap(m_count, d.m_count);
    std::swap(m_prompt_botlin, d.m_prompt_botlin);
    std::swap(m_vpos, d.m_vpos);
    std::swap(m_cpos, d.m_cpos);
    std::swap(m_top, d.m_top);
    std::swap(m_horz_start, d.m_horz_start);
    std::swap(m_horz_scroll, d.m_horz_scroll);
    std::swap(m_comment_row, d.m_comment_row);
}

//------------------------------------------------------------------------------
void display_lines::clear()
{
    for (unsigned int i = m_count; i--;)
        m_lines[i].clear();
    m_width = 0;
    m_count = 0;
    m_prompt_botlin = 0;
    m_vpos = 0;
    m_cpos = 0;
    m_top = 0;
    m_horz_start = 0;
    m_horz_scroll = false;
    m_comment_row.clear();
}

//------------------------------------------------------------------------------
const display_line* display_lines::get(unsigned int index) const
{
    if (index >= m_count)
        return nullptr;
    return &m_lines[index];
}

//------------------------------------------------------------------------------
unsigned int display_lines::count() const
{
    return m_count;
}

//------------------------------------------------------------------------------
unsigned int display_lines::width() const
{
    return m_width;
}

//------------------------------------------------------------------------------
bool display_lines::can_show_rprompt() const
{
    return (rl_rprompt &&                             // has rprompt
            (_rl_term_forward_char || _rl_term_ch) && // has termcap
            rl_display_prompt == rl_prompt &&         // displaying the real prompt
            m_count == 1 &&                           // only one line
            (m_lines[0].m_lastcol + 1 + rl_visible_rprompt_length < _rl_screenwidth)); // fits
}

//------------------------------------------------------------------------------
bool display_lines::is_horz_scrolled() const
{
    return (m_horz_scroll && m_horz_start > 0);
}

//------------------------------------------------------------------------------
const char* display_lines::comment_row() const
{
    return m_comment_row.c_str();
}

//------------------------------------------------------------------------------
display_line* display_lines::next_line(unsigned int start)
{
    assert(!m_horz_scroll);

    if (m_count >= m_lines.size())
    {
        m_lines.emplace_back();
        m_lines.back().m_toeol = (m_width == _rl_screenwidth);
    }

    display_line* d = &m_lines[m_count++];
    assert(!d->m_x);
    assert(!d->m_len);
    d->m_start = start;
    d->m_toeol = (m_width == _rl_screenwidth);
    return d;
}

//------------------------------------------------------------------------------
bool display_lines::adjust_columns(unsigned int& index, int delta, const char* buffer, unsigned int len) const
{
    assert(delta != 0);
    assert(len >= index);

    bool first = true;

    if (delta < 0)
    {
        const char* walk = buffer + index;
        delta *= -1;
        while (delta > 0)
        {
            if (!index)
                return false;
            const int i = _rl_find_prev_mbchar(const_cast<char*>(buffer), index, MB_FIND_NONZERO);
            const int bytes = index - i;
            walk -= bytes;
            const int width = (bytes == 1 && (CTRL_CHAR(*walk) || *walk == RUBOUT)) ? 2 : raw_measure_cols(walk, bytes);
            if (first || delta >= width)
                index -= bytes;
            first = false;
            delta -= width;
        }
    }
    else
    {
        str_iter iter(buffer + index, len - index);
        while (delta > 0)
        {
            const char* prev = iter.get_pointer();
            const int c = iter.next();
            if (!c)
                return false;
            const int bytes = static_cast<int>(iter.get_pointer() - prev);
            const int width = ((c >= 0 && c <= 0x1f) || c == RUBOUT) ? 2 : clink_wcwidth(c);
            if (first || delta >= width)
                index += bytes;
            first = false;
            delta -= width;
        }
    }

    return index;
}



//------------------------------------------------------------------------------
class measure_columns
{
public:
    enum measure_mode { print, resize };
                    measure_columns(measure_mode mode, unsigned int width=0);
    void            measure(const char* text, unsigned int len, bool is_prompt);
    void            measure(const char* text, bool is_prompt);
    void            apply_join_count(const measure_columns& mc);
    void            reset_column() { m_col = 0; }
    int             get_column() const { return m_col; }
    int             get_line_count() const { return m_line_count - m_join_count; }
    bool            get_force_wrap() const { return m_force_wrap; }
private:
    const measure_mode m_mode;
    const unsigned int m_width;
    int             m_col = 0;
    int             m_line_count = 1;
    int             m_join_count = 0;
    bool            m_force_wrap = false;
};

//------------------------------------------------------------------------------
measure_columns::measure_columns(measure_mode mode, unsigned int width)
: m_mode(mode)
, m_width(width ? width : _rl_screenwidth)
{
}

//------------------------------------------------------------------------------
void measure_columns::measure(const char* text, unsigned int length, bool is_prompt)
{
    ecma48_state state;
    ecma48_iter iter(text, state, length);
    const char* last_lf = nullptr;
    bool wrapped = false;
    while (const ecma48_code &code = iter.next())
    {
        switch (code.get_type())
        {
        case ecma48_code::type_chars:
            for (str_iter i(code.get_pointer(), code.get_length()); i.more();)
            {
                const char *chars = code.get_pointer();
                const int c = i.next();
                assert(c != '\n');          // See ecma48_code::c0_lf below.
                assert(!CTRL_CHAR(*chars)); // See ecma48_code::type_c0 below.
                if (!is_prompt && (CTRL_CHAR(*chars) || *chars == RUBOUT))
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
                    int n = clink_wcwidth(c);
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
                if (int n = 8 - (m_col & 7))
                {
                    m_col = min<int>(m_col + n, m_width);
                    m_col = min<int>(m_col + n, m_width);
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
int display_accumulator::s_nested = 0;
static str_moveable s_buf;

//------------------------------------------------------------------------------
display_accumulator::display_accumulator()
{
    assert(!m_saved_fwrite);
    assert(!m_saved_fflush);
    assert(rl_fwrite_function);
    assert(rl_fflush_function);
    assert(s_nested || s_buf.empty());
    m_saved_fwrite = rl_fwrite_function;
    m_saved_fflush = rl_fflush_function;
    m_active = true;
    rl_fwrite_function = fwrite_proc;
    rl_fflush_function = fflush_proc;
    ++s_nested;
}

//------------------------------------------------------------------------------
display_accumulator::~display_accumulator()
{
    if (m_active)
    {
        if (s_nested > 1)
            restore();
        else
            flush();
        assert(!m_active);
    }
    --s_nested;
}

//------------------------------------------------------------------------------
void display_accumulator::split()
{
    if (m_active && s_buf.length())
    {
        m_saved_fwrite(_rl_out_stream, s_buf.c_str(), s_buf.length());
        m_saved_fflush(_rl_out_stream);
        s_buf.clear();
    }
}

//------------------------------------------------------------------------------
void display_accumulator::flush()
{
    if (s_nested > 1)
        return;

    restore();

    if (s_buf.length())
    {
        rl_fwrite_function(_rl_out_stream, s_buf.c_str(), s_buf.length());
        rl_fflush_function(_rl_out_stream);
        s_buf.clear();
    }
}

//------------------------------------------------------------------------------
void display_accumulator::restore()
{
    assert(m_active);
    assert(m_saved_fwrite);
    assert(m_saved_fflush);
    rl_fwrite_function = m_saved_fwrite;
    rl_fflush_function = m_saved_fflush;
    m_saved_fwrite = nullptr;
    m_saved_fflush = nullptr;
    m_active = false;
}

//------------------------------------------------------------------------------
void display_accumulator::fwrite_proc(FILE* out, const char* text, int len)
{
    assert(out == _rl_out_stream);
    dbg_ignore_scope(snapshot, "display_readline");
    s_buf.concat(text, len);
}

//------------------------------------------------------------------------------
void display_accumulator::fflush_proc(FILE*)
{
    // No-op, since the destructor automatically flushes.
}



//------------------------------------------------------------------------------
class display_manager
{
public:
                        display_manager();
    void                clear();
    unsigned int        top_offset() const;
    unsigned int        top_buffer_start() const;
    void                on_new_line();
    void                end_prompt_lf();
    void                display();
    void                set_history_expansions(history_expansion* list=nullptr);
    void                measure(measure_columns& mc);

private:
    void                update_line(int i, const display_line* o, const display_line* d, bool has_rprompt);
    void                move_to_column(unsigned int col, bool force=false);
    void                move_to_row(int row);
    void                shift_cols(unsigned int col, int delta);
    void                print(const char* chars, unsigned int len);
    void                print_rprompt(const char* s);
    void                detect_pending_wrap();
    void                finish_pending_wrap();

    display_lines       m_next;
    display_lines       m_curr;
    history_expansion*  m_histexpand = nullptr;
    unsigned int        m_top = 0;      // Vertical scrolling; index to top displayed line.
    str_moveable        m_last_prompt_line;
    int                 m_last_prompt_line_width = -1;
    int                 m_last_prompt_line_botlin = -1;
    bool                m_last_modmark = false;
    bool                m_horz_scroll = false;

    const bool          m_autowrap_bug;
    bool                m_pending_wrap = false;
    const display_lines* m_pending_wrap_display = nullptr;
};

//------------------------------------------------------------------------------
static display_manager s_display_manager;

//------------------------------------------------------------------------------
display_manager::display_manager()
: m_autowrap_bug(is_autowrap_bug_present())
{
    rl_on_new_line();
}

//------------------------------------------------------------------------------
void display_manager::clear()
{
    m_next.clear();
    m_curr.clear();
    // m_histexpand is only cleared in on_new_line().
    // m_top is only cleared in on_new_line().
    m_last_prompt_line.clear();
    m_last_prompt_line_width = -1;
    m_last_prompt_line_botlin = -1;
    m_last_modmark = false;
    m_horz_scroll = false;

    m_pending_wrap = false;
    m_pending_wrap_display = nullptr;
}

//------------------------------------------------------------------------------
unsigned int display_manager::top_offset() const
{
    if (m_last_prompt_line_botlin < 0)
        return 0;
    assert(m_top >= m_last_prompt_line_botlin);
    return m_top - m_last_prompt_line_botlin;
}

//------------------------------------------------------------------------------
unsigned int display_manager::top_buffer_start() const
{
    const display_line* d = m_curr.get(m_top);
    assert(d);
    return d ? d->m_start : 0;
}

//------------------------------------------------------------------------------
void display_manager::on_new_line()
{
    clear();
    if (!rl_end)
    {
        m_top = 0;
        history_free_expansions(&m_histexpand);
    }

    str<> env;
    s_use_eol_optimization = (os::get_env("CLINK_USE_EOL_OPTIMIZATION", env) && atoi(env.c_str()));
}

//------------------------------------------------------------------------------
void display_manager::end_prompt_lf()
{
    // FUTURE: When in a scrolling mode (vert or horz), reprint the entire
    // prompt and input line without the scroll constraints?

    // Erase comment row if present.
    if (m_curr.comment_row())
    {
        _rl_move_vert(_rl_vis_botlin + 1);
        _rl_cr();
        _rl_last_c_pos = 0;
        _rl_clear_to_eol(0);
        m_curr.clear_comment_row();
    }

    // If the cursor is the only thing on an otherwise-blank last line,
    // compensate so we don't print an extra CRLF.
    bool unwrap = false;
    const unsigned int count = m_curr.count();
    if (_rl_vis_botlin &&
        m_top - m_last_prompt_line_botlin + _rl_vis_botlin + 1 == count &&
        m_curr.get(count - 1)->m_len == 0)
    {
        _rl_vis_botlin--;
        unwrap = true;
    }
    _rl_move_vert(_rl_vis_botlin);

    // If we've wrapped lines, remove the final xterm line-wrap flag.
    // BUGBUG:  The Windows console is not smart enough to recognize that this
    // means it should not merge the line and the next line when resizing the
    // terminal width.  But, Windows Terminal gets the line breaks correct when
    // copy/pasting.  Let's call it a win.
    if (unwrap && _rl_term_autowrap)
    {
        const display_line* d = m_curr.get(count - 2);
        if (d && d->m_chars &&
            _rl_vis_botlin - m_last_prompt_line_botlin >= 2 &&
            d->m_lastcol + d->m_trail == _rl_screenwidth)
        {
            // Reprint end of the previous row if at least 2 rows are visible.
            const int index = _rl_find_prev_mbchar(d->m_chars, d->m_len, MB_FIND_NONZERO);
            const unsigned int len = d->m_len - index;
            const unsigned int wc = raw_measure_cols(d->m_chars + index, len);
            move_to_column(_rl_screenwidth - wc);
            _rl_clear_to_eol(0);
            rl_puts_face_func(d->m_chars + index, d->m_faces + index, len);
        }
        else if (m_top == m_last_prompt_line_botlin)
        {
            assert(count <= 1);
            // When there is no previous row (input line is empty but starts at
            // col 0), reprint the last prompt line to clear the line-wrap flag.
            _rl_move_vert(0);
            clear_to_end_of_screen();
            rl_fwrite_function(_rl_out_stream, m_last_prompt_line.c_str(), m_last_prompt_line.length());
            _rl_vis_botlin = m_last_prompt_line_botlin;
        }
        else
        {
            // Degenerate case; just give up.
        }
    }

    // Print CRLF to end the prompt.
    rl_crlf();
    _rl_last_c_pos = 0;
    rl_fflush_function(_rl_out_stream);
    rl_display_fixed++;
}

//------------------------------------------------------------------------------
void display_manager::display()
{
    if (!_rl_echoing_p)
        return;

    // NOTE:  This implementation doesn't use _rl_quick_redisplay.  I'm not
    // clear on what practical benefit it would provide, or why it would be
    // worth adding that complexity.

    // Block keyboard interrupts because this function manipulates global data
    // structures.
    _rl_block_sigint();
    RL_SETSTATE(RL_STATE_REDISPLAYING);

#ifndef LOG_OUTPUT_CALLSTACKS
    display_accumulator coalesce;
#endif

    m_pending_wrap = false;

    if (!rl_display_prompt)
    {
        // This assignment technically isn't safe, but Readline does it.
        rl_display_prompt = const_cast<char *>("");
    }

    // Is history expansion preview desired?
    const bool want_histexpand_preview = (g_history_show_preview.get() && g_history_autoexpand.get());

    // Max number of rows to use when displaying the input line.
    unsigned int max_rows = g_input_rows.get();
    if (!max_rows)
        max_rows = 999999;
    max_rows = min<unsigned int>(max_rows, _rl_screenheight - (want_histexpand_preview && _rl_screenheight > 1));
    max_rows = max<unsigned int>(max_rows, 1);

    // FUTURE:  Maybe support defining a region in which to display the input
    // line; configurable left starting column, configurable right ending
    // column, and configurable max row count (with vertical scrolling and
    // optional scroll bar).

    const char* prompt = rl_get_local_prompt();
    const char* prompt_prefix = rl_get_local_prompt_prefix();

    bool forced_display = rl_get_forced_display();
    rl_set_forced_display(false);

    if (prompt || rl_display_prompt == rl_prompt)
    {
        if (prompt_prefix && forced_display)
            rl_fwrite_function(_rl_out_stream, prompt_prefix, strlen(prompt_prefix));
    }
    else
    {
        int pmtlen;
        prompt = strrchr(rl_display_prompt, '\n');
        if (!prompt)
            prompt = rl_display_prompt;
        else
        {
            assert(!rl_get_message_buffer());
            prompt++;
            pmtlen = static_cast<int>(prompt - rl_display_prompt);
            if (forced_display)
            {
                rl_fwrite_function(_rl_out_stream, rl_display_prompt, pmtlen);
                // Make sure we are at column zero even after a newline,
                // regardless of the state of terminal output processing.
                if (pmtlen < 2 || prompt[-2] != '\r')
                    _rl_cr();
            }
        }
    }

    if (!prompt)
        prompt = "";

    // Let the application have a chance to do processing; for example to parse
    // the input line and update font faces for the line.
    if (rl_before_display_function)
        rl_before_display_function();

    // Modmark.
    const bool is_message = (rl_display_prompt == rl_get_message_buffer());
    const bool modmark = (!is_message && _rl_mark_modified_lines && current_history() && rl_undo_list);

    // If someone thought that the redisplay was handled, but the currently
    // visible line has a different modification state than the one about to
    // become visible, then correct the caller's misconception.
    if (modmark != m_last_modmark)
        rl_display_fixed = 0;

    // Is update needed?
    forced_display |= (m_last_prompt_line_width < 0 ||
                       modmark != m_last_modmark ||
                       !m_last_prompt_line.equals(prompt));

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
    }

    // Activate horizontal scroll mode when requested or when necessary.
    const bool was_horz_scroll = m_horz_scroll;
    m_horz_scroll = (_rl_horizontal_scroll_mode || max_rows <= 1 || m_last_prompt_line_botlin + max_rows > _rl_screenheight);

    // Can we show history expansion?
    const bool can_show_histexpand = (want_histexpand_preview &&
                                      m_last_prompt_line_botlin + max_rows + 1 <= _rl_screenheight);

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    const bool need_update = (!rl_display_fixed || forced_display || was_horz_scroll != m_horz_scroll);

    // Prepare data structures for displaying the input line.
    const display_lines* next = &m_curr;
    m_pending_wrap_display = &m_curr;
    if (need_update)
    {
        next = &m_next;
        if (m_horz_scroll)
            m_next.horz_parse(m_last_prompt_line_botlin, m_last_prompt_line_width, rl_line_buffer, rl_point, rl_end, m_curr);
        else
            m_next.parse(m_last_prompt_line_botlin, m_last_prompt_line_width, rl_line_buffer, rl_end);
        assert(m_next.count() > 0);
    }
#define m_next __use_next_instead__
    const int input_botlin_offset = max<int>(0,
        min<int>(min<int>(next->count() - 1 - m_last_prompt_line_botlin, max_rows - 1), _rl_screenheight - 1));
    const int new_botlin = m_last_prompt_line_botlin + input_botlin_offset;

    // Scroll to keep cursor in view.
    const unsigned int old_top = m_top;
    if (m_top < m_last_prompt_line_botlin)
        m_top = m_last_prompt_line_botlin;
    if (m_last_prompt_line_botlin + next->vpos() < m_top)
        m_top = m_last_prompt_line_botlin + next->vpos();
    if (m_last_prompt_line_botlin + next->vpos() > m_top + input_botlin_offset)
        m_top = next->vpos() - input_botlin_offset;
    if (m_top + input_botlin_offset + 1 > next->count())
        m_top = next->count() - 1 - input_botlin_offset;

    // Scroll when cursor is on a scroll marker.
    if (m_top > m_last_prompt_line_botlin && m_top == m_last_prompt_line_botlin + next->vpos())
    {
        const display_line* d = next->get(m_top);
        if (next->cpos() == d->m_x)
            m_top--;
    }
    else if (m_top + input_botlin_offset < next->count() - 1 && m_top + input_botlin_offset == next->vpos())
    {
        if (next->cpos() + 1 == _rl_screenwidth)
            m_top++;
    }
    assert(m_top >= m_last_prompt_line_botlin);

    // Remember the top.
#undef m_next
    m_next.set_top(m_top);
#define m_next __use_next_instead__

    // Apply scroll markers.
    if (need_update && !m_horz_scroll)
    {
#undef m_next
        m_next.apply_scroll_markers(m_top, m_top + input_botlin_offset);
#define m_next __use_next_instead__
    }

    // Display the last line of the prompt.
    const bool old_horz_scrolled = m_curr.is_horz_scrolled();
    const bool is_horz_scrolled = next->is_horz_scrolled();
    if (m_top == m_last_prompt_line_botlin && (forced_display ||
                                               old_top != m_top ||
                                               old_horz_scrolled != is_horz_scrolled))
    {
        move_to_row(0);
        move_to_column(0, true/*force*/);

        if (modmark)
        {
            if (_rl_display_modmark_color)
                rl_fwrite_function(_rl_out_stream, _rl_display_modmark_color, strlen(_rl_display_modmark_color));

            rl_fwrite_function(_rl_out_stream, "*", 1);

            if (_rl_display_modmark_color)
                rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);
        }

        if (is_message && _rl_display_message_color)
            rl_fwrite_function(_rl_out_stream, _rl_display_message_color, strlen(_rl_display_message_color));

        if (prompt_contains_problem_codes(prompt) & BIT_PROMPT_PROBLEM)
            m_curr.clear();

        rl_fwrite_function(_rl_out_stream, prompt, strlen(prompt));

        if (is_message && _rl_display_message_color)
            rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);

        m_pending_wrap = force_wrap;

        if (is_CJK_codepage(GetConsoleOutputCP()))
        {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            coalesce.split();
            if (get_console_screen_buffer_info(&csbi) &&
                m_last_prompt_line_width != csbi.dwCursorPosition.X)
            {
                m_last_prompt_line_width = csbi.dwCursorPosition.X;
#undef m_next
                if (m_horz_scroll)
                    m_next.horz_parse(m_last_prompt_line_botlin, m_last_prompt_line_width, rl_line_buffer, rl_point, rl_end, m_curr);
                else
                    m_next.parse(m_last_prompt_line_botlin, m_last_prompt_line_width, rl_line_buffer, rl_end);
#define m_next __use_next_instead__
            }
        }

        _rl_last_c_pos = m_last_prompt_line_width;
        _rl_last_v_pos = m_last_prompt_line_botlin;

        move_to_column(_rl_last_c_pos, true/*force*/);

        dbg_ignore_scope(snapshot, "display_readline");

        m_last_prompt_line = prompt;
        m_last_modmark = modmark;
    }

    // From here on, use move_to_row/move_to_column/print/etc so that the
    // m_pending_wrap compatibility logic can work reliably.
#define rl_fwrite_function  __not_safe__
#define rl_fflush_function  __not_safe__
#define tputs               __not_safe__
#define _rl_move_vert       __not_safe__
#define _rl_cr              __not_safe__
#define _rl_crlf            __not_safe__

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    bool can_show_rprompt = false;
    const int old_botlin = _rl_vis_botlin;
    if (need_update)
    {
        // If the right side prompt is shown but shouldn't be, erase it.
        can_show_rprompt = next->can_show_rprompt();
        if (_rl_rprompt_shown_len && !can_show_rprompt)
            print_rprompt(nullptr);

        // Erase old comment row if its row changes.
        if (*m_curr.comment_row() && new_botlin != old_botlin)
        {
            move_to_row(old_botlin + 1);
            move_to_column(0);
            _rl_clear_to_eol(_rl_screenwidth); // BUGBUG: assumes _rl_term_clreol.
        }

        // Update each display line for the line buffer.
        unsigned int rows = m_last_prompt_line_botlin;
        for (unsigned int i = m_top; auto d = next->get(i); ++i)
        {
            if (rows++ > new_botlin)
                break;

            auto o = m_curr.get(i - m_top + old_top);
            update_line(i, o, d, _rl_rprompt_shown_len > 0);
        }

        // Once the display lines have been printed, the next (pending) state
        // must be used for finishing pending wraps.  In practice, this affects
        // only pending wrap caused by printing an rprompt.
        m_pending_wrap_display = next;

        // Erase any surplus lines and update the bottom line counter.
        if (new_botlin < old_botlin)
        {
            if (m_pending_wrap)
                finish_pending_wrap();

            move_to_column(0);

            // BUGBUG: This probably will garble the display if the terminal
            // height has shrunk and no longer fits _rl_vis_botlin.
            for (int i = new_botlin; i++ < old_botlin;)
            {
                move_to_row(i);
                _rl_clear_to_eol(_rl_screenwidth); // BUGBUG: assumes _rl_term_clreol.
            }
        }

        // Update current cursor position.
        assert(_rl_last_c_pos < _rl_screenwidth);

        // Finally update the bottom line counter.
        _rl_vis_botlin = new_botlin;
    }

    // Maybe show history expansion.
    if (can_show_histexpand && _rl_vis_botlin < _rl_screenheight)
    {
        const char* expanded = nullptr;
        const history_expansion* e;
        for (e = m_histexpand; e; e = e->next)
        {
            if (e->start <= rl_point && rl_point <= e->start + e->len)
            {
                expanded = e->result;
                if (!expanded || !*expanded)
                    expanded = "(empty)";
                break;
            }
        }

        if (expanded)
        {
            dbg_ignore_scope(snapshot, "display_readline");

            str_moveable in;
            in << "History expansion for \"";
            append_expand_ctrl(in, rl_line_buffer + e->start, e->len);
            in << "\": ";
            append_expand_ctrl(in, expanded);

#undef m_next
            m_next.set_comment_row(std::move(in));
#define m_next __use_next_instead__
        }
    }

    if (strcmp(m_curr.comment_row(), next->comment_row()) || new_botlin != old_botlin)
    {
        move_to_row(_rl_vis_botlin + 1);
        move_to_column(0);

        if (m_pending_wrap)
            finish_pending_wrap();

        if (*next->comment_row())
        {
            str<> out;
            const int limit = _rl_screenwidth - 1;
            ellipsify(next->comment_row(), limit, out, false);

            str<16> color;
            const char* const color_comment_row = g_color_comment_row.get();
            color << "\x1b[" << color_comment_row << "m";

            print(color.c_str(), color.length());
            print(out.c_str(), out.length());
        }

        print("\x1b[m\x1b[K", 6);
    }

    // If the right side prompt is not shown and should be, display it.
    if (!_rl_rprompt_shown_len && can_show_rprompt)
        print_rprompt(rl_rprompt);

    // Move cursor to the rl_point position.
    move_to_row(m_last_prompt_line_botlin + next->vpos() - m_top);
    move_to_column(next->cpos());

    if (m_pending_wrap)
        finish_pending_wrap();

#undef rl_fwrite_function
#undef rl_fflush_function
#undef tputs
#undef _rl_move_vert
#undef _rl_cr
#undef _rl_crlf

    assert(!m_pending_wrap);
    rl_fflush_function(_rl_out_stream);

    m_pending_wrap_display = nullptr;

#undef m_next

    if (need_update)
    {
        m_next.swap(m_curr);
        m_next.clear();
    }

    rl_display_fixed = 0;

#ifndef LOG_OUTPUT_CALLSTACKS
    coalesce.flush();
#endif

    RL_UNSETSTATE(RL_STATE_REDISPLAYING);
    _rl_release_sigint();
}

//------------------------------------------------------------------------------
void display_manager::set_history_expansions(history_expansion* list)
{
    history_free_expansions(&m_histexpand);
    m_histexpand = list;
}

//------------------------------------------------------------------------------
void display_manager::measure(measure_columns& mc)
{
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
    unsigned int rows = m_last_prompt_line_botlin;
    for (unsigned int i = m_top; auto d = m_curr.get(i); ++i)
    {
        if (rows++ > _rl_vis_botlin)
            break;

        // Reset the column if the first display line starts at column 0, which
        // happens when scrolling (vert or horz) is active.
        if (i == m_top && d->m_x == 0)
            mc.reset_column();

        if (rl_point < d->m_start)
            break;

        unsigned int len = d->m_len;
        if (rl_point >= d->m_start && rl_point < d->m_end)
            len = d->m_lead + rl_point - d->m_start;
        mc.measure(d->m_chars, len, false);
    }
}

//------------------------------------------------------------------------------
void display_manager::update_line(int i, const display_line* o, const display_line* d, bool has_rprompt)
{
    unsigned int lcol = d->m_x;
    unsigned int rcol = d->m_lastcol + d->m_trail;
    unsigned int lind = 0;
    unsigned int rind = d->m_len;
    int delta = 0;

    // If the old and new lines are identical, there's nothing to do.
    if (o &&
        o->m_x == d->m_x &&
        o->m_len == d->m_len &&
        !memcmp(o->m_chars, d->m_chars, d->m_len) &&
        !memcmp(o->m_faces, d->m_faces, d->m_len))
        return;

    bool use_eol_opt = !has_rprompt && d->m_toeol && s_use_eol_optimization;

    // Optimize updating when the new starting column is less than or equal to
    // the old starting column.  Can't optimize in the other direction unless
    // update_line(0) happens before displaying the prompt string.
    if (o && d->m_x <= o->m_x)
    {
        const char* oc = o->m_chars;
        const char* of = o->m_faces;
        const char* dc = d->m_chars;
        const char* df = d->m_faces;
        unsigned int stop = min<unsigned int>(o->m_len, d->m_len);

        // Find left index of difference.
        str_iter iter(dc, stop);
        const char* p = dc;
        while (const int c = iter.next())
        {
            const char* q = iter.get_pointer();
            const char* walk = p;
test_left:
            if (*oc != *dc || *of != *df)
                break;
            oc++;
            dc++;
            of++;
            df++;
            if (++walk < q)
                goto test_left;
            lcol += clink_wcwidth(c);
            p = q;
        }
        lind = static_cast<unsigned int>(p - d->m_chars);

        oc = o->m_chars + lind;
        of = o->m_faces + lind;
        dc = d->m_chars + lind;
        df = d->m_faces + lind;
        const char* oc2 = o->m_chars + o->m_len;
        const char* of2 = o->m_faces + o->m_len;
        const char* dc2 = d->m_chars + d->m_len;
        const char* df2 = d->m_faces + d->m_len;

        // Find right index of difference.  But not if there is a right side
        // prompt on this line.
        if (!has_rprompt)
        {
            const char* dcend = dc2;

            while (oc2 > oc && dc2 > dc)
            {
                const char* oback = oc + _rl_find_prev_mbchar(const_cast<char*>(oc), oc2 - oc, MB_FIND_ANY);
                const char* dback = dc + _rl_find_prev_mbchar(const_cast<char*>(dc), dc2 - dc, MB_FIND_ANY);
                if (oc2 - oback != dc2 - dback)
                    break;
                const size_t bytes = dc2 - dback;
                if (memcmp(oback, dback, bytes) ||
                    memcmp(of2 - bytes, df2 - bytes, bytes))
                    break;
                oc2 = oback;
                dc2 = dback;
                of2 -= bytes;
                df2 -= bytes;
            }

            if (use_eol_opt)
            {
                const char* ec = dc2;
                const char* ef = df2;
                size_t elen = d->m_len - (dc2 - d->m_chars);
                while (elen--)
                {
                    if (*ec != ' ' || *ef != FACE_NORMAL)
                    {
                        use_eol_opt = false;
                        break;
                    }
                    --ec;
                    --ef;
                }
            }
        }

        const unsigned int olen = static_cast<unsigned int>(oc2 - oc);
        const unsigned int dlen = static_cast<unsigned int>(dc2 - dc);
        assert(oc2 - oc == of2 - of);
        assert(dc2 - dc == df2 - df);
        rind = lind + dlen;

        // Measure columns, to find whether to delete characters or open spaces.
        unsigned int dcols = raw_measure_cols(dc, dlen);
        rcol = lcol + dcols;
        if (oc2 < o->m_chars + o->m_len)
        {
            unsigned int ocols = raw_measure_cols(oc, olen);
            delta = dcols - ocols;
        }

#ifdef DEBUG
        if (dbg_get_env_int("DEBUG_DISPLAY"))
        {
            dbg_printf_row(-1, "delta %d; len %d/%d; col %d/%d; ind %d/%d\r\n", delta, olen, dlen, lcol, rcol, lind, rind);
            dbg_printf_row(-1, "old=[%*s]\toface='[%*s]'\r\n", olen, oc, olen, of);
            dbg_printf_row(-1, "new=[%*s]\tdface='[%*s]'\r\n", dlen, dc, dlen, df);
        }
#endif
    }

    assert(i >= m_top);
    const unsigned int row = m_last_prompt_line_botlin + i - m_top;

    move_to_row(row);

    if (o && o->m_x > d->m_x)
    {
        move_to_column(d->m_x);
        shift_cols(d->m_x, d->m_x - o->m_x);
    }

    move_to_column(lcol);
    shift_cols(lcol, delta);

    if (m_pending_wrap)
        finish_pending_wrap();

    rl_puts_face_func(d->m_chars + lind, d->m_faces + lind, rind - lind);

    _rl_last_c_pos = rcol;

    // Clear anything leftover from o.
    if (o && d->m_lastcol < o->m_lastcol)
    {
        if (use_eol_opt)
        {
            rl_fwrite_function(_rl_out_stream, "\x1b[K", 3);
        }
        else
        {
            // m_lastcol does not include filler spaces; and that's fine since
            // the spaces use FACE_NORMAL.
            const unsigned int erase_cols = o->m_lastcol - d->m_lastcol;

            move_to_column(d->m_lastcol);

            str<> tmp;
            while (tmp.length() < erase_cols)
            {
                const unsigned int c = min<unsigned int>(32, erase_cols - tmp.length());
                tmp.concat("                                ", c);
            }

            rl_fwrite_function(_rl_out_stream, tmp.c_str(), tmp.length());
            _rl_last_c_pos += erase_cols;
        }
    }

    // Scroll marker should have a trailing space.
    assertimplies(d->m_scroll_mark < 0, _rl_last_c_pos < _rl_screenwidth);

    // Update cursor position and deal with autowrap.
    detect_pending_wrap();
}

//------------------------------------------------------------------------------
void display_manager::move_to_column(unsigned int col, bool force)
{
    assert(_rl_term_ch && *_rl_term_ch);

    assert(col < _rl_screenwidth);
    if (col == _rl_last_c_pos && !force)
        return;

    if (m_pending_wrap)
        finish_pending_wrap();

    if (col)
    {
        char *buffer = tgoto(_rl_term_ch, 0, col + 1);
        tputs(buffer);
    }
    else
    {
        _rl_cr();
    }

    _rl_last_c_pos = col;
}

//------------------------------------------------------------------------------
void display_manager::move_to_row(int row)
{
    if (row == _rl_last_v_pos)
        return;

    if (m_pending_wrap)
        finish_pending_wrap();

    _rl_move_vert(row);
}

//------------------------------------------------------------------------------
void display_manager::shift_cols(unsigned int col, int delta)
{
    assert(col == _rl_last_c_pos);

    if (delta > 0)
    {
        if (m_pending_wrap)
            finish_pending_wrap();

        assert(delta < _rl_screenwidth - col);
        if (_rl_term_IC)
        {
            char* buffer = tgoto(_rl_term_IC, 0, delta);
            tputs(buffer);
        }
#if 0
        else if (_rl_term_im && *_rl_term_im && _rl_term_ei && *_rl_term_ei)
        {
            tputs(_rl_term_im);
            for (int i = delta; i--;)
                _rl_output_character_function(' ');
            tputs(_rl_term_ei);
        }
        else if (_rl_term_ic && *_rl_term_ic)
        {
            for (int i = delta; i--;)
                tputs(_rl_term_ic);
        }
#endif
        else
            assert(false);
    }
    else if (delta < 0)
    {
        if (m_pending_wrap)
            finish_pending_wrap();

        assert(-delta < _rl_screenwidth - col);
        if (_rl_term_DC && *_rl_term_DC)
        {
            char *buffer = tgoto(_rl_term_DC, -delta, -delta);
            tputs(buffer);
        }
#if 0
        else if (_rl_term_dc && *_rl_term_dc)
        {
            for (int i = -delta; i--;)
                tputs(_rl_term_dc);
        }
#endif
        else
            assert(false);
    }

    move_to_column(col);
}

//------------------------------------------------------------------------------
void display_manager::print(const char* chars, unsigned int len)
{
    m_pending_wrap = false;
    rl_fwrite_function(_rl_out_stream, chars, len);
}

//------------------------------------------------------------------------------
void display_manager::print_rprompt(const char* s)
{
    const int col = _rl_screenwidth - (s ? rl_visible_rprompt_length : _rl_rprompt_shown_len);
    if (col <= 0 || col >= _rl_screenwidth)
        return;

    move_to_row(0);
    move_to_column(col);

    if (m_pending_wrap)
        finish_pending_wrap();

    if (s)
    {
        tputs(s);
        _rl_last_c_pos = _rl_screenwidth;
    }
    else
    {
        _rl_clear_to_eol(_rl_rprompt_shown_len);
    }

    _rl_rprompt_shown_len = s ? rl_visible_rprompt_length : 0;

    // Win10 and higher don't need to deal with pending wrap from rprompt.
    if (m_autowrap_bug)
        detect_pending_wrap();
}

//------------------------------------------------------------------------------
void display_manager::detect_pending_wrap()
{
    if (_rl_last_c_pos == _rl_screenwidth)
    {
        _rl_last_c_pos = 0;
        _rl_last_v_pos++;
        m_pending_wrap = true;
    }
    else
    {
        m_pending_wrap = false;
    }
}

//------------------------------------------------------------------------------
void display_manager::finish_pending_wrap()
{
    // This finishes a pending wrap using a technique that works equally well on
    // both Win 8.1 and Win 10.
    assert(m_pending_wrap);
    assert(m_pending_wrap_display);
    assert(_rl_last_c_pos == 0);

    unsigned int bytes = 0;

    // If there's a display_line, then re-print its first character to force
    // wrapping.  Otherwise, print a placeholder.
    const int index = m_pending_wrap_display->top() +  _rl_last_v_pos;
    assert(index >= 0);
    if (index < m_pending_wrap_display->count())
    {
        const display_line& d = *m_pending_wrap_display->get(index);

        str_iter iter(d.m_chars, d.m_len);
        unsigned int cols = 0;
        while (const int c = iter.next())
        {
            const int wc = clink_wcwidth(c);
            cols += wc;
            if (wc)
                break;
        }

        bytes = static_cast<unsigned int>(iter.get_pointer() - d.m_chars);
        if (bytes)
        {
            rl_puts_face_func(d.m_chars, d.m_faces, bytes);
            _rl_cr();
        }
    }

    if (!bytes)
    {
        // If there's no display_line or it's empty, print a space to force
        // wrapping and a backspace to move the cursor to the beginning of the
        // line with the fewest possible side effects (which potentially matters
        // during terminal resize, which is asynchronous with respect to the
        // console application).
        rl_fwrite_function(_rl_out_stream, "\x1b[m \x08", 5);
    }

    m_pending_wrap = false;
}

#endif // INCLUDE_CLINK_DISPLAY_READLINE



//------------------------------------------------------------------------------
static bool s_use_display_manager = false;

//------------------------------------------------------------------------------
extern "C" int use_display_manager()
{
#if defined (OMIT_DEFAULT_DISPLAY_READLINE)
    s_use_display_manager = true;
#elif defined (INCLUDE_CLINK_DISPLAY_READLINE)
    str<> env;
    if (os::get_env("USE_DISPLAY_MANAGER", env))
        s_use_display_manager = !!atoi(env.c_str());
    else
        s_use_display_manager = true;
#endif
    return s_use_display_manager;
}

//------------------------------------------------------------------------------
extern "C" void host_on_new_line()
{
#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
    if (use_display_manager())
        s_display_manager.on_new_line();
#endif

    // Terminal shell integration.
    terminal_end_command();
}

//------------------------------------------------------------------------------
#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
extern "C" void end_prompt_lf()
{
    if (use_display_manager())
        s_display_manager.end_prompt_lf();
}
#endif

//------------------------------------------------------------------------------
void reset_readline_display()
{
    clear_to_end_of_screen();
#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
    if (use_display_manager())
        s_display_manager.clear();
#endif
}

//------------------------------------------------------------------------------
void refresh_terminal_size()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    get_console_screen_buffer_info(&csbi);

    const int width = csbi.dwSize.X;
    const int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    if (_rl_screenheight != height || _rl_screenwidth != width)
    {
        _rl_get_screen_size(0, 0);
        if (g_debug_log_terminal.get())
            LOG("terminal size %u x %u", _rl_screenwidth, _rl_screenheight);
    }
}

//------------------------------------------------------------------------------
void display_readline()
{
#if defined (OMIT_DEFAULT_DISPLAY_READLINE) && !defined (INCLUDE_CLINK_DISPLAY_READLINE)
#error Must have at least one display implementation defined.
#endif

#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
#if !defined (HANDLE_MULTIBYTE)
#error INCLUDE_CLINK_DISPLAY_READLINE requires HANDLE_MULTIBYTE.
#endif
    if (use_display_manager())
    {
        s_display_manager.display();
        return;
    }
#endif

#if !defined (OMIT_DEFAULT_DISPLAY_READLINE)
    rl_redisplay();
#endif
}

//------------------------------------------------------------------------------
void set_history_expansions(history_expansion* list)
{
    if (use_display_manager())
        s_display_manager.set_history_expansions(list);
    else
        history_free_expansions(&list);
}

//------------------------------------------------------------------------------
void resize_readline_display(const char* prompt, const line_buffer& buffer, const char* _prompt, const char* _rprompt)
{
    // Clink tries to put the cursor on the original top row, compensating for
    // terminal wrapping behavior, and redisplay the prompt and input buffer.
    //
    // DISCLAIMER:  Windows captures various details about output it received in
    // order to improve its line wrapping behavior.  Those supplemental details
    // are not available outside conhost itself, and its wrapping algorithm is
    // complex and inconsistent, so there's no reliable way for Clink to predict
    // the actual exact wrapping that will occur.

    // Coalesce all Readline output in this scope into a single WriteConsoleW
    // call.  This avoids the vast majority of race conditions that can occur
    // between the OS async terminal resize and cursor movement while refreshing
    // the Readline display.  The result is near-perfect resize behavior; but
    // perfection is beyond reach, due to the inherent async execution.
#ifndef LOG_OUTPUT_CALLSTACKS
    display_accumulator coalesce;
#endif

#if defined(NO_READLINE_RESIZE_TERMINAL)
    // Update Readline's perception of the terminal dimensions.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    get_console_screen_buffer_info(&csbi);
    refresh_terminal_size();
#endif

    // Measure what was previously displayed.
    measure_columns mc(measure_columns::resize);
#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
    if (use_display_manager())
    {
        s_display_manager.measure(mc);
    }
    else
#endif
    {
        // Measure the lines for the prompt segment.
#if defined(NO_READLINE_RESIZE_TERMINAL)
        mc.measure(prompt, true);
#else
        const char* last_prompt_line = strrchr(prompt, '\n');
        if (last_prompt_line)
            ++last_prompt_line;
        else
            last_prompt_line = prompt;
        mc.measure(last_prompt_line, true);
#endif

        // Measure the new number of lines to the cursor position.
        const char* buffer_ptr = buffer.get_buffer();
        mc.measure(buffer_ptr, buffer.get_cursor(), false);
    }
    int cursor_line = mc.get_line_count() - 1;

    // WORKAROUND FOR OS ISSUE:  If the buffer ends with one trailing space and
    // the cursor is at the end of the input line, then the OS can wrap the line
    // strangely and end up inserting an extra blank line between the cursor and
    // the preceding text.  Test for a blank line above the cursor, and
    // increment cursor_line to compensate.
    if (cursor_line > 0 && csbi.dwCursorPosition.X == 1)
    {
        const unsigned int cur = buffer.get_cursor();
        const unsigned int len = buffer.get_length();
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
    {
        char *tmp = tgoto(tgetstr("UP", nullptr), 0, cursor_line);
        tputs(tmp);
    }
    _rl_cr();
    _rl_last_v_pos = 0;
    _rl_last_c_pos = 0;

    // Clear to end of screen.
    reset_readline_display();

#if defined(NO_READLINE_RESIZE_TERMINAL)
    // Readline (even in bash on Ubuntu in WSL in Windows Terminal) doesn't do
    // very well at responding to terminal resize events.  Apparently Clink must
    // take care of it manually.  Calling rl_set_prompt() recalculates the
    // prompt line breaks.
    rl_set_prompt(_prompt);
    rl_set_rprompt(_rprompt && *_rprompt ? _rprompt : nullptr);
    g_prompt_redisplay++;
    rl_forced_update_display();
#else
    // Let Readline update its display.
    rl_resize_terminal();

    if (g_debug_log_terminal.get())
        LOG("terminal size %u x %u", _rl_screenwidth, _rl_screenheight);
#endif
}

//------------------------------------------------------------------------------
unsigned int get_readline_display_top_offset()
{
#if defined (INCLUDE_CLINK_DISPLAY_READLINE)
    if (use_display_manager())
        return s_display_manager.top_offset();
#endif
    return 0;
}
