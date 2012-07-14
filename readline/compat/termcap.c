/* Copyright (c) 2012 Martin Ridgers
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

#include <Windows.h>

#define sizeof_array(x) (sizeof(x) / sizeof(x[0]))

//------------------------------------------------------------------------------
void move_cursor(int dx, int dy)
{
    CONSOLE_SCREEN_BUFFER_INFO i;
    COORD o;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i);
    o.X = i.dwCursorPosition.X + dx;
    o.Y = i.dwCursorPosition.Y + dy;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), o);
}

//------------------------------------------------------------------------------
void clear_to_eol()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int i;
    int width;
    DWORD length;
    const char space[] = "                ";

    GetConsoleScreenBufferInfo(handle, &csbi);
    width = csbi.srWindow.Right - csbi.srWindow.Left;
    length = sizeof_array(space) - 1;

    for (i = csbi.dwCursorPosition.X; i < width; i += length)
    {
        DWORD to_write = width - i;
        if (to_write > length)
        {
            to_write = length;
        }

        WriteConsole(handle, space, to_write, &to_write, NULL);
    }

    SetConsoleCursorPosition(handle, csbi.dwCursorPosition);
}

//------------------------------------------------------------------------------
void clear_screen()
{
    // Scroll the whole buffer off the top. This is exactly what cmd.exe does!

    HANDLE handle;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SMALL_RECT full_buffer;
    COORD dest_origin;
    COORD cursor_pos;
    CHAR_INFO fill;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(handle, &csbi);

    full_buffer.Left = 0;
    full_buffer.Top = 0;
    full_buffer.Right = csbi.dwMaximumWindowSize.X;
    full_buffer.Bottom = csbi.dwMaximumWindowSize.Y;
    dest_origin.X = 0;
    dest_origin.Y = -full_buffer.Bottom - 1;
    fill.Char.UnicodeChar = L' ';
    fill.Attributes = csbi.wAttributes;
    ScrollConsoleScreenBuffer(handle, &full_buffer, NULL, dest_origin, &fill);

    cursor_pos.X = 0;
    cursor_pos.Y = 0;
    SetConsoleCursorPosition(handle, cursor_pos);
}

//------------------------------------------------------------------------------
int tputs(const char *str, int affcnt, int (*putc_func)(int))
{
	switch (*str)
	{
	case '\b':
        move_cursor(-1, 0);
		break;

	case '\v':
        move_cursor(1, 0);
		break;

    case '\001':
        clear_to_eol();
        break;

    case '\002':
        clear_screen();
        break;

    default:
        while (*str)
        {
            putc_func(*str++);
        }
        break;
	}

    return 0;
}

//------------------------------------------------------------------------------
int tgetflag(char *capname)
{
    return 0;
}

//------------------------------------------------------------------------------
char* tgetstr(char* id, char** capname)
{
    return "";
}

//------------------------------------------------------------------------------
int tgetent(char *bp, const char *name)
{
    return 0;
}

//------------------------------------------------------------------------------
int tgetnum(char *id)
{
    return 0;
}
