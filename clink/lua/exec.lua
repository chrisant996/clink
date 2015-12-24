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
local dos_commands = {
    "assoc", "break", "call", "cd", "chcp", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "diskcomp", "diskcopy", "echo", "endlocal", "erase",
    "exit", "for", "format", "ftype", "goto", "graftabl", "if", "md", "mkdir",
    "mklink", "more", "move", "path", "pause", "popd", "prompt", "pushd", "rd",
    "rem", "ren", "rename", "rmdir", "set", "setlocal", "shift", "start",
    "time", "title", "tree", "type", "ver", "verify", "vol"
}

--------------------------------------------------------------------------------
local function get_environment_paths()
    local paths = clink.split(clink.get_env("PATH"), ";")

    -- We're expecting absolute paths and as ';' is a valid path character
    -- there maybe unneccessary splits. Here we resolve them.
    local paths_merged = { paths[1] }
    for i = 2, #paths, 1 do
        if not paths[i]:find("^[a-zA-Z]:") then
            local t = paths_merged[#paths_merged];
            paths_merged[#paths_merged] = t..paths[i]
        else
            table.insert(paths_merged, paths[i])
        end
    end

    -- Append slashes.
    for i = 1, #paths_merged, 1 do
        paths_merged[i] = paths_merged[i].."/"
    end

    return paths_merged
end

--------------------------------------------------------------------------------
local function exec_find_dirs(pattern, case_map)
    local ret = {}

    for _, dir in ipairs(clink.find_dirs(pattern, case_map)) do
        if dir ~= "." and dir ~= ".." then
            table.insert(ret, dir)
        end
    end

    return ret
end

--------------------------------------------------------------------------------
local function exec_match_generator(text, first, last)
    -- If match style setting is < 0 then consider executable matching disabled.
    local match_style = clink.get_setting_int("exec_match_style")
    if match_style < 0 then
        return false
    end

    -- We're only interested in exec completion if this is the first word of the
    -- line, or the first word after a command separator.
    if clink.get_setting_int("space_prefix_match_files") > 0 then
        if first > 1 then
            return false
        end
    else
        local leading = rl_state.line_buffer:sub(1, first - 1)
        local is_first = leading:find("^%s*\"*$")
        if not is_first then
            return false
        end
    end

    -- Split text into directory and name
    local text_dir = ""
    local text_name = text
    local i = text:find("[\\/:][^\\/:]*$")
    if i then
        text_dir = text:sub(1, i)
        text_name = text:sub(i + 1)
    end

    local paths
    if not text:find("[\\/:]") then
        -- If the terminal is cmd.exe check it's commands for matches.
        if clink.get_host_process() == "cmd.exe" then
            clink.match_words(text, dos_commands)
        end

        paths = get_environment_paths();
    else
        paths = {}

        -- 'text' is an absolute or relative path. If we're doing Bash-style
        -- matching should now consider directories.
        if match_style < 1 then
            match_style = 2
        else
            match_style = 1
        end
    end

    -- Should we also consider the path referenced by 'text'?
    if match_style >= 1 then
        table.insert(paths, text_dir)
    end

    -- Search 'paths' for files ending in 'suffices' and look for matches
    local suffices = clink.split(clink.get_env("pathext"), ";")
    for _, suffix in ipairs(suffices) do
        for _, path in ipairs(paths) do
            local files = clink.find_files(path.."*"..suffix, false)
            for _, file in ipairs(files) do
                if clink.is_match(text_name, file) then
                    clink.add_match(text_dir..file)
                end
            end
        end
    end

    -- Lastly we may wish to consider directories too.
    if clink.match_count() == 0 or match_style >= 2 then
        clink.match_files(text.."*", true, exec_find_dirs)
    end

    clink.matches_are_files()
    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)

-- vim: expandtab
