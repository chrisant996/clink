// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "ecma48_terminal.h"

#include <core/base.h>
#include <core/log.h>
#include <core/str_iter.h>

//------------------------------------------------------------------------------
static unsigned g_last_buffer_size = 0;

int             get_clink_setting_int(const char*);
void            on_terminal_resize();



//------------------------------------------------------------------------------
xterm_input::xterm_input()
: m_buffer_head(0)
, m_buffer_count(0)
{
}

//------------------------------------------------------------------------------
int xterm_input::read()
{
    if (int c = pop())
        return c;
    
    return read_console();
}

//------------------------------------------------------------------------------
int xterm_input::read_console()
{
    // Clear 'processed input' flag so key presses such as Ctrl-C and Ctrl-S
    // aren't swallowed. We also want events about window size changes.
    HANDLE handle_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD flags;
    GetConsoleMode(handle_stdin, &flags);
    flags |= ENABLE_WINDOW_INPUT;
    flags &= ~ENABLE_EXTENDED_FLAGS;
    flags &= ~ENABLE_PROCESSED_INPUT;
    SetConsoleMode(handle_stdin, flags);

loop:
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle_stdout, &csbi);

    // Check for a new buffer size for simulated SIGWINCH signals.
// MODE4
    {
        DWORD i = (csbi.dwSize.X << 16);
        i |= (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
        if (!g_last_buffer_size || g_last_buffer_size != i)
        {
            if (g_last_buffer_size)
                on_terminal_resize();
    
            g_last_buffer_size = i;
            goto loop;
        }
    }
// MODE4

    // Fresh read from the console.
    DWORD i;
    INPUT_RECORD record;
    ReadConsoleInputW(handle_stdin, &record, 1, &i);
    if (record.EventType != KEY_EVENT)
        goto loop;

    GetConsoleScreenBufferInfo(handle_stdout, &csbi);
// MODE4
    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
    {
        on_terminal_resize();

        g_last_buffer_size = (csbi.dwSize.X << 16) | csbi.dwSize.Y;
        goto loop;
    }
// MODE4

    const KEY_EVENT_RECORD* key = &record.Event.KeyEvent;
    int key_char = key->uChar.UnicodeChar;
    int key_vk = key->wVirtualKeyCode;
    int key_sc = key->wVirtualScanCode;
    int key_flags = key->dwControlKeyState;

#if defined(DEBUG_GETC) && defined(_DEBUG)
    {
        static int id = 0;
        int i;
        printf("\n%03d: %s ", id++, key->bKeyDown ? "+" : "-");
        for (i = 2; i < sizeof(*key) / sizeof(short); ++i)
            printf("%04x ", ((unsigned short*)key)[i]);
    }
#endif

    if (key->bKeyDown == FALSE)
    {
        // Some times conhost can send through ALT codes, with the resulting
        // Unicode code point in the Alt key-up event.
        if (key_vk == VK_MENU && key_char)
            goto end;

        goto loop;
    }

    // Windows supports an AltGr substitute which we check for here. As it
    // collides with Readline mappings Clink's support can be disabled.
    int altgr_sub;
    altgr_sub = !!(key_flags & LEFT_ALT_PRESSED);
    altgr_sub &= !!(key_flags & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED));
    altgr_sub &= !!key_char;

    if (altgr_sub && !get_clink_setting_int("use_altgr_substitute"))
    {
        altgr_sub = 0;
        key_char = 0;
    }

    int alt = 0;
    if (!altgr_sub)
        alt = !!(key_flags & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED));

    // No Unicode character? Then some post-processing is required to make the
    // output compatible with whatever standard Linux terminals adhere to and
    // that which Readline expects.
    static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;
    if (key_char == 0)
    {
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

        // Differentiate enhanced keys depending on modifier key state. MSVC's
        // runtime does something similar. Slightly non-standard.
        if (key_flags & ENHANCED_KEY)
        {
            static const int mod_map[][5] =
            {
                // j---->
                // Scan Nrml Shft
                {  'H', 'A', 'a'  }, // up      i
                {  'P', 'B', 'b'  }, // down    |
                {  'K', 'D', 'd'  }, // left    |
                {  'M', 'C', 'c'  }, // right   v
                {  'R', '2', 'w'  }, // insert
                {  'S', '3', 'e'  }, // delete
                {  'G', '1', 'q'  }, // home
                {  'O', '4', 'r'  }, // end
                {  'I', '5', 't'  }, // pgup
                {  'Q', '6', 'y'  }, // pgdn
            };

            for (int i = 0; i < sizeof_array(mod_map); ++i)
            {
                if (mod_map[i][0] != key_sc)
                    continue;

                int j = 1 + !!(key_flags & SHIFT_PRESSED);

                push((key_flags & CTRL_PRESSED) ? 'O' : '[');
                push(mod_map[i][j]);

                break;
            }

            // Blacklist.
            if (!m_buffer_count)
                goto loop;

            key_vk = 0x1b;
        }
        else if (!(key_flags & CTRL_PRESSED))
            goto loop;

        // This builds Ctrl-<key> map to match that as described by Readline's
        // source for the emacs/vi keymaps.
        #define CONTAINS(l, r) (unsigned)(key_vk - l) <= (r - l)
        else if (CONTAINS('A', 'Z'))    key_vk -= 'A' - 1;
        else if (CONTAINS(0xdb, 0xdd))  key_vk -= 0xdb - 0x1b;
        else if (key_vk == 0x32)        key_vk = 0;
        else if (key_vk == 0x36)        key_vk = 0x1e;
        else if (key_vk == 0xbd)        key_vk = 0x1f;
        else                            goto loop;
        #undef CONTAINS

        key_char = key_vk;
    }

    // Special case for shift-tab.
    if (key_char == '\t' && !m_buffer_count && (key_flags & SHIFT_PRESSED))
    {
        key_char = 0x1b;
        push('[');
        push('Z');
    }

