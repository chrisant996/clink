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

#ifndef CLINK_PCH
#define CLINK_PCH

#include <stdio.h>
#include <conio.h>
#include <io.h>
#include <locale.h>
#include <stdlib.h>
#ifdef __MINGW32__
#   include <stdint.h>
#endif

#include <Windows.h>
#ifndef __MINGW32__
#   include <DbgHelp.h>
#else
    typedef void* PCONSOLE_READCONSOLE_CONTROL;
#endif

#ifdef CLINK_USE_READLINE
#   include <readline/readline.h>
#   include <readline/history.h>
#   include <readline/rldefs.h>
#endif

#ifdef CLINK_USE_LUA
#   include "lua.h"
#   include "lauxlib.h"
#   include "lualib.h"
#endif

#define sizeof_array(x) (sizeof(x)/sizeof(x[0]))

#endif // CLINK_PCH
