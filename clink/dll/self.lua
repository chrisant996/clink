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
local function starts_with_hyphen(word)
    if word:sub(1, 1) == "-" then
        return 2
    end

    return 1
end

--------------------------------------------------------------------------------
local clag = clink.arg
local self_tree = clag.node(
    "--help" .. clag.node(true),
    "inject" .. clag.node(
        "--althook",
        "--help",
        "--nohostcheck",
        "--pid" .. clag.node(true),
        "--profile" .. clag.node(false),
        "--quiet",
        "--scripts" .. clag.node(false)
    ):loop(),
    "autorun" .. clag.node(
        "--install" .. clag.node(true),
        "--uninstall" .. clag.node(true),
        "--show" .. clag.node(true),
        "--value" .. clag.node(false),
        "--help" .. clag.node(true)
    )
)

clink.arg.register_tree("clink", self_tree)

-- vim: expandtab
