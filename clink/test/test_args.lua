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
s = clink.arg.new_parser()
s:set_arguments({ "one" , "two" })

r = clink.arg.new_parser()
r:set_arguments({ "five", "six" })
r:loop()

q = clink.arg.new_parser()
q:set_arguments({ "four" .. r })

p = clink.arg.new_parser()
p:set_arguments(
    {
        "one",
        "two",
        "three" .. q,
        "spa ce" .. s,
    }
)

clink.arg.register_parser("argcmd", p)

clink.test.test_matches(
    "Node matches 1",
    "argcmd ",
    { "one", "two", "three", "spa ce" }
)

clink.test.test_matches(
    "Node matches 2",
    "argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Node matches 3 (.exe)",
    "argcmd.exe t",
    { "two", "three" }
)

clink.test.test_matches(
    "Node matches 4 (.bat)",
    "argcmd.bat t",
    { "two", "three" }
)

clink.test.test_matches(
    "Node matches quoted 1",
    "argcmd \"t",
    { "two", "three" }
)

clink.test.test_matches(
    "Node matches quoted executable",
    "\"argcmd\" t",
    { "two", "three" }
)

clink.test.test_output(
    "Key as only match.",
    "argcmd three",
    "argcmd three four "
)

clink.test.test_matches(
    "Simple traversal 1",
    "argcmd three four ",
    { "five", "six" }
)

clink.test.test_output(
    "Simple traversal 2",
    "argcmd th\tf",
    "argcmd three four "
)

clink.test.test_output(
    "Simple traversal 3",
    "argcmd one one one",
    "argcmd one one one"
)

clink.test.test_matches(
    "Simple traversal 4",
    "argcmd one one "
)

clink.test.test_matches(
    "Quoted traversal 1",
    "argcmd \"three\" four ",
    { "five", "six" }
)

clink.test.test_matches(
    "Quoted traversal 2a",
    "argcmd three four \"",
    { "five", "six" }
)

clink.test.test_output(
    "Quoted traversal 2b",
    "argcmd three four \"fi",
    "argcmd three four \"five\" "
)

clink.test.test_output(
    "Quoted traversal 2c",
    "argcmd three four \"five\" five five s",
    "argcmd three four \"five\" five five six "
)

clink.test.test_output(
    "Quoted traversal 3",
    "argcmd \"three\"",
    "argcmd three four "
)

clink.test.test_matches(
    "Quoted traversal 4",
    "argcmd \"spa ce\" ",
    { "one", "two" }
)

clink.test.test_output(
    "Quoted traversal 5",
    "argcmd spa",
    "argcmd \"spa ce\" "
)

clink.test.test_matches(
    "Loop property: basic",
    "argcmd three four six \t",
    { "five", "six" }
)

clink.test.test_matches(
    "Loop property: miss",
    "argcmd three four green four \t",
    { "five", "six" }
)

clink.test.test_matches(
    "Separator && 1",
    "nullcmd && argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator && 1",
    "nullcmd &&argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator && 2",
    "nullcmd \"&&\" && argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator && 3",
    "nullcmd \"&&\"&&argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator &",
    "nullcmd & argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator | 1",
    "nullcmd | argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator | 2",
    "nullcmd|argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator multiple 1",
    "nullcmd | nullcmd && argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Separator multiple 2",
    "nullcmd | nullcmd && argcmd |argcmd t",
    { "two", "three" }
)

clink.test.test_output(
    "Not separator",
    "argcmd three four \"  &&foobar\" f",
    "argcmd three four \"  &&foobar\" five "
)

clink.test.test_matches(
    "Path: relative",
    ".\\foo\\bar\\argcmd t",
    { "two", "three" }
)

clink.test.test_matches(
    "Path: absolute",
    "c:\\foo\\bar\\argcmd t",
    { "two", "three" }
)

--------------------------------------------------------------------------------
p = clink.arg.new_parser()
p:set_arguments({
    "true",
    "sub_parser" .. clink.arg.new_parser():disable_file_matching(),
    "this_parser"
})

clink.arg.register_parser("argcmd_file", p)

clink.test.test_matches(
    "File matching enabled",
    "argcmd_file true "
)

clink.test.test_output(
    "File matching disabled: sub",
    "argcmd_file sub_parser ",
    "argcmd_file sub_parser "
)

p:disable_file_matching()
clink.test.test_output(
    "File matching disabled: this",
    "argcmd_file this_parser ",
    "argcmd_file this_parser "
)

--------------------------------------------------------------------------------
clink.arg.register_parser("argcmd_table", {"two", "three", "one"});

clink.test.test_matches(
    "Parserless: table",
    "argcmd_table t",
    { "two", "three" }
)

