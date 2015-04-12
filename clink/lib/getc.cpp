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

extern "C" {
extern int      _rl_vis_botlin;
extern int      _rl_last_c_pos;
extern int      _rl_last_v_pos;
} // extern "C"

//------------------------------------------------------------------------------
void on_terminal_resize()
{
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
