/*

    Custom display routines for the Readline prompt and input buffer,
    as well as the Clink auto-suggestions.

*/

#include "pch.h"
#include <assert.h>

#include "display_readline.h"

#include <core/base.h>
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

#ifndef DISPLAY_TABS
#error DISPLAY_TABS is required.
#endif
#ifndef HANDLE_MULTIBYTE
#error HANDLE_MULTIBYTE is required.
#endif

//------------------------------------------------------------------------------
static setting_int g_input_rows(
    "clink.max_input_rows",
    "Maximum rows for the input line",
    "This limits how many rows the input line can use, up to the terminal height.\n"
    "When this is 0, the terminal height is the limit.",
    0);

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
static unsigned int measure_cols(const char* s, unsigned int len)
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

    void                parse(unsigned int col, const char* buffer, unsigned int len);
    void                apply_scroll_indicators(unsigned int top, unsigned int bottom);
    void                swap(display_lines& d);
    void                clear();

    const display_line* get(unsigned int index) const;
    unsigned int        count() const;
    bool                can_show_rprompt() const;

    unsigned int        vpos() const { return m_vpos; }
    unsigned int        cpos() const { return m_cpos; }

private:
    display_line*       next_line(unsigned int start);

    std::vector<display_line> m_lines;
    unsigned int        m_count = 0;
    unsigned int        m_vpos = 0;
    unsigned int        m_cpos = 0;
};