end:
#if defined(DEBUG_GETC) && defined(_DEBUG)
    printf("\n%08x '%c'", key_char, key_char);
#endif

    // Include an ESC character in the input stream if Alt is pressed.
    if (alt && key_char < 0x80)
    {
        push(key_char);
        key_char = 0x1b;
    }

    return key_char;
}

//------------------------------------------------------------------------------
void xterm_input::push(int value)
{
    if (m_buffer_count >= sizeof_array(m_buffer))
        return;

    int index = (m_buffer_head + m_buffer_count) & (sizeof_array(m_buffer) - 1);

    m_buffer[index] = value;
    ++m_buffer_count;
}

//------------------------------------------------------------------------------
int xterm_input::pop()
{
    if (!m_buffer_count)
        return 0;

    int value = m_buffer[m_buffer_head];

    --m_buffer_count;
    m_buffer_head = (m_buffer_head + 1) & (sizeof_array(m_buffer) - 1);

    return value;
}



//------------------------------------------------------------------------------
ecma48_terminal::ecma48_terminal()
: m_handle(nullptr)
, m_default_attr(0)
, m_attr(0)
, m_enable_sgr(true)
{
}

//------------------------------------------------------------------------------
ecma48_terminal::~ecma48_terminal()
{
}

//------------------------------------------------------------------------------
void ecma48_terminal::begin()
{
    m_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    m_default_attr = csbi.wAttributes;
    m_attr = m_default_attr;
}

//------------------------------------------------------------------------------
void ecma48_terminal::end()
{
    SetConsoleTextAttribute(m_handle, m_default_attr);
}

//------------------------------------------------------------------------------
int ecma48_terminal::read()
{
    return m_xterm_input.read();
}

//------------------------------------------------------------------------------
void ecma48_terminal::write_csi(const ecma48_code& code)
{
    const ecma48_csi& csi = *(code.csi);
    switch (csi.func)
    {
    case 'm':
        if (!m_enable_sgr)
            break;

        write_sgr(csi);
        return;
    }

    write_impl(code.str, code.length); 
}

