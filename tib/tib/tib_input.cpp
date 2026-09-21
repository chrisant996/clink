// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_terminal.h"
#include "tib_termcap.h"
#include <memory>
#include <conio.h>
#include <assert.h>

namespace tib {

extern void custom_vt_driver(const KEY_EVENT_RECORD& record, pushed_input& pushed);

static mouse_input_mode s_mouse_input_mode = mouse_input_mode::none;
static bool s_mouse_sgr_encoding = true;
static DWORD s_prev_input_mode = 0;
static DWORD s_prev_output_mode = 0;
static DWORD s_prev_mouse_button_state = 0;
static coord s_last_term_size { -1, -1 };
static cstring s_tmp_utf8;

class basic_terminal_in : public terminal_in
{
public:
                        ~basic_terminal_in();
                        basic_terminal_in(pushed_input& pushed);

    int32_t             read() noexcept override;
    bool                avail(uint32_t timeout=0) noexcept override;
    bool                enable_mouse_input(mouse_input_mode mode, bool sgr_encoding) noexcept override;

protected:
#ifdef _WIN32
    int32_t             read_redirected() noexcept;
#endif

protected:
    pushed_input&       m_pushed;

#ifdef _WIN32
    HANDLE              m_hin = 0;
    HANDLE              m_hout = 0;
    bool                m_is_console = false;
    bool                m_use_custom_vt_driver = false;
#endif
};

terminal_in* new_basic_terminal_in(pushed_input& pushed)
{
    return new basic_terminal_in(pushed);
}

#ifdef _WIN32
static bool need_custom_vt_driver()
{
#pragma warning(push)
#pragma warning(disable:4996)
    OSVERSIONINFO ver = {sizeof(ver)};
    if (GetVersionEx(&ver))
        return ver.dwMajorVersion < 10;
    return false;
#pragma warning(pop)
}

static bool is_invalid_keyevent(KEY_EVENT_RECORD& record)
{
    // Only respond to key down events.
    if (!record.bKeyDown)
    {
        // WARNING:  Some times conhost can send through ALT codes, with the
        // resulting Unicode code point in the Alt key-up event.  I don't know
        // whether that also happens when ENABLE_VIRTUAL_TERMINAL_PROCESSING
        // is present, but I think that would be a console bug, and not
        // something for this to attempt to mitigate.
        return true;
    }

    // Ignore unaccompanied Alt/Ctrl/Shift/Windows key presses.  These can
    // happen even when ENABLE_VIRTUAL_TERMINAL_PROCESSING is present.
    switch (record.wVirtualKeyCode)
    {
    case VK_MENU:
    case VK_CONTROL:
    case VK_SHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    }

    return false;
}

static bool append_one_mouse_sequence(uint8_t flags, uint16_t x, uint16_t y, bool release, cstring& out)
{
    if (s_mouse_sgr_encoding)
    {
        // SGR Encoding.
        const char op = release ? 'm' : 'M';
        out.printf("\x1b[<%u;%u;%u%c", flags, x, y, op);
        return true;
    }
    else
    {
        // DEFAULT Encoding.
        if (release)
            flags = uint8_t((flags & 0x1c) | 3); // Generic release.
        const uint8_t Cb = uint8_t(0x20 + flags);
        const uint8_t Cx = (x < 1 || x > 223) ? 0 : uint8_t(x + 0x20);
        const uint8_t Cy = (y < 1 || y > 223) ? 0 : uint8_t(y + 0x20);
        out.append("\x1b[M");
        out.append(reinterpret_cast<const char*>(&Cb), 1);
        out.append(reinterpret_cast<const char*>(&Cx), 1);
        out.append(reinterpret_cast<const char*>(&Cy), 1);
        return true;
    }

    return false;
}

static bool generate_mouse_sequences(const MOUSE_EVENT_RECORD& record, cstring& out)
{
    // The implementation here supports these protocols:  VT200, DRAG, ANY.
    // It does not support SGR Pixels Encoding.
    //
    // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Mouse-Tracking

    out.clear();

    // Remember the button state, to differentiate press vs release.
    const auto prv = s_prev_mouse_button_state;
    s_prev_mouse_button_state = record.dwButtonState;

    // Xterm generates a separate sequence per button event.  Attempt to
    // emulate that reasonably well.
    const auto btn = record.dwButtonState;
    const bool left_held = !!(btn & FROM_LEFT_1ST_BUTTON_PRESSED);
    const bool middle_held = !!(btn & FROM_LEFT_2ND_BUTTON_PRESSED);
    const bool right_held = !!(btn & RIGHTMOST_BUTTON_PRESSED);
    const bool left_button_change = left_held != !!(prv & FROM_LEFT_1ST_BUTTON_PRESSED);
    const bool middle_button_change = middle_held != !!(prv & FROM_LEFT_2ND_BUTTON_PRESSED);
    const bool right_button_change = right_held != !!(prv & RIGHTMOST_BUTTON_PRESSED);
    const bool wheel = !!(record.dwEventFlags & MOUSE_WHEELED) && (s_mouse_input_mode >= mouse_input_mode::VT200);
    const bool hwheel = !!(record.dwEventFlags & MOUSE_HWHEELED) && (s_mouse_input_mode >= mouse_input_mode::VT200);

    constexpr DWORD ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;
    constexpr DWORD CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;

    uint8_t flags = 0;
    if (record.dwControlKeyState & SHIFT_PRESSED)  flags |= 4;      // MB+S
    if (record.dwControlKeyState & ALT_PRESSED)    flags |= 8;      // MB+A (also MB4)
    if (record.dwControlKeyState & CTRL_PRESSED)   flags |= 16;     // MB+C

    const uint16_t x = record.dwMousePosition.X + 1;
    const uint16_t y = record.dwMousePosition.Y + 1;

    bool ret = false;

    // Left click/release.
    if (left_button_change)
        ret |= append_one_mouse_sequence(flags + 0, x, y, !left_held, out); // MB1

    // Middle click/release.
    if (middle_button_change)
        ret |= append_one_mouse_sequence(flags + 1, x, y, !middle_held, out); // MB2

    // Right click/release.
    if (right_button_change)
        ret |= append_one_mouse_sequence(flags + 2, x, y, !right_held, out); // MB3

    // Mouse wheel.
    if (wheel)
    {
        const auto direction = int16_t(0 - int16_t(HIWORD(record.dwButtonState))) / 120;
        ret |= append_one_mouse_sequence(flags + ((direction < 0) ? 64 : 65), x, y, false, out); // MB4/MB5
    }

    // Mouse horizontal wheel.
    if (hwheel)
    {
        const auto direction = int16_t(int16_t(HIWORD(record.dwButtonState)) / 120);
        ret |= append_one_mouse_sequence(flags + ((direction < 0) ? 66 : 67), x, y, false, out); // MB6/MB7
    }

    // Drag.
    if (!ret && (record.dwEventFlags & MOUSE_MOVED))
    {
        if ((s_mouse_input_mode == mouse_input_mode::ANY) ||
            (s_mouse_input_mode == mouse_input_mode::DRAG && (left_held || middle_held || right_held)))
        {
            uint8_t drag_flags = (flags & 0x1c) | 32;               // Motion
            if (left_held)         drag_flags += 0;                 // MB1
            else if (middle_held)  drag_flags += 1;                 // MB2
            else if (right_held)   drag_flags += 2;                 // MB3
            else                   drag_flags += 3;                 // None
            ret |= append_one_mouse_sequence(drag_flags, x, y, false, out);
        }
    }

    return ret;
}
#endif // _WIN32

basic_terminal_in::~basic_terminal_in()
{
#ifdef _WIN32
    if (m_hin && m_is_console)
        SetConsoleMode(m_hin, s_prev_input_mode);
    if (m_hout)
        SetConsoleMode(m_hout, s_prev_output_mode);

    m_hin = 0;
    m_hout = 0;
#endif
}

basic_terminal_in::basic_terminal_in(pushed_input& pushed)
: m_pushed(pushed)
{
#ifdef _WIN32
    m_hin = GetStdHandle(STD_INPUT_HANDLE);
    m_is_console = !!GetConsoleMode(m_hin, &s_prev_input_mode);
    if (m_is_console)
    {
        s_prev_mouse_button_state = 0;
        if (GetKeyState(VK_LBUTTON) < 0)
            s_prev_mouse_button_state |= FROM_LEFT_1ST_BUTTON_PRESSED;
        if (GetKeyState(VK_MBUTTON) < 0)
            s_prev_mouse_button_state |= FROM_LEFT_2ND_BUTTON_PRESSED;
        if (GetKeyState(VK_RBUTTON) < 0)
            s_prev_mouse_button_state |= RIGHTMOST_BUTTON_PRESSED;
        m_use_custom_vt_driver = (need_custom_vt_driver() || !(s_prev_input_mode & ENABLE_VIRTUAL_TERMINAL_INPUT));
    }

    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(hout, &s_prev_output_mode))
        m_hout = hout;
#endif

