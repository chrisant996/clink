// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_in.h"

#include <core/base.h>
#include <core/str.h>

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
static const char* const kfx[]   = {
    // kf1-12 : Fx unmodified
    "\x1bOP",   "\x1bOQ",   "\x1bOR",   "\x1bOS",
    "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~",
    "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~",

    // kf13-24 : shift
    "\x1b[1;2P",  "\x1b[1;2Q",  "\x1b[1;2R",  "\x1b[1;2S",
    "\x1b[15;2~", "\x1b[17;2~", "\x1b[18;2~", "\x1b[19;2~",
    "\x1b[20;2~", "\x1b[21;2~", "\x1b[23;2~", "\x1b[24;2~",

    // kf25-36 : ctrl
    "\x1b[1;5P",  "\x1b[1;5Q",  "\x1b[1;5R",  "\x1b[1;5S",
    "\x1b[15;5~", "\x1b[17;5~", "\x1b[18;5~", "\x1b[19;5~",
    "\x1b[20;5~", "\x1b[21;5~", "\x1b[23;5~", "\x1b[24;5~",

    // kf37-48 : ctrl-shift
    "\x1b[1;6P",  "\x1b[1;6Q",  "\x1b[1;6R",  "\x1b[1;6S",
    "\x1b[15;6~", "\x1b[17;6~", "\x1b[18;6~", "\x1b[19;6~",
    "\x1b[20;6~", "\x1b[21;6~", "\x1b[23;6~", "\x1b[24;6~",
};
} // namespace terminfo



//------------------------------------------------------------------------------
void win_terminal_in::begin()
{
    m_buffer_count = 0;
    m_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(m_stdin, &m_prev_mode);
}

//------------------------------------------------------------------------------
void win_terminal_in::end()
{
    SetConsoleMode(m_stdin, m_prev_mode);
    m_stdin = nullptr;
}

//------------------------------------------------------------------------------
void win_terminal_in::select()
{
    if (!m_buffer_count)
        read_console();
}

//------------------------------------------------------------------------------
int win_terminal_in::read()
{
    if (!m_buffer_count)
        return terminal_in::input_none;

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

    // Function keys (kf1-kf48 from xterm+pcf2)
    unsigned key_func = key_vk - VK_F1;
    if (key_func <= (VK_F12 - VK_F1))
    {
        if (key_flags & ALT_PRESSED)
            push(0x1b);

        int kfx_group = !!(key_flags & SHIFT_PRESSED);
        kfx_group |= !!(key_flags & CTRL_PRESSED) << 1;
        push((terminfo::kfx + (12 * kfx_group) + key_func)[0]);

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
unsigned char win_terminal_in::pop()
{
    if (!m_buffer_count)
        return 0xff;

    unsigned char value = m_buffer[m_buffer_head];

    --m_buffer_count;
    m_buffer_head = (m_buffer_head + 1) & (sizeof_array(m_buffer) - 1);

    return value;
}
