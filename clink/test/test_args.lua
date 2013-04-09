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
local clag = clink.arg

--------------------------------------------------------------------------------
local t = clag.node(
    "one",
    "two",
    "three" .. clag.node("four" .. clag.node("five", "six")):loop()
)
--clag.print_tree(t)
clag.register_tree("argcmd", t)

clink.test.test_matches(
    "Node matches 1",
    "argcmd ",
    { "one", "two", "three" }
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
    "Quoted traversal 2",
    "argcmd three four \"",
    { "five", "six" }
)

clink.test.test_output(
    "Quoted traversal 3",
    "argcmd \"three\"",
    "argcmd \"three\" four "
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
    "argcmd three four \"  &&foobar\" four "
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
local t = clag.node(
    "true" .. clag.node(true),
    "false" .. clag.node(false)
)
--clag.print_tree(t)
clag.register_tree("argcmd_bool", t)

clink.test.test_output(
    "Boolean: true",
    "argcmd_bool true ",
    "argcmd_bool true "
)

clink.test.test_matches(
    "Boolean: false",
    "argcmd_bool false "
)

--------------------------------------------------------------------------------
local t = clag.node(
    "eleven"
)

clag.register_tree("argcmd", t)

clink.test.test_matches(
    "Merged trees",
    "argcmd ",
    { "one", "two", "three", "eleven" }
)

--------------------------------------------------------------------------------
local t = clag.node(
    clag.condition(
        function (word)
            word = word:sub(1, 1)
            if word == "1" then
                return -100
            elseif word == "2" then
                return 2
            else
                return 300
            end
        end,
        10,
        clag.node(20):loop(),
        "key" .. clag.node("one", "two", "three")
    ):loop()
)
clag.register_tree("argcmd_condition", t);

clink.test.test_output(
    "Conditional: under bound",
    "argcmd_condition 1",
    "argcmd_condition 10 key "
)

clink.test.test_output(
    "Conditional: in bound",
    "argcmd_condition 2",
    "argcmd_condition 20 20 "
)

clink.test.test_output(
    "Conditional: over bound",
    "argcmd_condition k",
    "argcmd_condition key "
)

clink.test.test_matches(
    "Conditional: traversal",
    "argcmd_condition key t",
    { "two", "three" }
)

clink.test.test_output(
    "Conditional: loop self",
    "argcmd_condition 10 k",
    "argcmd_condition 10 key "
)

clink.test.test_output(
    "Conditional: loop choice",
    "argcmd_condition 20 ",
    "argcmd_condition 20 20 20 "
)

clink.test.test_output(
    "Conditional: loop choice",
    "argcmd_condition 20 20 k",
    "argcmd_condition 20 20 k"
)

--------------------------------------------------------------------------------
local t = clag.condition(
    function (word)
        return 2
    end,
    false,
    clag.node("one", "two", "three")
):loop()
clag.register_tree("argcmd_condition_as_root", t);

clink.test.test_matches(
    "Condition as root",
    "argcmd_condition_as_root t",
    { "two", "three" }
)

clink.test.test_output(
    "Condition as root (looping)",
    "argcmd_condition_as_root one two th",
    "argcmd_condition_as_root one two three "
)

--------------------------------------------------------------------------------
clag.register_tree("argcmd_table", {"two", "three", "one"});

clink.test.test_matches(
    "Tree-less: table",
    "argcmd_table t",
    { "two", "three" }
)

--------------------------------------------------------------------------------
local t = clag.node(
	{ "one", "onetwo", "onethree" } .. clag.node("four", "five")
)
clag.register_tree("argcmd_substr", t);

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
