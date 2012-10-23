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

//-----------------------------------------------------------------------------
#ifndef ANSI_X_COMPILE
#   error ansi.x is not to be compiled directly!!
#endif

//------------------------------------------------------------------------------
const char_t* ANSI_FNAME(find_next_ansi_code)(const char_t* buffer, int* size)
{
    const char_t* read;
    int state;

    read = buffer;
    state = -1;
    while (*read)
    {
        if (state < 0)
        {
            // CSI
            if ((read[0] == 0x1b) && (read[1] == '['))
            {
                state = read - buffer;
                ++read;
            }
            else if (read[0] == 0x9b)
            {
                state = read - buffer;
            }
            else if ((read[0] == 0xc2) && (read[1] == 0x9b))
            {
                state = read - buffer;
            }
        }
        else
        {
            char c = *read;
            if ((c < '0' || c > '9') && c != ';')
            {
                buffer += state;
                if (size != NULL)
                {
                    *size = (int)(read - buffer);
                    *size += 1;
                }

                return buffer;
            }
        }

        ++read;
    }

    return NULL;
}

// vim: expandtab syntax=c
