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
int get_clink_setting_int(const char*);

//------------------------------------------------------------------------------
static void simulate_sigwinch(COORD expected_cursor_pos)
{
    // In the land of POSIX a terminal would raise a SIGWINCH signal when it is
    // resized. See rl_sigwinch_handler() in readline/signal.c.

    extern int _rl_vis_botlin;
    extern int _rl_last_v_pos;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    rl_voidfunc_t* redisplay_func_cache;
    int base_y;
    int bottom_line;
    HANDLE handle;

    bottom_line = _rl_vis_botlin - 1;
    handle = GetStdHandle(STD_OUTPUT_HANDLE);

    // Cache redisplay function. Need original as it handles redraw correctly.
    redisplay_func_cache = rl_redisplay_function;
    rl_redisplay_function = rl_redisplay;

    // Cursor may be out of sync with where Readline expects the cursor to be.
    // Put it back where it was, clamping if necessary.
    GetConsoleScreenBufferInfo(handle, &csbi);
    if (expected_cursor_pos.X >= csbi.dwSize.X)
    {
        expected_cursor_pos.X = csbi.dwSize.X - 1;
    }
    if (expected_cursor_pos.Y >= csbi.dwSize.Y)
    {
        expected_cursor_pos.Y = csbi.dwSize.Y - 1;
    }
    SetConsoleCursorPosition(handle, expected_cursor_pos);

    // Let Readline handle the buffer resize.
    RL_SETSTATE(RL_STATE_SIGHANDLER);
    rl_resize_terminal();
    RL_UNSETSTATE(RL_STATE_SIGHANDLER);

    rl_redisplay_function = redisplay_func_cache;

    // Now some redraw edge cases need to be handled.
    GetConsoleScreenBufferInfo(handle, &csbi);
    base_y = csbi.dwCursorPosition.Y - _rl_last_v_pos;

    if (bottom_line > _rl_vis_botlin)
    {
        // Readline SIGWINCH handling assumes that at most one line needs to
        // be cleared which is not the case when resizing from small to large
        // widths.

        CHAR_INFO fill;
        SMALL_RECT rect;
        COORD coord;

        rect.Left = 0;
        rect.Right = csbi.dwSize.X;
        rect.Top = base_y + _rl_vis_botlin + 1;
        rect.Bottom = base_y + bottom_line;

        fill.Char.AsciiChar = ' ';
        fill.Attributes = csbi.wAttributes;

        coord.X = rect.Right + 1;
        coord.Y = rect.Top;

        ScrollConsoleScreenBuffer(handle, &rect, NULL, coord, &fill);
    }
    else
    {
        // Readline never writes to the last column as it wraps the cursor. The
        // last column will have noise when making the width smaller. Clear it.

        CHAR_INFO fill;
        SMALL_RECT rect;
        COORD coord;

        rect.Left = rect.Right = csbi.dwSize.X - 1;
        rect.Top = base_y;
        rect.Bottom = base_y + _rl_vis_botlin;

        fill.Char.AsciiChar = ' ';
        fill.Attributes = csbi.wAttributes;

        coord.X = rect.Right + 1;
        coord.Y = rect.Top;

        ScrollConsoleScreenBuffer(handle, &rect, NULL, coord, &fill);
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
    HANDLE handle;
    DWORD mode;

    // Clear all flags so the console doesn't do anything special. This prevents
    // key presses such as Ctrl-C and Ctrl-S from being swallowed.
    handle = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(handle, &mode);

loop:
    key_char = 0;
    key_vk = 0;
    key_sc = 0;
    key_flags = 0;
    *alt = 0;

    // Read a key or use what was carried across from a previous call.
    if (carry)
    {
        key_char = carry;
        carry = 0;
    }
    else
    {
        HANDLE handle_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        DWORD count;
        INPUT_RECORD record;
        const KEY_EVENT_RECORD* key;

        GetConsoleScreenBufferInfo(handle_stdout, &csbi);

        // Fresh read from the console.
        SetConsoleMode(handle, ENABLE_WINDOW_INPUT);
        ReadConsoleInputW(handle, &record, 1, &count);

        // Simulate SIGWINCH signals.
        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
        {
            simulate_sigwinch(csbi.dwCursorPosition);
            goto loop;
        }

        if (record.EventType != KEY_EVENT)
        {
            goto loop;
        }

        key = &record.Event.KeyEvent;
        if (key->bKeyDown == FALSE)
        {
            goto loop;
        }

        key_char = key->uChar.UnicodeChar;
        key_vk = key->wVirtualKeyCode;
        key_sc = key->wVirtualScanCode;
        key_flags = key->dwControlKeyState;

        *alt = !!(key_flags & LEFT_ALT_PRESSED);

#if defined(DEBUG_GETC) && defined(_DEBUG)
        {
            int i;
            puts("");
            for (i = 0; i < sizeof(*key); ++i)
            {
                printf("%02x ", ((unsigned char*)key)[i]);
            }
        }
#endif
    }

    // No Unicode character? Then some post-processing is required to make the
    // output compatible with whatever standard Linux terminals adhere to and
    // that which Readline expects.
    if (key_char == 0)
    {
        // Differentiate enhanced keys depending on modifier key state. MSVC's
        // runtime does something similar. Slightly non-standard.
        if (key_flags & ENHANCED_KEY)
        {
            int i;
            static const int mod_map[][4] =
            {
                //Nrml  Shft  Ctrl  CtSh
                { 0x47, 0x61, 0x77, 0x21 }, // home
                { 0x48, 0x62, 0x54, 0x22 }, // up
                { 0x49, 0x63, 0x55, 0x23 }, // pgup
                { 0x4b, 0x64, 0x73, 0x24 }, // left
                { 0x4d, 0x65, 0x74, 0x25 }, // right
                { 0x4f, 0x66, 0x75, 0x26 }, // end
                { 0x50, 0x67, 0x56, 0x27 }, // down
                { 0x51, 0x68, 0x76, 0x28 }, // pgdn
                { 0x52, 0x69, 0x57, 0x29 }, // insert
                { 0x53, 0x6a, 0x58, 0x2a }, // delete
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

#if defined(DEBUG_GETC) && defined(_DEBUG)
    printf("\n%08x '%c'", key_char, key_char);
#endif

    SetConsoleMode(handle, mode);
    return key_char;
}

//------------------------------------------------------------------------------
int getc_impl(FILE* stream)
{
    int alt;
    int i;
    while (1)
    {
        wchar_t wc[2];
        char utf8[4];

        alt = 0;
        i = GETWCH_IMPL(&alt);

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
        if (i < 0x80 || i == 0xe0)
        {
            break;
        }

        // Convert to utf-8 and insert directly into rl's line buffer.
        wc[0] = (wchar_t)i;
        wc[1] = L'\0';

        WideCharToMultiByte(
            CP_UTF8, 0,
            wc, -1,
            utf8, sizeof(utf8),
            NULL,
            NULL
        );

        rl_insert_text(utf8);
        rl_redisplay();
    }

    alt = RL_ISSTATE(RL_STATE_MOREINPUT) ? 0 : alt;
    alt = alt ? 0x80 : 0;
    return i|alt;
}

// vim: expandtab
