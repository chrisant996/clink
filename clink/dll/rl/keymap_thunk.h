// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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
