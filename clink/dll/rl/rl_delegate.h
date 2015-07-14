// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

template <typename CLASS, int UNIQUE>
struct _rl_delegate
{
    template <class T, class RET, class... ARGS>
    static RET (*make(T* t, RET (T::*f)(ARGS...)))(ARGS...)
    {
        static T* self = t;
        static RET (T::*func)(ARGS...) = f;

        struct thunk
        {
            static RET impl(ARGS... args)
            {
                return (self->*func)(args...);
            }
        };

        return &thunk::impl;
    }
};

#define rl_delegate(ths, cls, mtd)\
    _rl_delegate<cls, __COUNTER__>::make(ths, &cls::mtd)
