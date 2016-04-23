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
static int ANSI_FNAME(is_osc)(const char_t* str)
{
    if ((str[0] == 0x1b) && (str[1] == ']'))  return 2;
    if ((str[0] == 0xc2) && (str[1] == 0x9d)) return 2;
    if (str[0] == 0x9d)                       return 1;

    // Also lump DCS under OSC too.
    if ((str[0] == 0x1b) && (str[1] == 'P'))  return 2;
    if ((str[0] == 0xc2) && (str[1] == 0x90)) return 2;
    if (str[0] == 0x90)                       return 1;

    return 0;
}

//------------------------------------------------------------------------------
static int ANSI_FNAME(is_csi)(const char_t* str)
{
    if ((str[0] == 0x1b) && (str[1] == '['))  return 2;
    if ((str[0] == 0xc2) && (str[1] == 0x9b)) return 2;
    if (str[0] == 0x9b)                       return 1;

    return 0;
}

//------------------------------------------------------------------------------
const char_t* ANSI_FNAME(find_next_ansi_code)(const char_t* buffer, int* size)
{
    int size_temp;
    const char_t* read;
    int state;
    int osc;

    if (size == NULL)
    {
        size = &size_temp;
    }

    read = buffer;
    state = -1;
    osc = 0;
    while (*read)
    {
        if (state < 0)
        {
            osc = ANSI_FNAME(is_osc)(read);
            int csi_len = ANSI_FNAME(is_csi)(read) + osc;
            if (csi_len)
            {
                state = read - buffer;
                read += (csi_len > 1);
            }
        }
        else
        {
            int done;

            char c = *read;
            if (osc)
                done = (c == 0x9c) || (c == 0x1b && read[1] == '\\');
            else
                done = (c < '0' || c > '9') && c != ';';

            if (done)
            {
                buffer += state;

                *size = (int)(read - buffer);
                *size += 1 + !!osc;

                return buffer;
            }
        }

        ++read;
    }

    *size = 0;
    return (buffer +  ansi_str_len(buffer));
}

//------------------------------------------------------------------------------
int ANSI_FNAME(parse_ansi_code)(const char_t* code, int* params, int max_params)
{
    int i;
    int command;
    int csi_len;

    csi_len = ANSI_FNAME(is_csi)(code);
    if (csi_len <= 0)
    {
        return -1;
    }

    code += csi_len;
    i = 0;
    command = -1;

    params[i] = 0;
    --max_params;       // one param must be -1 terminator.

    while (*code)
    {
        // Accumilate numerics.
        unsigned int n = (unsigned int)*code - '0';
        if (n < 10)
        {
            if (i < max_params)
            {
                params[i] *= 10;
                params[i] += n;
            }

            ++code;
            continue;
        }

        // Still room to move onto the next parameter?
        if (i < max_params)
        {
            ++i;
            params[i] = 0;
        }

        // Anything but [0-9;] indicates the end of the sequence
        if (*code != ';')
        {
            command = *code;
            break;
        }

        ++code;
    }

    params[i] = -1;
    return command;
}

// vim: expandtab syntax=c
