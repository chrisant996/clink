// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "win_terminal_in.h"
#include "key_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/settings.h>

#include <Windows.h>
#include <assert.h>

//------------------------------------------------------------------------------
extern bool is_scroll_mode();



//------------------------------------------------------------------------------
static setting_bool g_modify_other_keys(
    "terminal.modify_other_keys",
    "Use XTerm modifyOtherKeys sequences",
    "When enabled, pressing Space or Tab with modifier keys sends extended\n"
    "XTerm key sequences so they can be bound separately.",
    true);

//------------------------------------------------------------------------------
static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
static const int ALT_PRESSED = LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED;

// TODO: 0.4.8 keyboard compatibility mode
#define CSI(x) "\x1b[" #x
#define SS3(x) "\x1bO" #x
namespace terminfo { //                       Shf        Alt        AtlShf     Ctl        CtlShf     CtlAlt     CtlAltShf
static const char* const kcuu1[] = { CSI(A),  CSI(1;2A), CSI(1;3A), CSI(1;4A), CSI(1;5A), CSI(1;6A), CSI(1;7A), CSI(1;8A) }; // up
static const char* const kcud1[] = { CSI(B),  CSI(1;2B), CSI(1;3B), CSI(1;4B), CSI(1;5B), CSI(1;6B), CSI(1;7B), CSI(1;8B) }; // down
static const char* const kcub1[] = { CSI(D),  CSI(1;2D), CSI(1;3D), CSI(1;4D), CSI(1;5D), CSI(1;6D), CSI(1;7D), CSI(1;8D) }; // left
static const char* const kcuf1[] = { CSI(C),  CSI(1;2C), CSI(1;3C), CSI(1;4C), CSI(1;5C), CSI(1;6C), CSI(1;7C), CSI(1;8C) }; // right
static const char* const kich1[] = { CSI(2~), CSI(2;2~), CSI(2;3~), CSI(2;4~), CSI(2;5~), CSI(2;6~), CSI(2;7~), CSI(2;8~) }; // insert
static const char* const kdch1[] = { CSI(3~), CSI(3;2~), CSI(3;3~), CSI(3;4~), CSI(3;5~), CSI(3;6~), CSI(3;7~), CSI(3;8~) }; // delete
static const char* const khome[] = { CSI(H),  CSI(1;2H), CSI(1;3H), CSI(1;4H), CSI(1;5H), CSI(1;6H), CSI(1;7H), CSI(1;8H) }; // home
static const char* const kend[]  = { CSI(F),  CSI(1;2F), CSI(1;3F), CSI(1;4F), CSI(1;5F), CSI(1;6F), CSI(1;7F), CSI(1;8F) }; // end
static const char* const kpp[]   = { CSI(5~), CSI(5;2~), CSI(5;3~), CSI(5;4~), CSI(5;5~), CSI(5;6~), CSI(5;7~), CSI(5;8~) }; // pgup
static const char* const knp[]   = { CSI(6~), CSI(6;2~), CSI(6;3~), CSI(6;4~), CSI(6;5~), CSI(6;6~), CSI(6;7~), CSI(6;8~) }; // pgdn
static const char* const kcbt    = CSI(Z);
static const char* const kfx[]   = {
    // kf1-12 : Fx unmodified
    SS3(P),     SS3(Q),     SS3(R),     SS3(S),
    CSI(15~),   CSI(17~),   CSI(18~),   CSI(19~),
    CSI(20~),   CSI(21~),   CSI(23~),   CSI(24~),

    // kf13-24 : shift
    CSI(1;2P),  CSI(1;2Q),  CSI(1;2R),  CSI(1;2S),
    CSI(15;2~), CSI(17;2~), CSI(18;2~), CSI(19;2~),
    CSI(20;2~), CSI(21;2~), CSI(23;2~), CSI(24;2~),

    // kf25-36 : ctrl
    CSI(1;5P),  CSI(1;5Q),  CSI(1;5R),  CSI(1;5S),
    CSI(15;5~), CSI(17;5~), CSI(18;5~), CSI(19;5~),
    CSI(20;5~), CSI(21;5~), CSI(23;5~), CSI(24;5~),

    // kf37-48 : ctrl-shift
    CSI(1;6P),  CSI(1;6Q),  CSI(1;6R),  CSI(1;6S),
    CSI(15;6~), CSI(17;6~), CSI(18;6~), CSI(19;6~),
    CSI(20;6~), CSI(21;6~), CSI(23;6~), CSI(24;6~),
};

#define MOK(x) "\x1b[27;" #x
//                                            Shf     Alt   AtlShf   Ctl         CtlShf      CtlAlt      CtlAltShf
static const char* const ktab[]  = { "\t",    CSI(Z), "",   "",      MOK(5;9~),  MOK(6;9~),  "",         ""         }; // TAB
static const char* const kspc[]  = { " ",     " ",    "",   "",      MOK(5;32~), MOK(6;32~), MOK(7;32~), MOK(8;32~) }; // SPC

static int xterm_modifier(int key_flags)
{
    // Calculate Xterm's modifier number.
    int i = 0;
    i |= !!(key_flags & SHIFT_PRESSED);
    i |= !!(key_flags & ALT_PRESSED) << 1;
    i |= !!(key_flags & CTRL_PRESSED) << 2;
    return i;
}
} // namespace terminfo
#undef SS3
#undef CSI



