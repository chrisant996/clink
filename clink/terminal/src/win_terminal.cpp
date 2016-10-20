// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal.h"

#include <core/array.h>
#include <core/base.h>
#include <core/log.h>
#include <core/settings.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
namespace terminfo {
static const char* const kcuu1[] = { "\x1b[A",  "\x1b[1;2A", "\x1b[1;3A", "\x1b[1;4A", "\x1b[1;5A", "\x1b[1;6A", "\x1b[1;7A", "\x1b[1;8A" };
static const char* const kcud1[] = { "\x1b[B",  "\x1b[1;2B", "\x1b[1;3B", "\x1b[1;4B", "\x1b[1;5B", "\x1b[1;6B", "\x1b[1;7B", "\x1b[1;8B" };
static const char* const kcub1[] = { "\x1b[D",  "\x1b[1;2D", "\x1b[1;3D", "\x1b[1;4D", "\x1b[1;5D", "\x1b[1;6D", "\x1b[1;7D", "\x1b[1;8D" };
static const char* const kcuf1[] = { "\x1b[C",  "\x1b[1;2C", "\x1b[1;3C", "\x1b[1;4C", "\x1b[1;5C", "\x1b[1;6C", "\x1b[1;7C", "\x1b[1;8C" };
static const char* const kich1[] = { "\x1b[2~", "\x1b[2;2~", "\x1b[2;3~", "\x1b[2;4~", "\x1b[2;5~", "\x1b[2;6~", "\x1b[2;7~", "\x1b[2;8~" };
static const char* const kdch1[] = { "\x1b[3~", "\x1b[3;2~", "\x1b[3;3~", "\x1b[3;4~", "\x1b[3;5~", "\x1b[3;6~", "\x1b[3;7~", "\x1b[3;8~" };
static const char* const khome[] = { "\x1b[H",  "\x1b[1;2H", "\x1b[1;3H", "\x1b[1;4H", "\x1b[1;5H", "\x1b[1;6H", "\x1b[1;7H", "\x1b[1;8H" };
static const char* const kend[]  = { "\x1b[F",  "\x1b[1;2F", "\x1b[1;3F", "\x1b[1;4F", "\x1b[1;5F", "\x1b[1;6F", "\x1b[1;7F", "\x1b[1;8F" };
static const char* const kpp[]   = { "\x1b[5~", "\x1b[5;2~", "\x1b[5;3~", "\x1b[5;4~", "\x1b[5;5~", "\x1b[5;6~", "\x1b[5;7~", "\x1b[5;8~" };
static const char* const knp[]   = { "\x1b[6~", "\x1b[6;2~", "\x1b[6;3~", "\x1b[6;4~", "\x1b[6;5~", "\x1b[6;6~", "\x1b[6;7~", "\x1b[6;8~" };
static const char* const kcbt    = "\x1b[Z";
} // namespace terminfo

//------------------------------------------------------------------------------
static setting_bool g_ansi(
    "terminal.ansi",
    "Enables basic ANSI escape code support",
    "When printing the prompt, Clink has basic built-in support for SGR\n"
    "ANSI escape codes to control the text colours. This is automatically\n"
    "disabled if a third party tool is detected that also provides this\n"
    "facility. It can also be disabled by setting this to 0.",
    true);



//------------------------------------------------------------------------------
inline void win_terminal_in::begin()
{
    m_buffer_count = 0;
    m_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(m_stdin, &m_prev_mode);
}

//------------------------------------------------------------------------------
inline void win_terminal_in::end()
{
    SetConsoleMode(m_stdin, m_prev_mode);
    m_stdin = nullptr;
}

//------------------------------------------------------------------------------
inline void win_terminal_in::select()
{
    if (!m_buffer_count)
        read_console();
}

//------------------------------------------------------------------------------
inline int win_terminal_in::read()
{
    if (!m_buffer_count)
        return terminal::input_none;

    return pop();
}

