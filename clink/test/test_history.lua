--
-- Copyright (c) 2014 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

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

-- vim: expandtab
