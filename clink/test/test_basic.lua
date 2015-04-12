--
-- Copyright (c) 2012 Martin Ridgers
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
clink.test.test_matches(
    "File system matches",
    "nullcmd "
)

clink.test.test_output(
    "Single file",
    "nullcmd file1",
    "nullcmd file1 "
)

clink.test.test_output(
    "Single dir",
    "nullcmd dir1",
    "nullcmd dir1\\"
)

clink.test.test_output(
    "Dir slash flip",
    "nullcmd dir1/",
    "nullcmd dir1\\"
)

clink.test.test_output(
    "Path slash flip",
    "nullcmd dir1/on",
    "nullcmd dir1\\only "
)

clink.test.test_matches(
    "Case mapping matches",
    "nullcmd case-m\t",
    { "case_map-1", "case_map_2" }
)

clink.test.test_output(
    "Case mapping output",
    "nullcmd case-m",
    "nullcmd case_map"
)

clink.test.test_output(
    "Case mapping complex",
    "nullcmd case_map-",
    "nullcmd case_map-"
)
