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
local exec_match_style = 0
local space_prefix_match_files = 0
local old_getter = clink.get_setting_int
function clink.get_setting_int(name)
    if name == "exec_match_style" then
        return exec_match_style
    elseif name == "space_prefix_match_files" then
        return space_prefix_match_files
    end

    return old_getter(name)
end

--------------------------------------------------------------------------------
local env_path = clink.test.test_fs({
    "spa ce.exe",
    "one_path.exe",
    "one_two.py",
    "one_three.txt"
})

clink.test.test_fs({
    one_dir = {
        "spa ce.exe",
        "two_dir_local.exe",
        "two_dir_local.txt"
    },
    foodir = {
        "two_dir_local.exe",
    },
    jumble = {
        "three.exe",
        "three-local.py",
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

clink.test.test_matches(
    "Nothing",
    "abc123",
    {}
)

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
    { "one_local.exe", "two_local.exe", "one_dir\\", "foodir\\", "jumble\\" }
)

clink.test.test_output(
    "Relative path dir",
    ".\\foodir\\",
    ".\\foodir\\two_dir_local.exe "
)

clink.test.test_output(
    "Relative path dir (with '_')",
    ".\\one_dir\\t",
    ".\\one_dir\\two_dir_local.exe "
)

clink.test.test_output(
    "Separator | 1",
    "nullcmd | one_p",
    "nullcmd | one_path.exe "
)

clink.test.test_output(
    "Separator | 2",
    "nullcmd |one_p",
    "nullcmd |one_path.exe "
)

clink.test.test_matches(
    "Separator & 1",
    "nullcmd & one_",
    { "one_path.exe", "one_two.py" }
)

clink.test.test_matches(
    "Separator & 2",
    "nullcmd &one_",
    { "one_path.exe", "one_two.py" }
)

clink.test.test_output(
    "Separator && 1",
    "nullcmd && one_p",
    "nullcmd && one_path.exe "
)

clink.test.test_output(
    "Separator && 2",
    "nullcmd &&one_p",
    "nullcmd &&one_path.exe "
)

clink.test.test_output(
    "Spaces (path)",
    "spa",
    "\"spa ce.exe\" "
)

clink.test.test_output(
    "Spaces (relative)",
    ".\\one_dir\\spa",
    "\".\\one_dir\\spa ce.exe\" "
)

clink.test.test_matches(
    "Separator false positive",
    "nullcmd \"&&\" o\t",
    { "one_local.exe", "one_local.txt", "one_dir\\" }
)

clink.test.test_output(
    "Last char . 1",
    "one_path.",
    "one_path.exe "
)

clink.test.test_output(
    "Last char . 2",
    "jumble\\three.",
    "jumble\\three.exe "
)

clink.test.test_output(
    "Last char -",
    "one_local-",
    "one_local-"
)

--------------------------------------------------------------------------------
exec_match_style = 1
clink.test.test_matches(
    "Style - cwd (no dirs) 1",
    "one_",
    { "one_local.exe", "one_path.exe", "one_two.py" }
)

exec_match_style = 2
clink.test.test_matches(
    "Style - cwd (all)",
    "one-\t",
    { "one_local.exe", "one_path.exe", "one_two.py", "one_dir\\" }
)

--------------------------------------------------------------------------------
exec_match_style = 0
space_prefix_match_files = 1
clink.test.test_matches(
    "Space prefix; none",
    "one_",
    { "one_path.exe", "one_two.py" }
)

clink.test.test_matches(
    "Space prefix; space",
    " one_",
    { "one_dir\\", "one_local.txt", "one_local.exe" }
)

clink.test.test_matches(
    "Space prefix; spaces",
    "   one_",
    { "one_dir\\", "one_local.txt", "one_local.exe" }
)