//------------------------------------------------------------------------------
void ecma48_terminal::write_c0(int c0)
{
    switch (c0)
    {
    case 0x07:
        // MODE4
        break;

    default:
        {
            wchar_t c = wchar_t(c0);
            DWORD written;
            WriteConsoleW(m_handle, &c, 1, &written, nullptr);
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal::write_impl(const char* chars, int length)
{
    str_iter iter(chars, length);
    while (length > 0)
    {
        wchar_t wbuf[256];
        int n = min<int>(sizeof_array(wbuf), length + 1);
        n = to_utf16(wbuf, n, iter);

        DWORD written;
        WriteConsoleW(m_handle, wbuf, n, &written, nullptr);

        n = int(iter.get_pointer() - chars);
        length -= n;
        chars += n;
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal::write(const char* chars, int length)
{
    ecma48_iter iter(chars, m_state, length);
    while (const ecma48_code* code = iter.next())
    {
        switch (code->type)
        {
        case ecma48_code::type_chars: write_impl(code->str, code->length); break;
        case ecma48_code::type_c0:    write_c0(code->c0);                  break;
        case ecma48_code::type_csi:   write_csi(*code);                    break;
        }
    }
}

//------------------------------------------------------------------------------
void ecma48_terminal::flush()
{
    // When writing to the console conhost.exe will restart the cursor blink
    // timer and hide it which can be disorientating, especially when moving
    // around a line. The below will make sure it stays visible.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    SetConsoleCursorPosition(m_handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void ecma48_terminal::check_sgr_support()
{
    // Check for the presence of known third party tools that also provide ANSI
    // escape code support (MODE4)
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
            m_enable_sgr = false;
            return;
        }
    }

    // Give the user the option to disable ANSI support.
    if (get_clink_setting_int("ansi_code_support") == 0)
        m_enable_sgr = false;

    return;
}

//------------------------------------------------------------------------------
int ecma48_terminal::get_columns() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    return csbi.dwSize.X;
}

//------------------------------------------------------------------------------
int ecma48_terminal::get_rows() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(m_handle, &csbi);
    return (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
}

//------------------------------------------------------------------------------
void ecma48_terminal::write_sgr(const ecma48_csi& csi)
{
    static const int sgr_to_attr[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

    // Process each code that is supported.
    for (int i = 0; i < csi.param_count; ++i)
    {
        int param = csi.params[i];

        if (param == 0) // reset
        {
            m_attr = m_default_attr;
        }
        else if (param == 1) // fg intensity (bright)
        {
            m_attr |= 0x08;
        }
        else if (param == 2 || param == 22) // fg intensity (normal)
        {
            m_attr &= ~0x08;
        }
        else if (param == 4) // bg intensity (bright)
        {
            m_attr |= 0x80;
        }
        else if (param == 24) // bg intensity (normal)
        {
            m_attr &= ~0x80;
        }
        else if ((unsigned int)param - 30 < 8) // fg colour
        {
            m_attr = (m_attr & 0xf8) | sgr_to_attr[(param - 30) & 7];
        }
        else if (param == 39) // default fg colour
        {
            m_attr = (m_attr & 0xf8) | (m_default_attr & 0x07);
        }
        else if ((unsigned int)param - 40 < 8) // bg colour
        {
            m_attr = (m_attr & 0x8f) | (sgr_to_attr[(param - 40) & 7] << 4);
        }
        else if (param == 49) // default bg colour
        {
            m_attr = (m_attr & 0x8f) | (m_default_attr & 0x70);
        }
        else if (param == 38 || param == 48) // extended colour (skipped)
        {
            // format = param;5;[0-255] or param;2;r;g;b
            ++i;
            if (i >= csi.param_count)
                break;

            switch (csi.params[i])
            {
            case 2: i += 3; break;
            case 5: i += 1; break;
            }

            continue;
        }
    }

    SetConsoleTextAttribute(m_handle, m_attr);
}