//------------------------------------------------------------------------------
void win_terminal_in::read_console()
{
    // Clear 'processed input' flag so key presses such as Ctrl-C and Ctrl-S
    // aren't swallowed. We also want events about window size changes.
    struct mode_scope {
        HANDLE  handle;
        DWORD   prev_mode;

        mode_scope(HANDLE handle) : handle(handle)
        {
            GetConsoleMode(handle, &prev_mode);
            SetConsoleMode(handle, ENABLE_WINDOW_INPUT);
        }

        ~mode_scope()
        {
            SetConsoleMode(handle, prev_mode);
        }
    };
    
    mode_scope _(m_stdin);

    // Read input records sent from the terminal (aka conhost) until some
    // input has beeen buffered.
    unsigned int buffer_count = m_buffer_count;
    while (buffer_count == m_buffer_count)
    {
        DWORD count;
        INPUT_RECORD record;
        ReadConsoleInputW(m_stdin, &record, 1, &count);

        switch (record.EventType)
        {
        case KEY_EVENT:
            {
                auto& key_event = record.Event.KeyEvent;

                // Some times conhost can send through ALT codes, with the
                // resulting Unicode code point in the Alt key-up event.
                if (!key_event.bKeyDown
                    && key_event.wVirtualKeyCode == VK_MENU
                    && key_event.uChar.UnicodeChar)
                {
                    key_event.bKeyDown = TRUE;
                    key_event.dwControlKeyState = 0;
                }

                if (key_event.bKeyDown)
                    process_input(key_event);
            }
            break;

        case WINDOW_BUFFER_SIZE_EVENT:
            return;
        }
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::process_input(KEY_EVENT_RECORD const& record)
{
    static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
    static const int ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;

    int key_char = record.uChar.UnicodeChar;
    int key_vk = record.wVirtualKeyCode;
    int key_sc = record.wVirtualScanCode;
    int key_flags = record.dwControlKeyState;

    // Early out of unaccompanied ctrl/shift/alt key presses.
    if (key_vk == VK_MENU || key_vk == VK_CONTROL || key_vk == VK_SHIFT)
        return;

    // If the input was formed using AltGr or LeftAlt-LeftCtrl then things get
    // tricky. But there's always a Ctrl bit set, even if the user didn't press
    // a ctrl key. We can use this and the knowledge that Ctrl-modified keys
    // aren't printable to clear appropriate AltGr flags.
    if (key_char > 0x1f && (key_flags & CTRL_PRESSED))
    {
        key_flags &= ~CTRL_PRESSED;
        if (key_flags & RIGHT_ALT_PRESSED)
            key_flags &= ~RIGHT_ALT_PRESSED;
        else
            key_flags &= ~LEFT_ALT_PRESSED;
    }

    // Special case for shift-tab (aka. back-tab or kcbt).
    if (key_char == '\t' && !m_buffer_count && (key_flags & SHIFT_PRESSED))
        return push(terminfo::kcbt);

    if (key->bKeyDown == FALSE)
    {
        // Some times conhost can send through ALT codes, with the resulting
        // Unicode code point in the Alt key-up event.
        if (key_vk == VK_MENU && key_char)
        {
            push(key_char);
            return;
        }

        return;
    }

    // Include an ESC character in the input stream if Alt is pressed.
    if (key_char)
    {
        if (key_flags & ALT_PRESSED)
            push(0x1b);

        return push(key_char);
    }

    // The numpad keys such as PgUp, End, etc. don't come through with the
    // ENHANCED_KEY flag set so we'll infer it here.
    static const int enhanced_vks[] = {
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_HOME, VK_END,
        VK_INSERT, VK_DELETE, VK_PRIOR, VK_NEXT,
    };

    for (int i = 0; i < sizeof_array(enhanced_vks); ++i)
    {
        if (key_vk == enhanced_vks[i])
        {
            key_flags |= ENHANCED_KEY;
            break;
        }
    }

    // Convert enhanced keys to normal mode xterm compatible escape sequences.
    if (key_flags & ENHANCED_KEY)
    {
        static const struct {
            int                 code;
            const char* const*  seqs;
        } sc_map[] = {
            { 'H', terminfo::kcuu1, }, // up
            { 'P', terminfo::kcud1, }, // down
            { 'K', terminfo::kcub1, }, // left
            { 'M', terminfo::kcuf1, }, // right
            { 'R', terminfo::kich1, }, // insert
            { 'S', terminfo::kdch1, }, // delete
            { 'G', terminfo::khome, }, // home
            { 'O', terminfo::kend, },  // end
            { 'I', terminfo::kpp, },   // pgup
            { 'Q', terminfo::knp, },   // pgdn
        };

        // Calculate Xterm's modifier number.
        int i = 0;
        i |= !!(key_flags & SHIFT_PRESSED);
        i |= !!(key_flags & ALT_PRESSED) << 1;
        i |= !!(key_flags & CTRL_PRESSED) << 2;

        for (const auto& iter : sc_map)
        {
            if (iter.code != key_sc)
                continue;

            push(iter.seqs[i]);
            break;
        }

        return;
    }

    // This builds Ctrl-<key> c0 codes. Some of these actually come though in
    // key_char and some don't.
    if (key_flags & CTRL_PRESSED)
    {
        #define CONTAINS(l, r) (unsigned)(key_vk - l) <= (r - l)
             if (CONTAINS('A', 'Z'))    key_vk -= 'A' - 1;
        else if (CONTAINS(0xdb, 0xdd))  key_vk -= 0xdb - 0x1b;
        else if (key_vk == 0x32)        key_vk = 0;
        else if (key_vk == 0x36)        key_vk = 0x1e;
        else if (key_vk == 0xbd)        key_vk = 0x1f;
        else                            return;
        #undef CONTAINS

        if (key_flags & ALT_PRESSED)
            push(0x1b);

        push(key_vk);
    }
}

//------------------------------------------------------------------------------
void win_terminal_in::push(const char* seq)
{
    static const unsigned int mask = sizeof_array(m_buffer) - 1;

    if (m_buffer_count >= sizeof_array(m_buffer))
        return;

    int index = m_buffer_head + m_buffer_count;
    for (; m_buffer_count <= mask && *seq; ++m_buffer_count, ++index, ++seq)
        m_buffer[index & mask] = *seq;
}

//------------------------------------------------------------------------------
void win_terminal_in::push(unsigned int value)
{
    static const unsigned int mask = sizeof_array(m_buffer) - 1;

    if (m_buffer_count >= sizeof_array(m_buffer))
        return;

    int index = m_buffer_head + m_buffer_count;

    if (value < 0x80)
    {
        m_buffer[index & mask] = value;
        ++m_buffer_count;
        return;
    }

    wchar_t wc[2] = { (wchar_t)value, 0 };
    char utf8[mask + 1];
    unsigned int n = to_utf8(utf8, sizeof_array(utf8), wc);
    if (n <= unsigned(mask - m_buffer_count))
        for (unsigned int i = 0; i < n; ++i, ++index)
            m_buffer[index & mask] = utf8[i];

    m_buffer_count += n;
}

//------------------------------------------------------------------------------
inline unsigned char win_terminal_in::pop()
{
    if (!m_buffer_count)
        return 0xff;

    unsigned char value = m_buffer[m_buffer_head];

    --m_buffer_count;
    m_buffer_head = (m_buffer_head + 1) & (sizeof_array(m_buffer) - 1);

    return value;
}



//------------------------------------------------------------------------------
inline void win_terminal_out::begin()
{
    m_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    m_default_attr = csbi.wAttributes & 0xff;
    m_attr = m_default_attr;

    GetConsoleMode(m_stdout, &m_prev_mode);
}

//------------------------------------------------------------------------------
inline void win_terminal_out::end()
{
    SetConsoleMode(m_stdout, m_prev_mode);
    SetConsoleTextAttribute(m_stdout, m_default_attr);

    m_stdout = nullptr;
}

//------------------------------------------------------------------------------
void win_terminal_out::write(const char* chars, int length)
{
    str_iter iter(chars, length);
    while (length > 0)
    {
        wchar_t wbuf[256];
        int n = min<int>(sizeof_array(wbuf), length + 1);
        n = to_utf16(wbuf, n, iter);

        write(wbuf, n);

        n = int(iter.get_pointer() - chars);
        length -= n;
        chars += n;
    }
}

//------------------------------------------------------------------------------
inline void win_terminal_out::write(const wchar_t* chars, int length)
{
    DWORD written;
    WriteConsoleW(m_stdout, chars, length, &written, nullptr);
}

//------------------------------------------------------------------------------
inline void win_terminal_out::flush()
{
    // When writing to the console conhost.exe will restart the cursor blink
    // timer and hide it which can be disorientating, especially when moving
    // around a line. The below will make sure it stays visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    SetConsoleCursorPosition(m_stdout, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
inline int win_terminal_out::get_columns() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    return csbi.dwSize.X;
}

//------------------------------------------------------------------------------
inline int win_terminal_out::get_rows() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_stdout, &csbi);
    return (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
inline unsigned char win_terminal_out::get_default_attr() const
{
    return m_default_attr;
}

//------------------------------------------------------------------------------
inline unsigned char win_terminal_out::get_attr() const
{
    return m_attr;
}

//------------------------------------------------------------------------------
inline void win_terminal_out::set_attr(unsigned char attr)
{
    m_attr = attr;
    SetConsoleTextAttribute(m_stdout, attr);
}

//------------------------------------------------------------------------------
inline void* win_terminal_out::get_handle() const
{
    return m_stdout;
}



//------------------------------------------------------------------------------
void win_terminal::begin()
{
    m_size = 0;
    win_terminal_in::begin();
    win_terminal_out::begin();
}

//------------------------------------------------------------------------------
void win_terminal::end()
{
    win_terminal_out::end();
    win_terminal_in::end();
}

//------------------------------------------------------------------------------
void win_terminal::select()
{
    // Has the console been resized?
    unsigned int size = get_size();
    if (m_size != size)
        return;

    win_terminal_in::select();
}

//------------------------------------------------------------------------------
int win_terminal::read()
{
    // Has the console been resized?
    unsigned int size = get_size();
    if (m_size != size)
    {
        m_size = size;
        return input_terminal_resize;
    }

    return win_terminal_in::read();
}

//------------------------------------------------------------------------------
void win_terminal::flush()
{
    win_terminal_out::flush();
}

//------------------------------------------------------------------------------
int win_terminal::get_columns() const
{
    return win_terminal_out::get_columns();
}

//------------------------------------------------------------------------------
int win_terminal::get_rows() const
{
    return win_terminal_out::get_rows();
}

//------------------------------------------------------------------------------
unsigned int win_terminal::get_size() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(win_terminal_out::get_handle(), &csbi);

    unsigned int terminal_size = (csbi.dwSize.X << 16);
    return terminal_size | (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
void win_terminal::write_c1(const ecma48_code& code)
{
    if (!m_enable_c1)
    {
        win_terminal_out::write(code.get_pointer(), code.get_length()); 
        return;
    }

    if (code.get_code() != ecma48_code::c1_csi)
        return;

    int final, params[32], param_count;
    param_count = code.decode_csi(final, params, sizeof_array(params));
    const array<int> params_array(params, param_count);

    switch (final)
    {
    case 'm':
        write_sgr(params_array);
        break;
    }
}

//------------------------------------------------------------------------------
void win_terminal::write_c0(int c0)
{
    switch (c0)
    {
    case 0x07:
        // TODO
        break;

    default:
        {
            wchar_t c = wchar_t(c0);
            win_terminal_out::write(&c, 1);
        }
    }
}

//------------------------------------------------------------------------------
void win_terminal::write(const char* chars, int length)
{
    ecma48_iter iter(chars, m_state, length);
    while (const ecma48_code* code = iter.next())
    {
        switch (code->get_type())
        {
        case ecma48_code::type_chars:
            win_terminal_out::write(code->get_pointer(), code->get_length());
            break;

        case ecma48_code::type_c0:
            write_c0(code->get_code());
            break;

        case ecma48_code::type_c1:
            write_c1(*code);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void win_terminal::check_c1_support()
{
    // Check for the presence of known third party tools that also provide ANSI
    // escape code support.
    const char* dll_names[] = {
        "conemuhk.dll",
        "conemuhk64.dll",
        "ansi.dll",
        "ansi32.dll",
        "ansi64.dll",
    };

    for (int i = 0; i < sizeof_array(dll_names); ++i)
    {
        const char* dll_name = dll_names[i];
        if (GetModuleHandle(dll_name) != nullptr)
        {
            LOG("Disabling ANSI support. Found '%s'", dll_name);
            m_enable_c1 = false;
            return;
        }
    }

    // Give the user the option to disable ANSI support.
    m_enable_c1 = !g_ansi.get();
}

//------------------------------------------------------------------------------
void win_terminal::write_sgr(const array<int>& params)
{
    static const unsigned char sgr_to_attr[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

    // Process each code that is supported.
    unsigned char attr = get_attr();
    for (int param : params)
    {
        if (param == 0) // reset
        {
            attr = get_default_attr();
        }
        else if (param == 1) // fg intensity (bright)
        {
            attr |= 0x08;
        }
        else if (param == 2 || param == 22) // fg intensity (normal)
        {
            attr &= ~0x08;
        }
        else if (param == 4) // bg intensity (bright)
        {
            attr |= 0x80;
        }
        else if (param == 24) // bg intensity (normal)
        {
            attr &= ~0x80;
        }
        else if (param - 30 < 8) // fg colour
        {
            attr = (attr & 0xf8) | sgr_to_attr[(param - 30) & 7];
        }
        else if (param - 90 < 8) // fg colour
        {
            attr |= 0x08;
            attr = (attr & 0xf8) | sgr_to_attr[(param - 90) & 7];
        }
        else if (param == 39) // default fg colour
        {
            attr = (attr & 0xf8) | (get_default_attr() & 0x07);
        }
        else if (param - 40 < 8) // bg colour
        {
            attr = (attr & 0x8f) | (sgr_to_attr[(param - 40) & 7] << 4);
        }
        else if (param - 100 < 8) // bg colour
        {
            attr |= 0x80;
            attr = (attr & 0x8f) | (sgr_to_attr[(param - 100) & 7] << 4);
        }
        else if (param == 49) // default bg colour
        {
            attr = (attr & 0x8f) | (get_default_attr() & 0x70);
        }
        else if (param == 38 || param == 48) // extended colour (skipped)
        {
            /* TODO
            // format = param;5;[0-255] or param;2;r;g;b
            ++i;
            if (i >= csi.param_count)
                break;

            switch (csi.params[i])
            {
            case 2: i += 3; break;
            case 5: i += 1; break;
            }
            */

            continue;
        }
    }

    set_attr(attr);
}
