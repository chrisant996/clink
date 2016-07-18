// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#if MODE4

--------------------------------------------------------------------------------
clink.test.test_fs({
    "pre_nospace",
    "pre_space 1",
    "pre_space_space 2"
})

clink.test.test_output(
    "No quote added",
    "nullcmd pr",
    "nullcmd pre_"
)

clink.test.test_output(
    "Quote added",
    "nullcmd pre_s",
    "nullcmd \"pre_space"
)

clink.test.test_output(
    "Quote added case mapped",
    "nullcmd pre-s",
    "nullcmd \"pre_space"
)

#endif // MODE4