--------------------------------------------------------------------------------
q = clink.arg.new_parser()
q:set_arguments({ "four", "five" })

p = clink.arg.new_parser()
p:set_arguments(
	{ "one", "onetwo", "onethree" } .. q
)
clink.arg.register_parser("argcmd_substr", p);

clink.test.test_matches(
    "Full match is also partial match 1",
    "argcmd_substr one",
    { "one", "onetwo", "onethree" }
)

clink.test.test_matches(
    "Full match is also partial match 2",
    "argcmd_substr one f",
    { "four", "five" }
)

--------------------------------------------------------------------------------
local tbl_1 = { "one", "two", "three" }
local tbl_2 = { "four", "five", tbl_1 }

q = clink.arg.new_parser()
q:set_arguments({ "fifth", tbl_2 })

p = clink.arg.new_parser()
p:set_arguments({ "once", tbl_1 } .. q)

clink.arg.register_parser("argcmd_nested", p)

clink.test.test_matches(
    "Nested table: simple",
    "argcmd_nested on",
    { "once", "one" }
)

clink.test.test_matches(
    "Nested table: sub-parser",
    "argcmd_nested once f",
    { "fifth", "four", "five" }
)

--------------------------------------------------------------------------------
q = clink.arg.new_parser()
q:set_arguments({ "two", "three" }, { "four", "banana" })
q:loop()

p = clink.arg.new_parser()
p:set_arguments({ "one" }, q)

clink.arg.register_parser("argcmd_parser", p)

clink.test.test_matches(
    "Nested full parser",
    "argcmd_parser one t",
    { "two", "three" }
)

clink.test.test_matches(
    "Nested full parser - loop",
    "argcmd_parser one two four t",
    { "two", "three" }
)

--------------------------------------------------------------------------------
p = clink.arg.new_parser()
p:set_flags("/one", "/two", "/twenty")

q = clink.arg.new_parser()
q:set_flags("-one", "-two", "-twenty")

clink.arg.register_parser("argcmd_flags_s", p)
clink.arg.register_parser("argcmd_flags_d", q)

clink.test.test_output(
    "Flags: slash 1a",
    "argcmd_flags_s nothing /",
    "argcmd_flags_s nothing /"
)

clink.test.test_output(
    "Flags: slash 1b",
    "argcmd_flags_s /",
    "argcmd_flags_s /"
)

p:disable_file_matching()

clink.test.test_output(
    "Flags: slash 1c",
    "argcmd_flags_s /",
    "argcmd_flags_s /"
)

clink.test.test_output(
    "Flags: slash 1d",
    "argcmd_flags_s nothing /",
    "argcmd_flags_s nothing /"
)

clink.test.test_matches(
    "Flags: slash 2a",
    "argcmd_flags_s nothing /tw",
    { "/two", "/twenty" }
)

clink.test.test_matches(
    "Flags: slash 2b",
    "argcmd_flags_s out of bounds /tw",
    { "/two", "/twenty" }
)

clink.test.test_matches(
    "Flags: slash 2c",
    "argcmd_flags_s /tw",
    { "/two", "/twenty" }
)

clink.test.test_output(
    "Flags: dash 1",
    "argcmd_flags_d -t",
    "argcmd_flags_d -tw"
)

clink.test.test_matches(
    "Flags: dash 2",
    "argcmd_flags_d -tw",
    { "-two", "-twenty" }
)

--------------------------------------------------------------------------------
q = clink.arg.new_parser()
q:set_arguments({ "two", "three" })

p = clink.arg.new_parser()
p:set_arguments({ "one" }, { "nine" })
p:set_flags("-flag_a" .. q, "-flag_b" .. q)

clink.arg.register_parser("argcmd_skip", p)

clink.test.test_matches(
    "Skip 1",
    "argcmd_skip one two -fla\t",
    { "-flag_a", "-flag_b" }
)

clink.test.test_matches(
    "Skip 2",
    "argcmd_skip one two -flag_a t",
    { "two", "three" }
)

clink.test.test_matches(
    "Skip 3",
    "argcmd_skip one two -flag_a two four five -f\t",
    { "-flag_a", "-flag_b" }
)

--------------------------------------------------------------------------------
p = clink.arg.new_parser(
    "-flag" .. clink.arg.new_parser({ "red", "green", "blue"}),
    { "one", "two", "three" },
    { "four", "five" }
)

clink.arg.register_parser("argcmd_lazy", p)

clink.test.test_output(
    "Lazy init 1",
    "argcmd_lazy o",
    "argcmd_lazy one f"
)

clink.test.test_matches(
    "Lazy init 2",
    "argcmd_lazy one f",
    { "four", "five" }
)

clink.test.test_matches(
    "Lazy init 2",
    "argcmd_lazy one four -flag ",
    { "red", "green", "blue" }
)