//------------------------------------------------------------------------------
enum : unsigned char
{
    input_abort_byte    = 0xff,
    input_none_byte     = 0xfe,
    input_timeout_byte  = 0xfd,
};



//------------------------------------------------------------------------------
static unsigned int get_dimensions()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    auto cols = short(csbi.dwSize.X);
    auto rows = short(csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
    return (cols << 16) | rows;
}

//------------------------------------------------------------------------------
static void set_cursor_visibility(bool state)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(handle, &info);
    info.bVisible = BOOL(state);
    SetConsoleCursorInfo(handle, &info);
}

//------------------------------------------------------------------------------
static void adjust_cursor_on_resize(COORD prev_position)
{
    // Windows will move the cursor onto a new line when it gets clipped on
    // buffer resize. Other terminals clamp along the X axis.

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle , &csbi);
    if (prev_position.X < csbi.dwSize.X)
        return;

    COORD fix_position = {
        short(csbi.dwSize.X - 1),
        short(csbi.dwCursorPosition.Y - 1)
    };
    SetConsoleCursorPosition(handle, fix_position);
}



//------------------------------------------------------------------------------
void win_terminal_in::begin()
{
    m_buffer_count = 0;
    m_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(m_stdin, &m_prev_mode);
    set_cursor_visibility(false);
}

//------------------------------------------------------------------------------
void win_terminal_in::end()
{
    set_cursor_visibility(true);
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
    unsigned int dimensions = get_dimensions();
    if (dimensions != m_dimensions)
    {
        m_dimensions = dimensions;
        return terminal_in::input_terminal_resize;
    }

    if (!m_buffer_count)
        return terminal_in::input_none;

    unsigned char c = pop();
    switch (c)
    {
    case input_none_byte:       return terminal_in::input_none;
    case input_timeout_byte:    return terminal_in::input_timeout;
    case input_abort_byte:      return terminal_in::input_abort;
    default:                    return c;
    }
}

