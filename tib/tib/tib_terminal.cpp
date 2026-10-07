// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_terminal.h"
#include "tib_termcap.h"
#include "wcwidth.h"
#include <assert.h>

namespace tib {

bool g_coalesce_output = true;

hook_new_terminal_in_func_t hook_new_terminal_in = nullptr;
hook_new_terminal_out_func_t hook_new_terminal_out = nullptr;

struct macro_playback
{
    cstring             m_text;
    size_t              m_index = 0;
    macro_playback*     m_next = nullptr;
};

static pushed_input s_pushed;
static macro_playback* s_macro_playback = nullptr;

static terminal_in* s_terminal_in = nullptr;
static terminal_out* s_terminal_out = nullptr;
static terminal_out* s_terminal_old_out = nullptr;
static int32_t s_term_began = 0;
static bool s_term_zombie = false;

#ifdef _WIN32
#ifdef DEBUG
DWORD GetMainThreadId()
{
    static const DWORD c_idMainThread = GetCurrentThreadId();
    return c_idMainThread;
}
#endif
#endif

terminal_in* new_basic_terminal_in(pushed_input& pushed);
terminal_out* new_basic_terminal_out();

pushed_input::~pushed_input() noexcept
{
    tib_free(m_data);
}

bool pushed_input::push(uint8_t c) noexcept
{
    if (m_high_surrogate && !push_invalid())
        return false;

    if (!ensure_capacity(1))
        return false;

    m_data[(m_head + m_count) % m_size] = c;
    ++m_count;
    return true;
}

bool pushed_input::push(const char* text, size_t len) noexcept
{
    len = resolve_auto_length(len, text);
    if (!len)
        return true;
    if (!ensure_capacity(len))
        return false;

    const size_t offset = (m_head + m_count) % m_size;
    const size_t first = min(len, m_size - offset);
    memcpy(m_data + offset, text, first);
    if (len > first)
        memcpy(m_data, text + first, len - first);
    m_count += len;
    return true;
}

bool pushed_input::push_front(const char* text, size_t len) noexcept
{
    if (!len)
        return true;
    if (!ensure_capacity(len))
        return false;

    const size_t offset = len % m_size; // In case len == m_size.
    m_head = (m_head >= offset) ? m_head - offset : m_size - (offset - m_head);
    const size_t first = min(len, m_size - m_head);
    memcpy(m_data + m_head, text, first);
    if (len > first)
        memcpy(m_data, text + first, len - first);
    m_count += len;
    return true;
}

#ifdef _WIN32
int32_t pushed_input::push_utf16(WCHAR c) noexcept
{
    // If c is a high surrogate then cache it for later.
    if (IS_HIGH_SURROGATE(c))
    {
        const int32_t pushed = m_high_surrogate ? push_invalid() : -1;
        m_high_surrogate = c; // After push_invalid() because that clears it.
        return pushed;
    }

    // If a high surrogate is cached then complete it.
    if (m_high_surrogate)
    {
        if (!IS_LOW_SURROGATE(c))
        {
            if (!push_invalid())
                return false;

convert_c:
            if (!to_utf8(&c, 1, m_tmp_utf8))
                return false;

push_utf8:
            if (!ensure_capacity(m_tmp_utf8.length()))
                return false;
            for (size_t i = 0; i < m_tmp_utf8.length(); ++i)
                push(m_tmp_utf8.c_str()[i]);
            return true;
        }

        WCHAR convert[2];
        convert[0] = m_high_surrogate;
        convert[1] = c;
        m_high_surrogate = 0;
        if (!to_utf8(convert, 2, m_tmp_utf8))
            return false;
        goto push_utf8;
    }

    goto convert_c;
}

int32_t pushed_input::push_key_event(const KEY_EVENT_RECORD& record) noexcept
{
    // Returns:
    //  -1  =   The first iteration hit an error.  Execution was aborted.
    //  0   =   No error, but all iterations pushed nothing.
    //  1   =   Something was pushed.  All preceding iterations returned 1,
    //          regardless what the last iteration returned.
    int32_t pushed = 0;
    for (WORD count = record.wRepeatCount; count; --count)
    {
        const int32_t result = push_utf16(record.uChar.UnicodeChar);
        if (result <= 0)
            return pushed > 0 ? pushed : result;
        pushed = result;
    }
    return pushed;
}
#endif

bool pushed_input::push_invalid() noexcept
{
    m_high_surrogate = 0;
    return push(0xef) && push(0xbf) && push(0xbd);
}

int32_t pushed_input::peek() const noexcept
{
    assert(!empty());
    const uint8_t c = m_data[m_head];
    return c;
}

int32_t pushed_input::read() noexcept
{
    assert(!empty());
    const uint8_t c = m_data[m_head];
    ++m_head;
    --m_count;
    m_head %= m_size;
    return c;
}

bool pushed_input::ensure_capacity(size_t num) noexcept
{
    if (m_size - m_count >= num)
        return true;

    if (num > size_t(-1) - m_count)
        return false;
    const size_t required = m_count + num;

#ifdef DEBUG
    constexpr size_t min_size = 1;
#else
    constexpr size_t min_size = 128;
#endif

    size_t new_size = m_size;
    if (m_size <= size_t(-1) - m_size / 2)
        new_size += m_size / 2;
    if (new_size < min_size)
        new_size = min_size;
    if (new_size < required)
        new_size = required;

    uint8_t* const data = static_cast<uint8_t*>(tib_malloc(new_size));
    if (!data)
        return false;

    const size_t first = min(m_count, m_size - m_head);
    if (first)
        memcpy(data, m_data + m_head, first);
    if (m_count > first)
        memcpy(data + first, m_data, m_count - first);

    tib_free(m_data);
    m_data = data;
    m_size = new_size;
    m_head = 0;
    return true;
}

void term_begin()
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began >= 0);
    if (s_term_began < 0)
    {
        assert(!s_terminal_in);
        assert(!s_terminal_out);
        assert(!s_terminal_old_out);
        s_term_began = 0;
    }

    if (!s_term_began)
    {
        static bool s_init_wcwidths = true;
        if (s_init_wcwidths)
        {
            s_init_wcwidths = false;
            reset_wcwidths();
        }

        assert(!s_terminal_in);
        assert(!s_terminal_out);
        assert(!s_terminal_old_out);
        s_terminal_in = hook_new_terminal_in ? hook_new_terminal_in(s_pushed) : new_basic_terminal_in(s_pushed);
        s_terminal_out = hook_new_terminal_out ? hook_new_terminal_out() : new_basic_terminal_out();
    }

    ++s_term_began;
}

