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

#include "pch.h"

#define ANSI_X_COMPILE

//------------------------------------------------------------------------------

#define char_t          char
#define ANSI_FNAME(x)   x
#define ansi_str_len(x) strlen(x)

#include "ansi.x"

#undef ansi_str_len
#undef char_t
#undef ANSI_FNAME

//------------------------------------------------------------------------------

#define char_t          wchar_t
#define ANSI_FNAME(x)   x##_w
#define ansi_str_len(x) wcslen(x)

#include "ansi.x"

#undef ansi_str_len
#undef char_t
#undef ANSI_FNAME

#undef ANSI_X_COMPILE

// vim: expandtab
