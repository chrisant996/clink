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
local function split_on_semicolon(str)
    local i = 0
    local ret = {}
    for _, j in function() return str:find(";", i, true) end do
        table.insert(ret, str:sub(i, j - 1))
        i = j + 1
    end
    table.insert(ret, str:sub(i, j))

    return ret
end

--------------------------------------------------------------------------------
local function dos_cmd_match_generator(text, first, last)
    for _, cmd in ipairs(dos_commands) do
        if clink.is_match(text, cmd) then
            clink.add_match(cmd)
        end
    end
end

--------------------------------------------------------------------------------
local function build_passes(text)
    local passes = {}

    -- If there's no path separator in text then consider the environment's path
    -- as a first pass for matches.
    if not text:find("[\\/:]") then
        local paths = split_on_semicolon(clink.get_env("PATH"))

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

        table.insert(passes, { paths=paths, func=dos_cmd_match_generator })
    end

    -- The fallback solution is to use 'text' to find matches, and also add
    -- directories.
    table.insert(passes, { paths={""}, func=dir_match_generator })

    return passes
end

--------------------------------------------------------------------------------
local function exec_match_generator(text, first, last)
    -- We're only interested in exec completion if this is the first word of the
    -- line, or the first word after a command separator.
    local leading = rl_line_buffer:sub(1, first - 1)
    local is_first = leading:find("^%s*\"*$")
    local is_separated = leading:find("[|&]%s*\"*$")
    if not is_first and not is_separated then
        return false
    end

    -- Strip off possible trailing extension.
    local needle = text
    local ext_a, ext_b = needle:find("%.[a-zA-Z]*$")
    if ext_a then
        needle = needle:sub(1, ext_a - 1)
    end

    -- Replace '_' or '-' with '*' for improved "case insentitive" searching.
    if clink.is_rl_variable_true("completion-map-case") then
        needle = needle:gsub("-", "?")
        needle = needle:gsub("_", "?")
    end

    -- Strip off any path components that may be on text
    local prefix = ""
    local i = text:find("[\\/:][^\\/:]*$")
    if i then
        prefix = text:sub(1, i)
    end

    local passes = build_passes(text)

    -- Combine extensions, text, and paths to find matches - this is done in two
    -- passes, the second pass possibly being "local" if the system-wide search
    -- didn't find any results.
    local n = #passes
    local exts = split_on_semicolon(clink.get_env("PATHEXT"))
    for p = 1, n do
        local pass = passes[p]
        for _, ext in ipairs(exts) do
            for _, path in ipairs(pass.paths) do
                local mask = path..needle.."*"..ext
                for _, file in ipairs(clink.find_files(mask)) do
                    file = prefix..file
                    if clink.is_match(text, file) then
                        clink.add_match(file)
                    end
                end
            end
        end
        
        if pass.func then
            pass.func(text, first, last)
        end

        -- Was there matches? Then there's no need to make any further passes.
        if clink.match_count() > 0 then
            break
        end
    end

    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)

-- vim: expandtab
