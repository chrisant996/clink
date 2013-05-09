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
clink.test.test_fs({
    one_dir = { "leaf" },
    two_dir = { "leaf" },
    three_dir = { "leaf" },
    nest_1 = { nest_2 = { "leaf" } },

    "one_file",
    "two_file",
    "three_file",
    "four_file",
})

--------------------------------------------------------------------------------
for _, i in ipairs({"cd", "rd", "rmdir", "md", "mkdir", "pushd"}) do
    clink.test.test_matches(
        "Matches: "..i,
        i.." t",
        { "two_dir\\", "three_dir\\" }
    )

    clink.test.test_output(
        "Single (with -/_): "..i,
        i.." one-",
        i.." one_dir\\ "
    )

    clink.test.test_output(
        "Relative: "..i,
        i.." o\\..\\o",
        i.." o\\..\\one_dir\\ "
    )

    clink.test.test_matches(
        "No matches: "..i,
        i.." f",
        {}
    )

    clink.test.test_output(
        "Nested (forward slash): "..i,
        i.." nest_1/ne",
        i.." nest_1\\nest_2\\ " 
    )
end

-- vim: expandtab
