// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#if TODO

--------------------------------------------------------------------------------
local p
local q
local r
local s

--------------------------------------------------------------------------------
q = clink.arg.new_parser():set_arguments({ "sub_p" })
p = clink.arg.new_parser()
p:set_flags("-flg_p", "-flg_ps")
p:set_arguments(
    {
        "sub_p______" .. q,
        "sub_p_sub_s" .. q,
        "sub_p_str_s" .. q,
        "str_p______",
        "str_p_sub_s",
        "str_p_str_s",
    },
    { "str_p" }
)

r = clink.arg.new_parser():set_arguments({ "sub_s" })
s = clink.arg.new_parser()
s:set_flags("-flg_s", "-flg_ps")
s:set_arguments(
    {
        "______sub_s" .. r,
        "sub_p_sub_s" .. r,
        "sub_p_str_s",
        "______str_s",
        "str_p_sub_s" .. r,
        "str_p_str_s",
    },
    { "str_s" }
)

clink.arg.register_parser("argcmd_merge", p)
clink.arg.register_parser("argcmd_merge", s)

local merge_tests = {
    { "flg_p_flg_s -flg_", { "-flg_p", "-flg_s", "-flg_ps" } },

    { "______str_s", "str_s" },
    { "______sub_s", "sub_s" },
    { "str_p______", "str_p" },
    { "sub_p______", "sub_p" },

    -- Disabled as merge_parsers is currently too naive to cater for this case.
    --{ "str_p_str_s", { "str_p", "str_s" } },
    --{ "str_p_sub_s", { "str_p", "sub_s" } },
    --{ "sub_p_str_s", { "sub_p", "str_s" } },
    --{ "sub_p_sub_s", { "sub_p", "sub_s" } },
}

for _, i in ipairs(merge_tests) do
    local test, result = i[1], i[2]

    local test_name = "Merge: "..test
    local cmd = "argcmd_merge "..test
    if type(result) == "string" then
        clink.test.test_output(test, cmd, cmd.." "..result.." ")
    else
        clink.test.test_matches(test, cmd.."\t", result)
    end
end

#endif // TODO
