/*

    Custom display routines for the Readline prompt and input buffer,
    as well as the Clink auto-suggestions.

*/

#include "pch.h"
#include <assert.h>

#include "display_readline.h"
#include "line_buffer.h"

#include <core/base.h>
#include <core/os.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/debugheap.h>
#include <terminal/ecma48_iter.h>
//#include <terminal/terminal_helpers.h>

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
extern int tputs(const char* str, int affcnt, int (*putc_func)(int));
extern char* tgoto(const char* base, int x, int y);
extern void tputs_rprompt(const char *s);

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
extern int g_prompt_redisplay;

//------------------------------------------------------------------------------
static setting_int g_input_rows(
    "clink.max_input_rows",
    "Maximum rows for the input line",
    "This limits how many rows the input line can use, up to the terminal height.\n"
    "When this is 0, the terminal height is the limit.",
    0);

extern setting_bool g_debug_log_terminal;

//------------------------------------------------------------------------------
static void clear_to_end_of_screen()
{
    static const char* const termcap_cd = tgetstr("cd", nullptr);
    rl_fwrite_function(_rl_out_stream, termcap_cd, strlen(termcap_cd));
}

//------------------------------------------------------------------------------
static void move_to_column(unsigned int cpos)
{
    assert(_rl_term_ch && *_rl_term_ch);

    assert(cpos < _rl_screenwidth);
    if (cpos == _rl_last_c_pos)
        return;

    char *buffer = tgoto(_rl_term_ch, 0, cpos + 1);
    tputs(buffer, 1, _rl_output_character_function);

    _rl_last_c_pos = cpos;
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
static void shift_cols(unsigned int col, int delta)
{
    assert(col == _rl_last_c_pos);

    if (delta > 0)
    {
        assert(delta < _rl_screenwidth - col);
        if (_rl_term_IC)
        {
            char* buffer = tgoto(_rl_term_IC, 0, delta);
            tputs(buffer, 1, _rl_output_character_function);
        }
#if 0
        else if (_rl_term_im && *_rl_term_im && _rl_term_ei && *_rl_term_ei)
        {
            tputs(_rl_term_im, 1, _rl_output_character_function);
            for (int i = delta; i--;)
                _rl_output_character_function(' ');
            tputs(_rl_term_ei, 1, _rl_output_character_function);
        }
        else if (_rl_term_ic && *_rl_term_ic)
        {
            for (int i = delta; i--;)
                tputs(_rl_term_ic, 1, _rl_output_character_function);
        }
#endif
        else
            assert(false);

        move_to_column(col);
    }
    else if (delta < 0)
    {
        assert(-delta < _rl_screenwidth - col);
        if (_rl_term_DC && *_rl_term_DC)
        {
            char *buffer = tgoto(_rl_term_DC, -delta, -delta);
            tputs(buffer, 1, _rl_output_character_function);
        }
#if 0
        else if (_rl_term_dc && *_rl_term_dc)
        {
            for (int i = -delta; i--;)
                tputs(_rl_term_dc, 1, _rl_output_character_function);
        }
#endif
        else
            assert(false);

        move_to_column(col);
    }
}



//------------------------------------------------------------------------------
#define FACE_NORMAL     '0'
#define FACE_STANDOUT   '1'
#define FACE_INVALID    ((char)1)

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
    void                compensate_force_wrap();
    void                swap(display_lines& d);
    void                clear();

    const display_line* get(unsigned int index) const;
    unsigned int        count() const;
    bool                can_show_rprompt() const;
    bool                is_horz_scrolled() const;

    unsigned int        vpos() const { return m_vpos; }
    unsigned int        cpos() const { return m_cpos; }

private:
    display_line*       next_line(unsigned int start);
    bool                adjust_columns(unsigned int& point, int delta, const char* buffer, unsigned int len) const;

    std::vector<display_line> m_lines;
    unsigned int        m_count = 0;
    unsigned int        m_prompt_botlin;
    unsigned int        m_vpos = 0;
    unsigned int        m_cpos = 0;
    unsigned int        m_horz_start = 0;
    bool                m_horz_scroll = false;
};