    s_last_term_size = get_terminal_size();
}

#ifdef _WIN32
int32_t basic_terminal_in::read_redirected() noexcept
{
    if (!m_hin || m_hin == INVALID_HANDLE_VALUE)
        return c_input_terminal_eof;

    uint8_t c;
    DWORD num_read = 0;
    if (!ReadFile(m_hin, &c, 1, &num_read, nullptr) || !num_read)
        return c_input_terminal_eof;
    return c;
}
#endif

int32_t basic_terminal_in::read() noexcept
{
#ifdef _WIN32
    // Read redirected stdin as UTF8 bytes.  FUTURE: let the host control
    // stdin decoding (i.e. ACP or UTF8).
    if (!m_is_console)
        return read_redirected();
#endif

    const coord term_size = get_terminal_size();
    if (s_last_term_size != term_size)
    {
        s_last_term_size = term_size;
        return uint8_t(c_input_terminal_resize);
    }

#ifdef _WIN32
again:
    DWORD num_read;
#define USE_READCONSOLEINPUT
#ifdef USE_READCONSOLEINPUT
    // FUTURE: add a mode that conditionally applies/removes
    // ENABLE_MOUSE_INPUT like Clink does?
    cstring seq;
    INPUT_RECORD record;
    if (!ReadConsoleInputW(m_hin, &record, 1, &num_read) || 1 != num_read)
        return -1;
    switch (record.EventType)
    {
    case KEY_EVENT:
        if (is_invalid_keyevent(record.Event.KeyEvent))
            goto again;
        if (m_use_custom_vt_driver)
        {
            custom_vt_driver(record.Event.KeyEvent, m_pushed);
            if (m_pushed.empty())
                goto again;
            return m_pushed.read();
        }
        break;
    case MOUSE_EVENT:
        if (generate_mouse_sequences(record.Event.MouseEvent, seq))
        {
            for (size_t i = 0; i < seq.length(); ++i)
                m_pushed.push(seq.c_str()[i]);
            if (m_pushed.empty())
                goto again;
            return m_pushed.read();
        }
        goto again;
    case WINDOW_BUFFER_SIZE_EVENT:
        {
            const coord term_size = get_terminal_size();
            if (s_last_term_size != term_size)
            {
                s_last_term_size = term_size;
                return c_input_terminal_resize;
            }
        }
        goto again;
    default:
        assert(false);
    case MENU_EVENT:
    case FOCUS_EVENT:
        goto again;
    }
    assert(record.EventType == KEY_EVENT);
    const int32_t pushed = m_pushed.push_key_event(record.Event.KeyEvent);
#else
    WCHAR tmp;
    if (!ReadConsoleW(h, &tmp, 1, &num_read, nullptr) || 1 != num_read)
        return -1;
    const int32_t pushed = m_pushed.push_utf16(tmp);
#endif
    // Zero indicates that no byte was queued, so there is nothing to read.
    if (pushed <= 0)
        goto again;
    return m_pushed.read();
#else
    // TODO-LINUX: Use fgetc?
    // TODO-LINUX: What to do upon EOF?
#endif
}