//------------------------------------------------------------------------------
key_tester* win_terminal_in::set_key_tester(key_tester* keys)
{
    key_tester* ret = m_keys;
    m_keys = keys;
    return ret;
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
    } _ms(m_stdin);

    // Hide the cursor unless we're accepting input so we don't have to see it
    // jump around as the screen's drawn.
    struct cursor_scope {
        cursor_scope()  { set_cursor_visibility(true); }
// TODO: I think this is what broke cursor visibility in the lua debugger.
        ~cursor_scope() { set_cursor_visibility(false); }
    } _cs;

    // Conhost restarts the cursor blink when writing to the console. It restarts
    // hidden which means that if you type faster than the blink the cursor turns
    // invisible. Fortunately, moving the cursor restarts the blink on visible.
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(stdout_handle, &csbi);
    if (!is_scroll_mode())
        SetConsoleCursorPosition(stdout_handle, csbi.dwCursorPosition);

    // Read input records sent from the terminal (aka conhost) until some
    // input has been buffered.
    unsigned int buffer_count = m_buffer_count;
    while (buffer_count == m_buffer_count)
    {
        DWORD count;
        INPUT_RECORD record;
        if (!ReadConsoleInputW(m_stdin, &record, 1, &count))
        {
            // Handle's probably invalid if ReadConsoleInput() failed.
            m_buffer_count = 1;
            m_buffer[0] = input_abort_byte;
            return;
        }

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
                {
                    process_input(key_event);

                    // If the processed input chord isn't bound, discard it.
                    // Otherwise unbound keys can have the tail part of their
                    // sequence show up as though it were typed input.  The
                    // approach here assumes no more than one key sequence per
                    // input record.
                    if (m_keys)
                    {
                        // If there are unprocessed queued keys, then we don't
                        // know what keymap will be active when this new input
                        // gets processed, so we can't accurately tell whether
                        // the key sequence is bound to anything.
                        assert(buffer_count == 0);

                        const int len = m_buffer_count - buffer_count;
                        if (len > 0)
                        {
                            // m_buffer is circular, so copy the key sequence to
                            // a separate sequential buffer.
                            char chord[sizeof_array(m_buffer) + 1];
                            static const unsigned int mask = sizeof_array(m_buffer) - 1;
                            for (int i = 0; i < len; ++i)
                                chord[i] = m_buffer[(m_buffer_head + i) & mask];

                            // Readline has a bug in rl_function_of_keyseq_len
                            // that looks for nul termination even though it's
                            // supposed to use a length instead.
                            chord[len] = '\0';

                            str<32> new_chord;
                            if (m_keys->translate(chord, len, new_chord))
                            {
                                m_buffer_count = buffer_count;
                                for (unsigned int i = 0; i < new_chord.length(); ++i)
                                    push((unsigned int)new_chord.c_str()[i]);
                            }
                            else if (!m_keys->is_bound(chord, len))
                            {
                                m_buffer_count = buffer_count;
                            }

                            m_keys->set_keyseq_len(m_buffer_count);
                        }
                    }
                }
            }
            break;

        case WINDOW_BUFFER_SIZE_EVENT:
            // Windows will move the cursor onto a new line when it gets clipped
            // on buffer resize. Other terminals
            adjust_cursor_on_resize(csbi.dwCursorPosition);
            return;
        }
    }
}

//------------------------------------------------------------------------------
extern "C" int rl_editing_mode;
void win_terminal_in::process_input(KEY_EVENT_RECORD const& record)
{
    int key_char = record.uChar.UnicodeChar;
    int key_vk = record.wVirtualKeyCode;
    int key_sc = record.wVirtualScanCode;
    int key_flags = record.dwControlKeyState;

    // We filter out Alt key presses unless they generated a character.
    if (key_vk == VK_MENU)
    {
        if (key_char)
            push(key_char);

        return;
    }

    // Early out of unaccompanied ctrl/shift key presses.
    if (key_vk == VK_CONTROL || key_vk == VK_SHIFT)
        return;

    // Special treatment for escape.
    // TODO: 0.4.8 keyboard compatibility mode
    if (key_char == 0x1b && rl_editing_mode != 0/*vi_mode*/)
        return push(bindableEsc);

    // Special treatment for variations of tab and space.
    if (key_vk == VK_TAB && !m_buffer_count && g_modify_other_keys.get())
        return push(terminfo::ktab[terminfo::xterm_modifier(key_flags)]);
    if (key_vk == VK_SPACE && !m_buffer_count && g_modify_other_keys.get())
        return push(terminfo::kspc[terminfo::xterm_modifier(key_flags)]);

    // If the input was formed using AltGr or LeftAlt-LeftCtrl then things get
    // tricky. But there's always a Ctrl bit set, even if the user didn't press
    // a ctrl key. We can use this and the knowledge that Ctrl-modified keys
    // aren't printable to clear appropriate AltGr flags.
    if ((key_char > 0x1f && key_char != 0x7f) && (key_flags & CTRL_PRESSED))
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

        for (const auto& iter : sc_map)
        {
            if (iter.code != key_sc)
                continue;

            push(iter.seqs[terminfo::xterm_modifier(key_flags)]);
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
        return input_none_byte;

    unsigned char value = m_buffer[m_buffer_head];

    --m_buffer_count;
    m_buffer_head = (m_buffer_head + 1) & (sizeof_array(m_buffer) - 1);

    return value;
}