void term_end()
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began > 0);
    if (s_term_began <= 0)
        return;

    if (s_term_began == 1)
    {
        display_accumulator coalesce;

        term_out(c_show_cursor);
        // FUTURE: cursor shape.
        term_out("\x1b[m");

        enable_mouse_input(mouse_input_mode::none, false);

        // Flush before nulling the globals.
        coalesce.flush();

        if (s_terminal_old_out)
            term_redirect(nullptr);

        delete s_terminal_in;
        s_terminal_in = nullptr;
        delete s_terminal_out;
        s_terminal_out = nullptr;
    }

    --s_term_began;
}

bool term_redirect(terminal_out* redirect)
{
    if (s_terminal_out)
    {
        if (s_terminal_old_out && !redirect)
        {
            s_terminal_out = s_terminal_old_out;
            s_terminal_old_out = nullptr;
            return true;
        }
        else if (!s_terminal_old_out && redirect)
        {
            s_terminal_old_out = s_terminal_out;
            s_terminal_out = redirect;
            return true;
        }
    }
    return false;
}

void term_sigint()
{
    if (s_term_began)
    {
        s_term_began = 1;
        term_end();
    }
}

#ifdef _WIN32
void term_sigclose()
{
    s_term_zombie = true;
}

bool is_term_sigclose()
{
    return s_term_zombie;
}
#endif

class auto_term_end
{
public:
    ~auto_term_end() { if (!s_term_zombie) term_sigint(); }
};
static auto_term_end s_auto_term_end;

