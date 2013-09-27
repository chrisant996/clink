--
-- Copyright (c) 2013 Martin Ridgers
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