bool basic_terminal_in::avail(const uint32_t _timeout) noexcept
{
#ifdef _WIN32
    if (!m_is_console)
    {
        if (!m_hin || m_hin == INVALID_HANDLE_VALUE)
            return true;

        if (GetFileType(m_hin) == FILE_TYPE_PIPE)
        {
            const DWORD stop = GetTickCount() + _timeout;
            for (;;)
            {
                DWORD available = 0;
                if (!PeekNamedPipe(m_hin, nullptr, 0, nullptr, &available, nullptr))
                    return true; // Let read() report EOF.
                if (available)
                    return true;

                DWORD timeout = stop - GetTickCount();
                if (timeout > _timeout)
                    return false;
                const DWORD sleep = min<DWORD>(timeout, 10);
                if (!sleep)
                    return false;
                Sleep(sleep);
            }
        }

        // Disk files are always ready, and other redirected handle types do
        // not have a general non-blocking availability API.  Read one byte
        // ahead and preserve it in the shared pushed-input queue.
        assert(m_pushed.empty());
        const int32_t c = read_redirected();
        if (c < 0 || !m_pushed.push(uint8_t(c)))
            return false;
        return true;
    }
#endif

    const coord term_size = get_terminal_size();
    if (s_last_term_size != term_size)
    {
        s_last_term_size = term_size;
        m_pushed.push(c_input_terminal_resize);
        return true;
    }

#ifdef _WIN32
    bool ret = !m_pushed.empty();
    bool sleep_on_error = false;
    const DWORD stop = GetTickCount() + _timeout;
    while (!ret)
    {
        DWORD timeout = stop - GetTickCount();
        if (timeout > _timeout)
            timeout = 0;

        if (sleep_on_error)
        {
            sleep_on_error = false;
            const DWORD sleep = (timeout > 1000) ? timeout - 1000 : timeout;
            Sleep(sleep);
            timeout -= sleep;
        }

        const DWORD waited = WaitForSingleObject(m_hin, timeout);
        if (waited == WAIT_TIMEOUT)
            break;

        DWORD count;
        cstring seq;
        INPUT_RECORD record;
        if (!ReadConsoleInputW(m_hin, &record, 1, &count) || 1 != count)
        {
            // Handle's probably invalid if ReadConsoleInput() failed.
            sleep_on_error = true;
            continue;
        }

        switch (record.EventType)
        {
        case KEY_EVENT:
            if (is_invalid_keyevent(record.Event.KeyEvent))
                break;
            if (m_use_custom_vt_driver)
            {
                custom_vt_driver(record.Event.KeyEvent, m_pushed);
                ret = !m_pushed.empty();
            }
            else
            {
                const int32_t pushed = m_pushed.push_key_event(record.Event.KeyEvent);
                if (pushed < 0)
                    continue;
                ret = (pushed > 0);
            }
            break;

        case MOUSE_EVENT:
            if (generate_mouse_sequences(record.Event.MouseEvent, seq))
            {
                for (size_t i = 0; i < seq.length(); ++i)
                    m_pushed.push(seq.c_str()[i]);
                ret = !m_pushed.empty();
            }
            break;

        case WINDOW_BUFFER_SIZE_EVENT:
#ifdef USE_READCONSOLEINPUT
            if (m_pushed.push(c_input_terminal_resize))
                ret = true;
#else
            // REVIEW: can't really do anything with this unless term_in()
            // also uses ReadConsoleInputW().
#endif
            break;
        }

        if (!timeout)
            break;
    }
#else
    // TODO-LINUX: Alternative Linux implementation.
    ret = false;
#endif
    return ret;
}

