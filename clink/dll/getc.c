/* Copyright (c) 2013 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
DWORD   g_knownBufferSize = 0;
int     get_clink_setting_int(const char*);

//------------------------------------------------------------------------------
static void simulate_sigwinch()
{
    // In the land of POSIX a terminal would raise a SIGWINCH signal when it is
    // resized. See rl_sigwinch_handler() in readline/signal.c.

    extern int _rl_vis_botlin;
    extern int _rl_last_c_pos;
    extern int _rl_last_v_pos;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD cursor_pos;
    HANDLE handle;
    int cell_count;
    DWORD written;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);

    // If the new buffer size has clipped the cursor, conhost will move it down
    // one line. Readline on the other hand thinks that the cursor's row remains
    // unchanged.
    cursor_pos.X = 0;
    cursor_pos.Y = csbi.dwCursorPosition.Y;
    if (_rl_last_c_pos >= csbi.dwSize.X && cursor_pos.Y > 0)
        --cursor_pos.Y;

    SetConsoleCursorPosition(handle, cursor_pos);

    // Readline only clears the last row. If a resize causes a line to now occupy
    // two or more fewer lines that it did previous it will leave display artefacts.
    if (_rl_vis_botlin)
    {
        // _rl_last_v_pos is vertical offset of cursor from first line.
        if (_rl_last_v_pos > 0)
            cursor_pos.Y -= _rl_last_v_pos - 1; // '- 1' so we're line below first.

        cell_count = csbi.dwSize.X * _rl_vis_botlin;

        FillConsoleOutputCharacterW(handle, ' ', cell_count, cursor_pos, &written);
        FillConsoleOutputAttribute(handle, csbi.wAttributes, cell_count, cursor_pos,
            &written);
    }

    // Tell Readline the buffer's resized, but make sure we don't use Clink's
    // redisplay path as then Readline won't redisplay multiline prompts correctly.
    {
        rl_voidfunc_t* old_redisplay = rl_redisplay_function;
        rl_redisplay_function = rl_redisplay;

        rl_resize_terminal();

        rl_redisplay_function = old_redisplay;
    }
}

//------------------------------------------------------------------------------
//#define DEBUG_GETC
/*
    Taken from msvcrt.dll's getextendedkeycode()

                          ELSE SHFT CTRL ALTS
    00000000`723d36e0  1c 000d 000d 000a a600
    00000000`723d36ea  35 002f 003f 9500 a400
    00000000`723d36f4  47 47e0 47e0 77e0 9700
    00000000`723d36fe  48 48e0 48e0 8de0 9800
    00000000`723d3708  49 49e0 49e0 86e0 9900
    00000000`723d3712  4b 4be0 4be0 73e0 9b00
    00000000`723d371c  4d 4de0 4de0 74e0 9d00
    00000000`723d3726  4f 4fe0 4fe0 75e0 9f00
    00000000`723d3730  50 50e0 50e0 91e0 a000
    00000000`723d373a  51 51e0 51e0 76e0 a100
    00000000`723d3744  52 52e0 52e0 92e0 a200
    00000000`723d374e  53 53e0 53e0 93e0 a300

    home 01 00 00 00 01 00 24 00 47 00 00 00 00 00 00 00
    end  01 00 00 00 01 00 23 00 4f 00 00 00 00 00 00 00
    pgup 01 00 00 00 01 00 21 00 49 00 00 00 00 00 00 00
    pgdn 01 00 00 00 01 00 22 00 51 00 00 00 00 00 00 00
*/
static int getc_internal(int* alt)
{
    static int       carry        = 0; // Multithreading? What's that?
    static const int CTRL_PRESSED = LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED;

    int key_char;
    int key_vk;
    int key_sc;
    int key_flags;
    HANDLE handle_stdin;

    handle_stdin = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(handle_stdin, ENABLE_WINDOW_INPUT);

loop:
    key_char = 0;
    key_vk = 0;
    key_sc = 0;
    key_flags = 0;
    *alt = 0;

    // Read a key or use what was carried across from a previous call.
    if (carry)
    {
        key_flags = ENHANCED_KEY;
        key_char = carry;
        carry = 0;
    }
    else
    {
        HANDLE handle_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        DWORD i;
        INPUT_RECORD record;
        const KEY_EVENT_RECORD* key;
        int altgr_sub;

        GetConsoleScreenBufferInfo(handle_stdout, &csbi);

        // Check for a new buffer size for simulated SIGWINCH signals.
        i = (csbi.dwSize.X << 16);
        i |= (csbi.srWindow.Bottom - csbi.srWindow.Top) + 1;
        if (!g_knownBufferSize || g_knownBufferSize != i)
        {
            if (g_knownBufferSize)
                simulate_sigwinch();

            g_knownBufferSize = i;
            goto loop;
        }

        // Fresh read from the console.
        ReadConsoleInputW(handle_stdin, &record, 1, &i);
        if (record.EventType != KEY_EVENT)
            goto loop;

        GetConsoleScreenBufferInfo(handle_stdout, &csbi);
        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
        {
            simulate_sigwinch();

            g_knownBufferSize = (csbi.dwSize.X << 16) | csbi.dwSize.Y;
            goto loop;
        }

        key = &record.Event.KeyEvent;
        key_char = key->uChar.UnicodeChar;
        key_vk = key->wVirtualKeyCode;
        key_sc = key->wVirtualScanCode;
        key_flags = key->dwControlKeyState;

#if defined(DEBUG_GETC) && defined(_DEBUG)
        {
            static int id = 0;
            int i;
            printf("\n%03d: %s ", id++, key->bKeyDown ? "+" : "-");
            for (i = 2; i < sizeof(*key) / sizeof(short); ++i)
            {
                printf("%04x ", ((unsigned short*)key)[i]);
            }
        }
#endif

        if (key->bKeyDown == FALSE)
        {
            // Some times conhost can send through ALT codes, with the resulting
            // Unicode code point in the Alt key-up event.
            if (key_vk == VK_MENU && key_char)
            {
                goto end;
            }

            goto loop;
        }

        // Windows supports an AltGr substitute which we check for here. As it
        // collides with Readline mappings Clink's support can be disabled.
        altgr_sub = !!(key_flags & LEFT_ALT_PRESSED);
        altgr_sub &= !!(key_flags & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED));
        altgr_sub &= !!key_char;

        if (altgr_sub && !get_clink_setting_int("use_altgr_substitute"))
        {
            altgr_sub = 0;
            key_char = 0;
        }

        if (!altgr_sub)
            *alt = !!(key_flags & LEFT_ALT_PRESSED);
    }

    // No Unicode character? Then some post-processing is required to make the
    // output compatible with whatever standard Linux terminals adhere to and
    // that which Readline expects.
    if (key_char == 0)
    {
        int i;

        // The numpad keys such as PgUp, End, etc. don't come through with the
        // ENHANCED_KEY flag set so we'll infer it here.
        static const int enhanced_vks[] = {
            VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_HOME, VK_END,
            VK_INSERT, VK_DELETE, VK_PRIOR, VK_NEXT,
        };

        for (i = 0; i < sizeof_array(enhanced_vks); ++i)
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
            static const int mod_map[][4] =
            {
                //Nrml  Shft  Ctrl  CtSh
                { 0x47, 0x61, 0x77, 0x21 }, // Gaw! home
                { 0x48, 0x62, 0x54, 0x22 }, // HbT" up
                { 0x49, 0x63, 0x55, 0x23 }, // IcU# pgup
                { 0x4b, 0x64, 0x73, 0x24 }, // Kds$ left
                { 0x4d, 0x65, 0x74, 0x25 }, // Met% right
                { 0x4f, 0x66, 0x75, 0x26 }, // Ofu& end
                { 0x50, 0x67, 0x56, 0x27 }, // PgV' down
                { 0x51, 0x68, 0x76, 0x28 }, // Qhv( pgdn
                { 0x52, 0x69, 0x57, 0x29 }, // RiW) insert
                { 0x53, 0x6a, 0x58, 0x2a }, // SjX* delete
            };

            for (i = 0; i < sizeof_array(mod_map); ++i)
            {
                int j = 0;
                if (mod_map[i][j] != key_sc)
                {
                    continue;
                }

                j += !!(key_flags & SHIFT_PRESSED);
                j += !!(key_flags & CTRL_PRESSED) << 1;
                carry = mod_map[i][j];
                break;
            }

            // Blacklist.
            if (!carry)
            {
                goto loop;
            }

            key_vk = 0xe0;
        }
        else if (!(key_flags & CTRL_PRESSED))
        {
            goto loop;
        }

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
    else if (!(key_flags & ENHANCED_KEY) && key_char > 0x7f)
    {
        key_char |= 0x8000000;
    }

    // Special case for shift-tab.
    if (key_char == '\t' && !carry && (key_flags & SHIFT_PRESSED))
    {
        key_char = 0xe0;
        carry = 'Z';
    }

