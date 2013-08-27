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
local match_style = 0
local dos_commands = {
    "assoc", "break", "call", "cd", "chcp", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "diskcomp", "diskcopy", "echo", "endlocal", "erase",
    "exit", "for", "format", "ftype", "goto", "graftabl", "if", "md", "mkdir",
    "mklink", "more", "move", "path", "pause", "popd", "prompt", "pushd", "rd",
    "rem", "ren", "rename", "rmdir", "set", "setlocal", "shift", "start",
    "time", "title", "tree", "type", "ver", "verify", "vol"
}

--------------------------------------------------------------------------------
local function dos_cmd_match_generator(text)
    local matches = {}
    for _, cmd in ipairs(dos_commands) do
        if clink.is_match(text, cmd) then
            table.insert(matches, cmd)
        end
    end

    return matches
end

--------------------------------------------------------------------------------
local function dos_and_dir_match_generators(text)
    local matches = dos_cmd_match_generator(text)

    local dirs = dir_match_generator_impl(text)
    for _, i in ipairs(dirs) do
        table.insert(matches, i)
    end

    return matches
end

--------------------------------------------------------------------------------
local function build_passes(text)
    local passes = {}

    -- If there's no path separator in text then consider the environment's path
    -- as a first pass for matches.
    if not text:find("[\\/:]") then
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
            table.insert(paths, paths_merged[i].."\\")
        end

        -- Depending on match style add empty path so 'text' is used.
        if match_style > 0 then
            table.insert(paths, "")
        end

        -- Should directories be considered too?
        local extra_func = dos_cmd_match_generator
        if match_style > 1 then
            extra_func = dos_and_dir_match_generators
        end

        table.insert(passes, { paths=paths, func=extra_func })
    end

    -- The fallback solution is to use 'text' to find matches, and also add
    -- directories.
    table.insert(passes, { paths={""}, func=dir_match_generator_impl })

    return passes
end

--------------------------------------------------------------------------------
local function exec_match_generator(text, first, last)
    -- We're only interested in exec completion if this is the first word of the
    -- line, or the first word after a command separator.
    local leading = rl_state.line_buffer:sub(1, first - 1)
    local is_first = leading:find("^%s*\"*$")
    if not is_first then
        return false
    end

    -- Strip off possible trailing extension.
    local needle = text
    local ext_a, ext_b = needle:find("%.[a-zA-Z]*$")
    if ext_a then
        needle = needle:sub(1, ext_a - 1)
    end

    -- Strip off any path components that may be on text
    local prefix = ""
    local i = text:find("[\\/:][^\\/:]*$")
    if i then
        prefix = text:sub(1, i)
    end

    match_style = clink.get_setting_int("exec_match_style")
    if match_style < 0 then
        match_style = 2
    end

    local passes = build_passes(text)

    -- Combine extensions, text, and paths to find matches - this is done in two
    -- passes, the second pass possibly being "local" if the system-wide search
    -- didn't find any results.
    local n = #passes
    local exts = clink.split(clink.get_env("PATHEXT"), ";")
    for p = 1, n do
        local pass = passes[p]
        for _, ext in ipairs(exts) do
            for _, path in ipairs(pass.paths) do
                local mask = path..needle.."*"..ext
                for _, file in ipairs(clink.find_files(mask, true)) do
                    file = prefix..file
                    if clink.is_match(text, file) then
                        clink.add_match(file)
                    end
                end
            end
        end
        
        if pass.func then
            clink.add_match(pass.func(text))
        end

        -- Was there matches? Then there's no need to make any further passes.
        if clink.match_count() > 0 then
            break
        end
    end

    clink.matches_are_files()
    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)

-- vim: expandtab