bool basic_terminal_in::enable_mouse_input(mouse_input_mode mode, bool sgr_encoding) noexcept
{
    // https://tintin.mudhalla.net/info/xterm/
    //
    // XTERM MOUSE TRACKING
    //
    // Code             Effect  Note
    // ----             ------  ----
    // CSI ? 1000 h     MTM     Enable Mouse Tracking Mode (VT200 Protocol; press, release, wheel)
    // CSI ? 1001 h     HMTM    Set Highlight Mouse Tracking Mode
    // CSI ? 1002 h     BMMTM   Set Button Motion Mouse Tracking Mode (DRAG Protocol; press, release, wheel, drag while button down)
    // CSI ? 1003 h             (ANY Protocol; all mouse events)
    // CSI ? 1004 h     WFTM    Enable Window Focus Tracking Mode
    // CSI ? 1006 h     DMTM    Set Decimal Mouse Tracking Mode (SGR Encoding)
    // CSI ? 1016 h             (SGR Pixels Encoding)
    //
    // NOTE:  The Win32 implementation in generate_mouse_sequences() supports
    // these protocols:  VT200, DRAG, ANY.  It supports both DEFAULT and SGR
    // encodings for each protocol.  It does not support SGR Pixels encoding
    // or Window Focus Tracking Mode.

#ifdef _WIN32
    DWORD old_console_mode;
    if (m_hin && GetConsoleMode(m_hin, &old_console_mode) &&
        !(old_console_mode & ENABLE_VIRTUAL_TERMINAL_INPUT))
    {
        DWORD new_console_mode = old_console_mode;
        new_console_mode &= ~(ENABLE_MOUSE_INPUT|ENABLE_QUICK_EDIT_MODE);
        if (mode != mouse_input_mode::none)
            new_console_mode |= ENABLE_MOUSE_INPUT;
        else
            new_console_mode |= (s_prev_input_mode & ENABLE_QUICK_EDIT_MODE);
        if (old_console_mode != new_console_mode)
            SetConsoleMode(m_hin, new_console_mode);
    }
    else
#endif
    {
        switch (mode)
        {
        case mouse_input_mode::none:    term_out("\x1b[?1000l\x1b[?1002l\x1b[?1003l"); break;
        case mouse_input_mode::VT200:   term_out("\x1b[?1000h"); break;
        case mouse_input_mode::DRAG:    term_out("\x1b[?1002h"); break;
        case mouse_input_mode::ANY:     term_out("\x1b[?1003h"); break;
        default:                        assert(false); break;
        }

        term_out(sgr_encoding ? "\x1b[?1006h" : "\x1b[?1006l");
    }

    s_mouse_input_mode = mode;
    s_mouse_sgr_encoding = sgr_encoding;

    return true;
}

} // namespace tib
