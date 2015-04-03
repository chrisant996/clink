/* Copyright (c) 2015 Martin Ridgers
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

#ifndef KEYMAP_THUNK_H
#define KEYMAP_THUNK_H

#define MAKE_KEYMAP_THUNK(self, cls, func)                      \
    int (*func##_thunk)(int, int);                              \
    {                                                           \
        static cls* func##_self = self;                         \
        static void (cls::*func##_func)(int, int) = &cls::func; \
        struct _func##_thunk {                                  \
            static int impl(int a, int b) {                     \
                (func##_self->*func##_func)(a, b);              \
                return 0;                                       \
            }                                                   \
        };                                                      \
        func##_thunk = _func##_thunk::impl;                     \
    }

#endif // KEYMAP_THUNK_H

// vim: expandtab