int32_t term_in()
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began);
    if (!s_terminal_in)
        return c_input_terminal_eof;

    if (!s_pushed.empty())
        return s_pushed.read();

    if (s_macro_playback)
    {
        assert(s_macro_playback->m_index < s_macro_playback->m_text.length());
        const char c = s_macro_playback->m_text.c_str()[s_macro_playback->m_index++];
        if (s_macro_playback->m_index >= s_macro_playback->m_text.length())
        {
            macro_playback* d = s_macro_playback;
            s_macro_playback = s_macro_playback->m_next;
            delete d;
        }
        return uint8_t(c);
    }

    const int32_t c = s_terminal_in->read();
    assert(c < 0 || !(c & 0xffffff00));
    return c;
}

int32_t term_in_peek()
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began);
    if (!s_terminal_in)
        return c_input_terminal_eof;

    if (!s_pushed.empty())
        return s_pushed.peek();

    if (s_macro_playback)
    {
        assert(s_macro_playback->m_index < s_macro_playback->m_text.length());
        const char c = s_macro_playback->m_text.c_str()[s_macro_playback->m_index];
        return uint8_t(c);
    }

    if (!term_in_avail())
        return -1;

    // term_in_avail() can queue multiple UTF8 bytes for one UTF16 input
    // character.  Return the head in place; reading and pushing it back would
    // rotate the queued bytes.
    if (!s_pushed.empty())
        return s_pushed.peek();

    assert(!s_macro_playback);

    const int32_t c = term_in();
    if (c < 0)
        return c;
    assert(!(c & 0xffffff00));

    s_pushed.push(uint8_t(c));
    return c;
}

bool term_in_avail(const DWORD _timeout)
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began);
    if (!s_terminal_in)
        return false;

    if (!s_pushed.empty())
        return true;
    if (s_macro_playback)
        return true;

    return s_terminal_in->avail(_timeout);
}

bool term_push_input(const char* text, size_t len)
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    len = resolve_auto_length(len, text);
    return s_pushed.push_front(text, len);
}

bool term_push_macro_text(const char* text, size_t len)
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    macro_playback* m = new macro_playback;
    if (!m)
        return false;

    if (!m->m_text.set(text, len))
    {
        delete m;
        return false;
    }

    m->m_next = s_macro_playback;
    s_macro_playback = m;
    return true;
}

bool enable_mouse_input(mouse_input_mode mode, bool sgr_encoding)
{
#ifdef _WIN32
#ifdef DEBUG
    assert(GetMainThreadId() == GetCurrentThreadId());
#endif
#endif

    assert(s_term_began);
    if (!s_term_began || !s_terminal_in)
        return false;

    return s_terminal_in->enable_mouse_input(mode, sgr_encoding);
}

void term_out(const char* s, size_t len)
{
    if (!s_terminal_out)
        return;

    len = resolve_auto_length(len, s);

    if (display_accumulator::active())
        display_accumulator::append(s, len);
    else
        s_terminal_out->write(s, len);
}

void ding()
{
    if (s_terminal_out)
        s_terminal_out->ding();
}

int32_t display_accumulator::s_nested = 0;
bool display_accumulator::s_can_synchronize_output = false;
bool display_accumulator::s_active = false;
bool display_accumulator::s_synchronized_output = false;
static tib::cstring s_acc;

display_accumulator::display_accumulator()
{
    if (!s_nested)
    {
        assert(!s_active);
        assert(!s_synchronized_output);
        assert(s_acc.empty());
        s_acc.empty();
    }

    assert(m_active);
    ++s_nested;

    if (s_nested == 1)
    {
        if (!g_coalesce_output)
            return;

        s_synchronized_output = s_can_synchronize_output;
        s_active = true;

        if (s_synchronized_output)
            s_acc.append("\x1b[2026h");
    }
}

display_accumulator::~display_accumulator()
{
    end();
}

void display_accumulator::end()
{
    if (!m_active)
        return;

    m_active = false;
    --s_nested;

    if (s_active && s_nested == 0)
    {
        cancel();
        assert(!s_active);
        assert(!s_synchronized_output);
        assert(s_acc.empty());
    }
}

