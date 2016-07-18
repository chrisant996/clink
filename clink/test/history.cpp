// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#if MODE4

--------------------------------------------------------------------------------
local prime_history = {
    "cmd1 arg1 arg2 arg3 arg4",
    "cmd2 arg1 arg2 arg3 arg4 extra",
    "cmd3 arg1 arg2 arg3 arg4",
}

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-P 1",
    { prime_history, "" },
    prime_history[3]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-P 2",
    { prime_history, "" },
    prime_history[2]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-P 3",
    { prime_history, "" },
    prime_history[1]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-P 4",
    { prime_history, "" },
    prime_history[1]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-N 1",
    { prime_history, "abc" },
    "abc"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Ctrl-N 2",
    { prime_history, "" },
    prime_history[2]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!",
    { prime_history, "!!" },
    prime_history[3]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !string",
    { prime_history, "!cmd2" },
    prime_history[2]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !1",
    { prime_history, "!1" },
    prime_history[1]
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !0",
    { prime_history, "!0" },
    "!0"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !#",
    "one two !#",
    "one two one two "
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !?string",
    { "one two", "three !?one" },
    "three one two"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !$",
    { prime_history, "cmdX !$" },
    "cmdX arg4"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!:$",
    { prime_history, "cmdX !!:$" },
    "cmdX arg4"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!:N-$",
    { prime_history, "cmdX !!:3-$" },
    "cmdX arg3 arg4"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!:N*",
    { prime_history, "cmdX !!:2*" },
    "cmdX arg2 arg3 arg4"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!:N",
    { prime_history, "cmdX !!:2" },
    "cmdX arg2"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !!:-N",
    { prime_history, "cmdX !!:-1" },
    "cmdX cmd3 arg1"
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion ^X^Y^",
    { prime_history, "^arg1^123^" },
    prime_history[3]:gsub("arg1", "123")
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !X:s/Y/Z",
    { prime_history, "!cmd1:s/arg1/123" },
    prime_history[1]:gsub("arg1", "123")
)

--------------------------------------------------------------------------------
clink.test.test_output(
    "Expansion !?X?:",
    { prime_history, "cmdX !?extra?:*" },
    prime_history[2]:gsub("cmd2", "cmdX")
)

#endif // MODE4
