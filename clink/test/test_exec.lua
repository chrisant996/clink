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
local env_path = clink.test.test_fs({
    "one_path.exe",
    "one_two.py",
    "one_three.txt"
})

clink.test.test_fs({
    one_dir = {
        "two_dir_local.exe",
        "two_dir_local.txt"
    },
    "one_local.exe",
    "two_local.exe",
    "one_local.txt"
})

function clink.get_env(name)
    name = name:lower()
    if name == "path" then
        return env_path
    elseif name == "pathext" then
        return ".exe;.py"
    else
        error("BAD ENV:"..name)
    end
end

clink.test.test_output(
    "PATH",
    "one_p",
    "one_path.exe "
)

clink.test.test_output(
    "PATH case mapped",
    "one-p",
    "one_path.exe "
)

clink.test.test_matches(
    "PATH matches",
    "one_",
    { "one_path.exe", "one_two.py" }
)

clink.test.test_matches(
    "Cmd.exe commands",
    "p",
    { "path", "pause", "popd", "prompt", "pushd" }
)

clink.test.test_matches(
    "Relative path",
    ".\\",
    { "one_local.exe", "two_local.exe", "one_dir\\" }
)

clink.test.test_output(
    "Relative path dir",
    ".\\one_dir\\",
    ".\\one_dir\\two_dir_local.exe "
)

--------------------------------------------------------------------------------
local exec_match_style = 0
local old_getter = clink.get_setting_int
function clink.get_setting_int(name)
    if name == "exec_match_style" then
        return exec_match_style
    end

    return old_getter(name)
end

exec_match_style = 1
clink.test.test_matches(
    "Style - cwd (no dirs)",
    "one_",
    { "one_local.exe", "one_path.exe", "one_path.py" }
)

exec_match_style = 2
clink.test.test_matches(
    "Style - cwd (all)",
    "one-",
    { "one_local.exe", "one_path.exe", "one_path.py", "one_dir" }
)

-- vim: expandtab