end:
#if defined(DEBUG_GETC) && defined(_DEBUG)
    printf("\n%08x '%c'", key_char, key_char);
#endif

    return key_char;
}

//------------------------------------------------------------------------------
int getc_impl(FILE* stream)
{
    int printable;
    int alt;
    int i;
    while (1)
    {
        wchar_t wc[2];
        char utf8[4];

        alt = 0;
        i = GETWCH_IMPL(&alt);

        // MSB is set if value represents a printable character.
        printable = (i & 0x80000000);
        i &= ~printable;

        // Treat esc like cmd.exe does - clear the line.
        if (i == 0x1b)
        {
            if (rl_editing_mode == emacs_mode &&
                get_clink_setting_int("esc_clears_line"))
            {
                using_history();
                rl_delete_text(0, rl_end);
                rl_point = 0;
                rl_redisplay();
                continue;
            }
        }

        // Mask off top bits, they're used to track ALT key state.
        if (i < 0x80 || (i == 0xe0 && !printable))
        {
            break;
        }

        // Convert to utf-8 and insert directly into rl's line buffer.
        wc[0] = (wchar_t)i;
        wc[1] = L'\0';
        WideCharToMultiByte(CP_UTF8, 0, wc, -1, utf8, sizeof(utf8), NULL, NULL);

        rl_insert_text(utf8);
        rl_redisplay();
    }

    alt = alt ? 0x80 : 0;
    return i|alt;
}

// vim: expandtab