//------------------------------------------------------------------------------
void display_lines::parse(unsigned int prompt_botlin, unsigned int col, const char* buffer, unsigned int len)
{
    assert(col < _rl_screenwidth);
    dbg_ignore_scope(snapshot, "display_readline");

    clear();

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
        d->append('<', '<');
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
        d->append('>', '<');
        d->m_lastcol++;
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
            d.append('<', '<');
            d.appendnul();
        }
        else
        {
            str_iter iter_top(d.m_chars, d.m_len);
            c = iter_top.next();
            if (c)
            {
another:
                int wc = clink_wcwidth(c);
                if (!wc)
                {
                    c = iter_top.next();
                    if (!c)
                        goto no_backward_indicator;
                    goto another;
                }

                int bytes = static_cast<int>(iter_top.get_pointer() - d.m_chars);
                assert(bytes >= wc);
                if (bytes < wc)
                    goto no_backward_indicator;

                unsigned int i = 0;
                d.m_chars[i] = '<';
                d.m_faces[i] = '<';
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

no_backward_indicator:
                ;
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
            while (d.m_x + d.m_lastcol >= _rl_screenwidth)
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
            d.append('>', '<');
            d.m_scroll_mark = -1;
            d.m_lastcol++;
            d.appendnul();
        }
    }
}

//------------------------------------------------------------------------------
void display_lines::compensate_force_wrap()
{
    if (!m_count)
        return;

    display_line& d = m_lines[0];
    str_iter iter(d.m_chars, d.m_len);
    unsigned int cols = 0;
    while (const int c = iter.next())
    {
        const int wc = clink_wcwidth(c);
        cols += wc;
        if (wc)
            break;
    }

    unsigned int bytes = static_cast<unsigned int>(iter.get_pointer() - d.m_chars);
    assert(bytes >= cols);

    memmove(d.m_chars, d.m_chars + bytes - cols, d.m_len - (bytes - cols));
    memmove(d.m_faces, d.m_faces + bytes - cols, d.m_len - (bytes - cols));
    while (cols--)
    {
        d.m_chars[cols] = ' ';
        d.m_faces[cols] = FACE_NORMAL;
    }
    d.m_len -= (bytes - cols);
}

//------------------------------------------------------------------------------
void display_lines::swap(display_lines& d)
{
    m_lines.swap(d.m_lines);
    std::swap(m_count, d.m_count);
    std::swap(m_prompt_botlin, d.m_prompt_botlin);
    std::swap(m_vpos, d.m_vpos);
    std::swap(m_cpos, d.m_cpos);
    std::swap(m_horz_start, d.m_horz_start);
    std::swap(m_horz_scroll, d.m_horz_scroll);
}

