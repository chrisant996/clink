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
#include "readline/xmalloc.h"
#include "readline/rlprivate.h"

#if defined (COLOR_SUPPORT)
#  include "readline/colors.h"
#endif

#include "hooks.h"

unsigned int cell_count(const char* in);

extern void (*rl_fwrite_function)(FILE*, const char*, int);
extern void (*rl_fflush_function)(FILE*);

extern int tputs(const char* str, int affcnt, int (*putc_func)(int));
extern char* tgoto(char* base, int x, int y);
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
    void                swap(display_lines& d);
    void                clear();

    const display_line* get(unsigned int index) const;
    unsigned int        count() const;
    bool                can_show_rprompt() const;

    unsigned int        vpos() const;
    unsigned int        cpos() const;

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
// TODO-DISPLAY: _rl_wrapped_multicolumn = 0;

                while (col < _rl_screenwidth)
                {
                    d->appendspace();
// TODO-DISPLAY: _rl_wrapped_multicolumn++;
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
void display_lines::swap(display_lines& d)
{
    m_lines.swap(d.m_lines);
    std::swap(m_count, d.m_count);
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
unsigned int display_lines::vpos() const
{
    return m_vpos;
}

//------------------------------------------------------------------------------
unsigned int display_lines::cpos() const
{
    return m_cpos;
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
static void move_horz(int cpos)
{
    assert(cpos < _rl_screenwidth);
    assert(_rl_term_forward_char);

    if (cpos == _rl_last_c_pos)
        return;

    int delta = cpos - _rl_last_c_pos;
    if ((delta < -5 || delta > 5) && _rl_term_ch && *_rl_term_ch)
    {
        char* buffer = tgoto(_rl_term_ch, 0, cpos + 1);
        tputs(buffer, 1, _rl_output_character_function);
    }
    else if (delta < 0)
    {
        // TODO-DISPLAY: some characters use more than 1 column, so this isn't accurate yet.
        _rl_backspace(-delta);
    }
    else
    {
        // TODO-DISPLAY: some characters use more than 1 column, so this isn't accurate yet.
        while (delta-- > 0)
            tputs(_rl_term_forward_char, 1, _rl_output_character_function);
    }

    _rl_last_c_pos = cpos;
}



//------------------------------------------------------------------------------
class display_manager
{
public:
                        display_manager();
    void                end_prompt_lf();
    void                display();

private:
    void                update_line(int i, const display_line* o, const display_line* d);

    display_lines       m_next;
    display_lines       m_curr;
    // TODO-DISPLAY: How to refresh input line display without needing to refresh prompt display?
};

//------------------------------------------------------------------------------
static display_manager s_display_manager;

//------------------------------------------------------------------------------
display_manager::display_manager()
{
    rl_on_new_line();
}

//------------------------------------------------------------------------------
void display_manager::end_prompt_lf()
{
    // TODO-DISPLAY: _rl_update_final: If we've wrapped lines, remove the final xterm line-wrap flag.

    if (_rl_last_c_pos > 0)
    {
do_crlf:
        rl_crlf();
        _rl_last_c_pos = 0;
        rl_fflush_function(_rl_out_stream);
        rl_display_fixed++;
        return;
    }

    if (!m_curr.count())
        return;

    if (m_top + _rl_vis_botlin + 1 < m_curr.count())
    {
        on_new_line();
        goto do_crlf;
    }

    if (m_curr.get(m_curr.count() - 1)->m_len > 0)
        goto do_crlf;
}

//------------------------------------------------------------------------------
void display_manager::display()
{
    if (!_rl_echoing_p)
        return;

    // Block keyboard interrupts because this function manipulates global data
    // structures.
    _rl_block_sigint();
    RL_SETSTATE(RL_STATE_REDISPLAYING);

    if (!rl_display_prompt)
        rl_display_prompt = "";

    // TODO-DISPLAY: _rl_quick_redisplay.
    // TODO-DISPLAY: how to clear m_curr on resize terminal?

    // TODO-DISPLAY: _rl_horizontal_scroll_mode.

    // TODO-DISPLAY: modmark.
    // TODO-DISPLAY: _rl_display_fixed: there's a cryptic check that might be attempting to look at modmark, but if so
    // then it looks inaccurate (overly simplistic `visible_line[0] != invisible_line[0]`; should track modmark).

    // TODO-DISPLAY: how to tell when the prompt needs to be re-analyzed; wasteful when prompt hasn't changed and screen width hasn't changed.

    const char* prompt = rl_get_local_prompt();
    const char* prompt_prefix = rl_get_local_prompt_prefix();

    const bool forced_display = rl_get_forced_display();
    rl_set_forced_display(false);

    if (prompt || rl_display_prompt == rl_prompt)
    {
        if (prompt_prefix && forced_display)
        {
            rl_fwrite_function(_rl_out_stream, prompt_prefix, strlen(prompt_prefix));
        }
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
                /* Make sure we are at column zero even after a newline,
                   regardless of the state of terminal output processing. */
                if (pmtlen < 2 || prompt[-2] != '\r')
                    _rl_cr();
            }
        }
    }

    // Let the application have a chance to do processing; for example to parse
    // the input line and update font faces for the line.
    if (rl_before_display_function)
        rl_before_display_function();

    // Display the last line of the prompt.
    const bool is_message = (rl_display_prompt == rl_get_message_buffer());
    if (forced_display)
    {
        _rl_cr();

        if (is_message)
            rl_fwrite_function(_rl_out_stream, _rl_display_message_color, strlen(_rl_display_message_color));

        rl_fwrite_function(_rl_out_stream, prompt, strlen(prompt));

        if (is_message)
            rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);
    }

    unsigned int col = 0;

    // Calculate ending column, accounting for wrapping (including double
    // width characters that don't fit).
    ecma48_state state;
    ecma48_iter iter(prompt, state);
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

    if (forced_display)
        _rl_last_c_pos = col;

    // Prepare data structures for displaying the input line.
    m_next.parse(col, rl_line_buffer, rl_end);

    // If the right side prompt is shown but shouldn't be, erase it.
    const bool can_show_rprompt = m_next.can_show_rprompt();
    if (_rl_rprompt_shown_len && !can_show_rprompt)
    {
        assert(_rl_last_v_pos == 0);
        tputs_rprompt(0);
    }

    // TODO-DISPLAY: remove faces for anything that's going to be off the top of the display.

    for (unsigned int i = 0; auto d = m_next.get(i); ++i)
    {
        auto o = m_curr.get(i);
        update_line(i, o, d);
    }

    // Erase any surplus lines and update the bottom line counter.
    const int new_botlin = max<int>(0, m_next.count() - 1);
    for (int i = new_botlin; i++ < _rl_vis_botlin;)
    {
        _rl_move_vert(i);
        _rl_cr();
        // BUGBUG: assumes _rl_term_clreol.
        _rl_clear_to_eol(_rl_screenwidth);
        _rl_last_c_pos = 0;
    }

    // Update current cursor position.
    assert(_rl_last_c_pos < _rl_screenwidth);

    // Finally update the bottom line counter.
    _rl_vis_botlin = new_botlin;

    // Move cursor to the rl_point position.
    _rl_move_vert(m_next.vpos());
    move_horz(m_next.cpos());

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
void display_manager::update_line(int i, const display_line* o, const display_line* d)
{
    unsigned int cpos = d->m_x;

    // TODO-DISPLAY: optimize by finding differences between o and d.
    if (o &&
        o->m_len == d->m_len &&
        !memcmp(o->m_chars, d->m_chars, d->m_len) &&
        !memcmp(o->m_faces, d->m_faces, d->m_len))
        return;

    if (i != _rl_last_v_pos)
        _rl_move_vert(i);

    move_horz(d->m_x);

    rl_puts_face_func(d->m_chars, d->m_faces, d->m_len);
    rl_fwrite_function(_rl_out_stream, "\x1b[m", 3);
    _rl_last_c_pos = d->m_lastcol + d->m_trail;

    if (o && d->m_lastcol < o->m_lastcol)
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
        if (_rl_term_autowrap)
        {
            rl_fwrite_function(_rl_out_stream, " ", 1);
            _rl_cr();
        }
        else
        {
            // TODO-DISPLAY: test with/without _rl_term_autowrap.
            rl_crlf();
        }
        _rl_last_v_pos++;
        _rl_last_c_pos = 0;
    }
}

#endif // INCLUDE_CLINK_DISPLAY_READLINE



//------------------------------------------------------------------------------
static bool s_use_display_manager = false;

//------------------------------------------------------------------------------
extern "C" int use_display_manager()
{
#if defined (OMIT_DEFAULT_DISPLAY_READLINE)
    s_use_display_manager = true;
#elif defined (DEBUG)
    s_use_display_manager = dbg_get_env_int("USE_DISPLAY_MANAGER");
#endif
    return s_use_display_manager;
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
extern "C" void display_readline()
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