//------------------------------------------------------------------------------
void display_lines::parse(unsigned int col, const char* buffer, unsigned int len)
{
    assert(col < _rl_screenwidth);
    dbg_ignore_scope(snapshot, "display_readline");

    clear();

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

// TODO-DISPLAY: when rl_byte_oriented == 0.

    str_iter iter(buffer, len);
    const char* chars = buffer;
    const char* end = nullptr;
    for (; true; chars = end)
    {
        const int c = iter.next();
        if (!c)
            break;

        end = iter.get_pointer();

        if (META_CHAR(*chars) && !_rl_output_meta_chars)
        {
            // When output-meta is off, display 0x80-0xff as octal (\ooo).
            for (tmp.clear(); chars < end; ++chars)
                tmp.format("\\%o", *chars);
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
        else if (c == '\n' && !_rl_horizontal_scroll_mode && _rl_term_up && *_rl_term_up)
        {
            d->m_lastcol = col;
            d->m_end = static_cast<unsigned int>(index);
            d->appendnul();
            d->m_newline = true;

            ++index;
            d = next_line(index);
            col = 0;
            continue;
        }
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
        index = static_cast<unsigned int>(end - buffer);

        if (index == rl_point)
        {
            m_vpos = m_count - 1;
            m_cpos = col;
        }

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
void display_lines::apply_scroll_indicators(unsigned int top, unsigned int bottom)
{
    assert(top <= bottom);
    assert(top < m_count);
    assert(bottom < m_count);

    int c;

    if (top > 0)
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
        display_line& d = m_lines[bottom];

        if (d.m_lastcol - d.m_x > 2)
        {
            d.m_len -= d.m_trail;
            while (d.m_x + d.m_lastcol >= _rl_screenwidth)
            {
                const int bytes = _rl_find_prev_mbchar(d.m_chars, d.m_len, MB_FIND_NONZERO);
                d.m_lastcol -= measure_cols(d.m_chars + bytes, d.m_len - bytes);
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
void display_lines::swap(display_lines& d)
{
    m_lines.swap(d.m_lines);
    std::swap(m_count, d.m_count);
    std::swap(m_vpos, d.m_vpos);
    std::swap(m_cpos, d.m_cpos);
}

//------------------------------------------------------------------------------
void display_lines::clear()
{
    for (unsigned int i = m_count; i--;)
        m_lines[i].clear();
    m_count = 0;
    m_vpos = 0;
    m_cpos = 0;
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
display_line* display_lines::next_line(unsigned int start)
{
    if (m_count >= m_lines.size())
        m_lines.emplace_back();

    display_line* d = &m_lines[m_count++];
    assert(!d->m_x);
    assert(!d->m_len);
    d->m_start = start;
    return d;
}



//------------------------------------------------------------------------------
class display_manager
{
public:
                        display_manager();
    void                clear();
    void                on_new_line();
    void                end_prompt_lf();
    void                display();
    unsigned int        top() const { return m_top; }

private:
    bool                update_line(int i, const display_line* o, const display_line* d, bool wrapped);

    display_lines       m_next;
    display_lines       m_curr;
    unsigned int        m_top = 0;      // Vertical scrolling; index to top displayed line.
    str_moveable        m_last_prompt_line;
    int                 m_last_prompt_line_width = -1;
    bool                m_last_modmark = false;
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
    m_last_modmark = false;
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
    // If the cursor is the only thing on an otherwise-blank last line,
    // compensate so we don't print an extra CRLF.
    bool unwrap = false;
    const unsigned int count = m_curr.count();
    if (_rl_vis_botlin &&
        m_top + _rl_vis_botlin + 1 == count &&
        m_curr.get(count - 1)->m_len == 0)
    {
        assert(count >= 2);
        _rl_vis_botlin--;
        unwrap = true;
    }
    _rl_move_vert(_rl_vis_botlin);

    // If we've wrapped lines, remove the final xterm line-wrap flag.
    // BUGBUG:  The Windows console is not smart enough to recognize that this
    // means it should not merge the line and the next line when resizing the
    // terminal width.
    if (unwrap && _rl_term_autowrap)
    {
        const display_line* d = m_curr.get(count - 2);
        if (d->m_lastcol + d->m_trail == _rl_screenwidth)
        {
            const int index = _rl_find_prev_mbchar(d->m_chars, d->m_len, MB_FIND_NONZERO);
            const unsigned int len = d->m_len - index;
            const unsigned int wc = measure_cols(d->m_chars + index, len);
            move_to_column(_rl_screenwidth - wc);
            _rl_clear_to_eol(0);
            rl_puts_face_func(d->m_chars + index, d->m_faces + index, len);
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

    if (!rl_display_prompt)
        rl_display_prompt = "";

    // Max number of rows to use when displaying the input line.
    unsigned int max_rows = g_input_rows.get();
    if (!max_rows)
        max_rows = _rl_screenheight;
    max_rows = min<unsigned int>(max_rows, _rl_screenheight);
    max_rows = max<unsigned int>(max_rows, 1);

    // TODO-DISPLAY: _rl_horizontal_scroll_mode.

    // FUTURE: Support defining a region in which to display the input line;
    // configurable left starting column, configurable right ending column, and
    // configurable max row count (with vertical scrolling and optional scroll
    // bar).

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

    // Calculate ending column, accounting for wrapping (including double width
    // characters that don't fit).
    if (forced_display)
    {
        ecma48_state state;
        ecma48_iter iter(prompt, state);
        unsigned int col = 0;
        if (modmark)
            ++col;
        while (const ecma48_code &code = iter.next())
        {
            if (code.get_type() != ecma48_code::type_chars)
                continue;

            str_iter inner_iter(code.get_pointer(), code.get_length());
            while (int c = inner_iter.next())
            {
                const int w = clink_wcwidth(c);
                col += w;
                if (col > _rl_screenwidth)
                    col = 0;
            }
        }
        m_last_prompt_line_width = col;
    }

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    const bool need_update = (!rl_display_fixed || forced_display);

    // Prepare data structures for displaying the input line.
    int new_botlin = _rl_vis_botlin;
    if (need_update)
        m_next.parse(m_last_prompt_line_width, rl_line_buffer, rl_end);
    new_botlin = max<int>(0, m_next.count() - 1);
    new_botlin = min<int>(new_botlin, max_rows - 1);

    // Scroll to keep cursor in view.
    const unsigned int old_top = m_top;
    if (m_next.vpos() < m_top)
        m_top = m_next.vpos();
    if (m_next.vpos() > m_top + new_botlin)
        m_top = m_next.vpos() - new_botlin;
    if (m_top + new_botlin + 1 > m_next.count())
        m_top = m_next.count() - 1 - new_botlin;
    if (m_top > 0 && m_top == m_next.vpos())
    {
        const display_line* d = m_next.get(m_top);
        if (m_next.cpos() == d->m_x)
            m_top--;
    }
    else if (m_top + new_botlin + 1 < m_next.count() && m_top + new_botlin == m_next.vpos())
    {
        if (m_next.cpos() + 1 == _rl_screenwidth)
            m_top++;
    }
    m_next.apply_scroll_indicators(m_top, m_top + new_botlin);

    // Display the last line of the prompt.
    if (m_top == 0 && (forced_display || old_top != m_top))
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

        dbg_ignore_scope(snapshot, "display_readline");

        m_last_prompt_line = prompt;
        m_last_modmark = modmark;
    }

    // Optimization:  can skip updating the display if someone said it's already
    // updated, unless someone is forcing an update.
    bool can_show_rprompt = false;
    if (!rl_display_fixed || forced_display)
    {
        // If the right side prompt is shown but shouldn't be, erase it.
        can_show_rprompt = m_next.can_show_rprompt();
        if (_rl_rprompt_shown_len && !can_show_rprompt)
        {
            assert(_rl_last_v_pos == 0);
            tputs_rprompt(0);
        }

        // Update each display line for the line buffer.
        bool wrapped = false;
        unsigned int rows = 0;
        for (unsigned int i = m_top; auto d = m_next.get(i); ++i)
        {
            if (rows++ > new_botlin)
                break;

            auto o = m_curr.get(i - m_top + old_top);
            wrapped = update_line(i, o, d, wrapped);
        }

        _rl_cr();
        _rl_last_c_pos = 0;

        // Erase any surplus lines and update the bottom line counter.
        for (int i = new_botlin; i++ < _rl_vis_botlin;)
        {
            _rl_move_vert(i);
            // BUGBUG: assumes _rl_term_clreol.
            _rl_clear_to_eol(_rl_screenwidth);
        }

        // Update current cursor position.
        assert(_rl_last_c_pos < _rl_screenwidth);

        // Finally update the bottom line counter.
        _rl_vis_botlin = new_botlin;
    }

    // Move cursor to the rl_point position.
    _rl_move_vert(m_next.vpos() - m_top);
    move_to_column(m_next.cpos());

    rl_fflush_function(_rl_out_stream);

    m_next.swap(m_curr);
    m_next.clear();

    rl_display_fixed = 0;

    // If the right side prompt is not shown and should be, display it.
    if (!_rl_rprompt_shown_len && can_show_rprompt)
        tputs_rprompt(rl_rprompt);

    RL_UNSETSTATE(RL_STATE_REDISPLAYING);
    _rl_release_sigint();
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
        if (oc == oc2 && dc == dc2)
            return false;

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
        unsigned int dcols = measure_cols(dc, dlen);
        rcol = lcol + dcols;
        if (oc2 < o->m_chars + o->m_len)
        {
            unsigned int ocols = measure_cols(oc, olen);
            delta = dcols - ocols;
        }

#ifdef DEBUG
        if (dbg_get_env_int("DEBUG_DISPLAY"))
        {
            dbg_printf_row(-1, "delta %d; len %d/%d; col %d/%d; ind %d/%d\r\n", delta, olen, dlen, lcol, rcol, lind, rind);
            dbg_printf_row(-1, "old=[%*s]\toface='[%*s]'\r\n", olen, oc, olen, of);
            dbg_printf_row(-1, "new=[%*s]\tdface=='[%*s]'\r\n", dlen, dc, dlen, df);
        }
#endif
    }

    assert(i >= m_top);
    const unsigned int row = i - m_top;
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
    s_use_display_manager = dbg_get_env_int("USE_DISPLAY_MANAGER", true);
#endif
    return s_use_display_manager;
}

//------------------------------------------------------------------------------
extern "C" void host_on_new_line()
{
    if (use_display_manager())
        s_display_manager.on_new_line();
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
    static const char* const termcap_cd = tgetstr("cd", nullptr);
    rl_fwrite_function(_rl_out_stream, termcap_cd, strlen(termcap_cd));
    if (use_display_manager())
        s_display_manager.clear();
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
unsigned int get_readline_display_top()
{
    if (use_display_manager())
        return s_display_manager.top();
    return 0;
}
