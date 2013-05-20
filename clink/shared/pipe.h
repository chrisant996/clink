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

#ifndef PIPE_H
#define PIPE_H

//------------------------------------------------------------------------------
typedef struct 
{
    HANDLE  read;
    HANDLE  write;
} pipe_t;

enum
{
    ReadHandleInheritable   = 1 << 0,
    WriteHandleInheritable  = 1 << 1,
};

//------------------------------------------------------------------------------
int     create_pipe(int, pipe_t*);
void    destroy_pipe(pipe_t*);
HANDLE  duplicate_handle(HANDLE, DWORD);
void    duplicate_pipe(pipe_t*, const pipe_t*, DWORD);

#endif // PIPE_H