void display_accumulator::cancel()
{
    if (s_active)
    {
        flush();
        s_active = false;
        s_synchronized_output = false;
    }
}

void display_accumulator::flush()
{
    if (!s_active)
    {
        assert(implies(!s_active, s_acc.empty()));
        return;
    }

    static int32_t s_in_flush = 0;
    if (s_active && s_in_flush <= 0)
    {
        ++s_in_flush;
        assert(s_in_flush <= 3);
        if (s_synchronized_output)
        {
            if (s_acc.equals("\x1b[2026h"))
                s_acc.clear();
            else
                s_acc.append("\x1b[2026l");
        }
        if (!s_acc.empty())
        {
            s_active = false;
            term_out(s_acc.c_str(), s_acc.length());
            s_acc.clear();
            s_active = true;
        }
        if (s_nested > 0 && s_synchronized_output)
            s_acc.append("\x1b[2026h");
        assert(s_in_flush > 0);
        --s_in_flush;
    }
}

void display_accumulator::append(const char* s, size_t len)
{
    s_acc.append(s, len);
}

#ifdef _WIN32
// In some versions of Windows, the legacy console doesn't handle CSI K
// correctly when the visible area is not the full console width.
bool preserve_window_horiz_scroll_position::s_safe_clreol_when_horiz_scrolled = false;
int32_t preserve_window_horiz_scroll_position::s_nested = 0;
HANDLE preserve_window_horiz_scroll_position::s_h = nullptr;
CONSOLE_SCREEN_BUFFER_INFO preserve_window_horiz_scroll_position::s_window;

preserve_window_horiz_scroll_position::preserve_window_horiz_scroll_position(HANDLE h)
{
    assert(implies(!s_nested, !s_h));
    ++s_nested;
    if (!s_h && h)
    {
        s_h = h;
        display_accumulator::flush();
        GetConsoleScreenBufferInfo(s_h, &s_window);
    }
}

preserve_window_horiz_scroll_position::~preserve_window_horiz_scroll_position()
{
    assert(s_nested > 0);
    if (s_h)
    {
        display_accumulator::flush();
        CONSOLE_SCREEN_BUFFER_INFO cursor;
        GetConsoleScreenBufferInfo(s_h, &cursor);
        if (cursor.srWindow.Right - cursor.srWindow.Left == s_window.srWindow.Right - s_window.srWindow.Left &&
            cursor.srWindow.Bottom - cursor.srWindow.Top == s_window.srWindow.Bottom - s_window.srWindow.Top &&
            cursor.srWindow.Left != s_window.srWindow.Left &&
            cursor.dwCursorPosition.Y >= s_window.srWindow.Top &&
            cursor.dwCursorPosition.Y <= s_window.srWindow.Bottom)
        {
            // Only restore the horizontal scroll position.  If the vertical
            // scroll position is also restored, then this interferes with
            // text output scrolling the terminal vertically when it goes past
            // the bottom of the visible window.
            const SHORT currentLeft = cursor.srWindow.Left;
            SHORT delta = 0;
            cursor.srWindow.Left = s_window.srWindow.Left;
            cursor.srWindow.Right = s_window.srWindow.Right;
            if (cursor.dwCursorPosition.X < cursor.srWindow.Left)
                delta = cursor.dwCursorPosition.X - cursor.srWindow.Left;
            else if (cursor.dwCursorPosition.X > cursor.srWindow.Right)
                delta = cursor.dwCursorPosition.X - cursor.srWindow.Right;
            cursor.srWindow.Left += delta;
            cursor.srWindow.Right += delta;
            if (cursor.srWindow.Left != currentLeft)
                SetConsoleWindowInfo(s_h, true, &cursor.srWindow);
        }
    }
    --s_nested;
    if (!s_nested)
        s_h = nullptr;
}

HANDLE is_horizpos_workaround_needed()
{
    if (is_test_harness())
        return nullptr;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return nullptr;
    if (csbi.srWindow.Left == 0 && csbi.srWindow.Right == csbi.dwSize.X - 1)
        return nullptr;
    return h;
}
#endif

} // namespace tib