//------------------------------------------------------------------------------
void display_lines::clear()
{
    for (unsigned int i = m_count; i--;)
        m_lines[i].clear();
    m_count = 0;
    m_prompt_botlin = 0;
    m_vpos = 0;
    m_cpos = 0;
    m_horz_start = 0;
    m_horz_scroll = false;
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
display_line* display_lines::next_line(unsigned int start)
{
    assert(!m_horz_scroll);

    if (m_count >= m_lines.size())
        m_lines.emplace_back();

    display_line* d = &m_lines[m_count++];
    assert(!d->m_x);
    assert(!d->m_len);
    d->m_start = start;
    return d;
}

//------------------------------------------------------------------------------
bool display_lines::adjust_columns(unsigned int& index, int delta, const char* buffer, unsigned int len) const
{
    assert(delta != 0);
    assert(len >= index);

    const char* walk = buffer + index;
    bool first = true;

    if (delta < 0)
    {
        delta *= -1;
        while (delta > 0)
        {
            if (!index)
                return false;
            const int i = _rl_find_prev_mbchar(const_cast<char*>(buffer), index, MB_FIND_NONZERO);
            const int bytes = index - i;
            const int width = (bytes == 1 && (CTRL_CHAR(buffer[-1]) || buffer[-1] == RUBOUT)) ? 2 : raw_measure_cols(buffer - bytes, bytes);
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
                    measure_columns(measure_mode mode) : m_mode(mode) {}
    void            measure(const char* text, unsigned int len, bool is_prompt);
    void            measure(const char* text, bool is_prompt);
    void            reset_column() { m_col = 0; }
    int             get_column() const { return m_col; }
    int             get_line_count() const { return m_line_count; }
    bool            get_force_wrap() const { return m_force_wrap; }
private:
    const measure_mode m_mode;
    int             m_col = 0;
    int             m_line_count = 1;
    bool            m_force_wrap = false;
};

//------------------------------------------------------------------------------
void measure_columns::measure(const char* text, unsigned int length, bool is_prompt)
{
    ecma48_state state;
    ecma48_iter iter(text, state, length);
    const char* last_lf = nullptr;
    bool wrapped = false;
    while (const ecma48_code &code = iter.next())
    {
// TODO-DISPLAY:  Predict unwrapping more accurately when resizing the terminal wider.
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
                    if (m_col >= _rl_screenwidth)
                    {
                        if (is_prompt && m_mode == print && m_col == _rl_screenwidth)
                            wrapped = true; // Defer, for accurate measurement.
                        else
                            ++m_line_count;
                        m_col = (m_col > _rl_screenwidth) ? n : 0;
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
                    while (m_col >= _rl_screenwidth)
                    {
                        m_col -= _rl_screenwidth;
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
                wrapped = false;
                m_col = 0;
                break;

            case ecma48_code::c0_ht:
#if !defined(DISPLAY_TABS)
                if (!is_prompt)
                {
                    // BUGBUG:  This case should instead count the spaces that
                    // were actually previously printed.
                    goto ctrl_char;
                }
#endif
                if (wrapped)
                {
                    wrapped = false;
                    ++m_line_count;
                }
                if (int n = 8 - (m_col & 7))
                {
                    m_col = min(m_col + n, _rl_screenwidth);
                    m_col = min(m_col + n, _rl_screenwidth);
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
    void                measure(measure_columns& mc);

private:
    bool                update_line(int i, const display_line* o, const display_line* d, bool wrapped);

    display_lines       m_next;
    display_lines       m_curr;
    unsigned int        m_top = 0;      // Vertical scrolling; index to top displayed line.
    str_moveable        m_last_prompt_line;
    int                 m_last_prompt_line_width = -1;
    int                 m_last_prompt_line_botlin = -1;
    bool                m_last_modmark = false;
    bool                m_horz_scroll = false;
};

//------------------------------------------------------------------------------
static display_manager s_display_manager;

//------------------------------------------------------------------------------
display_manager::display_manager()
{
    rl_on_new_line();
}

//------------------------------------------------------------------------------
void display_manager::clear()
{
    m_next.clear();
    m_curr.clear();
    m_last_prompt_line.clear();
    m_last_prompt_line_width = -1;
    m_last_prompt_line_botlin = -1;
    m_last_modmark = false;
    m_horz_scroll = false;
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
        m_top = 0;
}

//------------------------------------------------------------------------------
void display_manager::end_prompt_lf()
{
    // FUTURE: When in a scrolling mode (vert or horz), reprint the entire
    // prompt and input line without the scroll constraints?

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

    display_accumulator coalesce;

    if (!rl_display_prompt)
        rl_display_prompt = "";

    // Max number of rows to use when displaying the input line.
    unsigned int max_rows = g_input_rows.get();
    if (!max_rows)
        max_rows = _rl_screenheight;
    max_rows = min<unsigned int>(max_rows, _rl_screenheight);
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

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    const bool need_update = (!rl_display_fixed || forced_display || was_horz_scroll != m_horz_scroll);

    // Prepare data structures for displaying the input line.
    const display_lines* next = &m_curr;
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
        _rl_move_vert(0);
        _rl_cr();

        if (modmark)
        {
            rl_fwrite_function(_rl_out_stream, _rl_display_modmark_color, strlen(_rl_display_modmark_color));
            rl_fwrite_function(_rl_out_stream, "*\x1b[m", 4);
        }

        if (is_message)
            rl_fwrite_function(_rl_out_stream, _rl_display_message_color, strlen(_rl_display_message_color));

        rl_fwrite_function(_rl_out_stream, prompt, strlen(prompt));

        if (is_message)
            rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);

        _rl_last_c_pos = m_last_prompt_line_width;
        _rl_last_v_pos = m_last_prompt_line_botlin;

        dbg_ignore_scope(snapshot, "display_readline");

        m_last_prompt_line = prompt;
        m_last_modmark = modmark;
    }

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    bool can_show_rprompt = false;
    if (need_update)
    {
        if (force_wrap)
        {
            rl_fwrite_function(_rl_out_stream, "\x1b[m \b", 5);
            m_curr.compensate_force_wrap();
        }

        // If the right side prompt is shown but shouldn't be, erase it.
        can_show_rprompt = next->can_show_rprompt();
        if (_rl_rprompt_shown_len && !can_show_rprompt)
        {
            assert(_rl_last_v_pos == 0);
            tputs_rprompt(0);
        }

        // Update each display line for the line buffer.
        bool wrapped = false;
        unsigned int rows = m_last_prompt_line_botlin;
        for (unsigned int i = m_top; auto d = next->get(i); ++i)
        {
            if (rows++ > new_botlin)
                break;

            auto o = m_curr.get(i - m_top + old_top);
            wrapped = update_line(i, o, d, wrapped);
        }

        // Erase any surplus lines and update the bottom line counter.
        //if (new_botlin < _rl_vis_botlin)
        {
            _rl_cr();
            _rl_last_c_pos = 0;

            for (int i = new_botlin; i++ < _rl_vis_botlin;)
            {
                _rl_move_vert(i);
                // BUGBUG: assumes _rl_term_clreol.
                _rl_clear_to_eol(_rl_screenwidth);
            }
        }

        // Update current cursor position.
        assert(_rl_last_c_pos < _rl_screenwidth);

        // Finally update the bottom line counter.
        _rl_vis_botlin = new_botlin;
    }

    // Move cursor to the rl_point position.
    _rl_move_vert(m_last_prompt_line_botlin + next->vpos() - m_top);
    move_to_column(next->cpos());

    rl_fflush_function(_rl_out_stream);

#undef m_next

    if (need_update)
    {
        m_next.swap(m_curr);
        m_next.clear();
    }

    rl_display_fixed = 0;

    // If the right side prompt is not shown and should be, display it.
    if (!_rl_rprompt_shown_len && can_show_rprompt)
        tputs_rprompt(rl_rprompt);

    coalesce.flush();

    RL_UNSETSTATE(RL_STATE_REDISPLAYING);
    _rl_release_sigint();
}

//------------------------------------------------------------------------------
void display_manager::measure(measure_columns& mc)
{
    // FUTURE:  Ideally this would remember what prompt it displayed and use
    // that here, rather than using whatever is the current prompt content.
    const char* prompt = rl_get_local_prompt();
    const char* prompt_prefix = rl_get_local_prompt_prefix();
    assert(prompt);

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
bool display_manager::update_line(int i, const display_line* o, const display_line* d, bool wrapped)
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
        return false;

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

        // Ignore trailing spaces with FACE_NORMAL.
        while (oc2 > oc && oc2[-1] == ' ' && of2[-1] == FACE_NORMAL)
            --oc2, --of2;
        while (dc2 > dc && dc2[-1] == ' ' && df2[-1] == FACE_NORMAL)
            --dc2, --df2;

        // Find right index of difference.
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
    if (wrapped && !delta && lcol == 0 && row == _rl_last_v_pos + 1)
    {
        if (_rl_term_autowrap)
        {
            rl_fwrite_function(_rl_out_stream, " ", 1);
            _rl_cr();
        }
        else
        {
            rl_crlf();
        }
        _rl_last_v_pos++;
        _rl_last_c_pos = 0;
    }
    else
    {
        if (row != _rl_last_v_pos)
            _rl_move_vert(row);

        if (o && o->m_x > d->m_x)
        {
            move_to_column(d->m_x);
            shift_cols(d->m_x, d->m_x - o->m_x);
        }

        move_to_column(lcol);
        shift_cols(lcol, delta);
    }

    rl_puts_face_func(d->m_chars + lind, d->m_faces + lind, rind - lind);
    rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);

    _rl_last_c_pos = rcol;

    // Clear anything leftover from o.
    if (o && rind == d->m_len && d->m_lastcol < o->m_lastcol)
    {
        // m_lastcol does not include filler spaces; and that's fine since
        // the spaces use FACE_NORMAL.
        const unsigned int erase_cols = o->m_lastcol - d->m_lastcol;

        str<> tmp;
        while (tmp.length() < erase_cols)
        {
            const unsigned int c = min<unsigned int>(32, erase_cols - tmp.length());
            tmp.concat("                                ", c);
        }

        rl_fwrite_function(_rl_out_stream, tmp.c_str(), tmp.length());
        _rl_last_c_pos += erase_cols;
    }

    // Update cursor position and deal with autowrap.
    if (_rl_last_c_pos == _rl_screenwidth)
    {
        if (d->m_scroll_mark < 0)
        {
            _rl_cr();
            _rl_last_c_pos = 0;
        }
    }

    return (_rl_last_c_pos == _rl_screenwidth);
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
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(h, &csbi);

    const int width = csbi.dwSize.X;
    const int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    if (_rl_screenheight != height || _rl_screenwidth != width)
    {
        rl_set_screen_size(height, width);
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
    display_accumulator coalesce;

#if defined(NO_READLINE_RESIZE_TERMINAL)
    // Update Readline's perception of the terminal dimensions.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(h, &csbi);
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
        tputs(tmp, 1, _rl_output_character_function);
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
