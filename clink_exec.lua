--[[
   Copyright (c) 2012 Martin Ridgers

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
]]--

--------------------------------------------------------------------------------
dos_commands = {
    "assoc", "break", "call", "cd", "chcp", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "diskcomp", "diskcopy", "echo", "endlocal", "erase",
    "exit", "for", "format", "ftype", "goto", "graftabl", "if", "md",
    "mkdir", "mklink", "mode", "more", "move", "path", "pause", "popd",
    "prompt", "pushd", "rd", "rem", "ren", "rename", "rmdir", "set",
    "setlocal", "shift", "start", "time", "title", "tree", "type", "ver",
    "verify", "vol"
}

--------------------------------------------------------------------------------
function is_match(needle, candidate)
    if candidate:sub(1, #needle):lower() == needle:lower() then
        return true
    end
    return false
end

--------------------------------------------------------------------------------
function split_on_semicolon(str)
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
function dos_cmd_match_generator(text, first, last)
    for _, cmd in ipairs(dos_commands) do
        if is_match(text, cmd) then
            clink.add_match(cmd)
        end
    end
end

--------------------------------------------------------------------------------
function exec_match_generator(text, first, last)
    -- We're only interested in exec completion if this is the first word of the
    -- line, or the first word after a command separator.
    local leading = rl_line_buffer:sub(1, first - 1)
    local is_first = leading:find("^%s*$")
    local is_separated = leading:find("[|&]%s*$")
    if not is_first and not is_separated then
        return false
    end

    -- Skip exec matching if text is an absolute path
    if text:find("^[a-zA-Z]:[\\/]") or text:find("^[\\/]") then
        return false
    end

    -- If there's no path separator in text then consider the environment's path
    -- otherwise just search the specified relative path.
    local paths
    if not text:find("[\\/]") then
        paths = split_on_semicolon(clink.getenv("PATH"))

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
        
        paths = paths_merged;

        dos_cmd_match_generator(text, first, last)
    end

    if type(paths) ~= "table" then
        paths = {}
    end
    table.insert(paths, ".")

    -- Strip off possible trailing extension.
    local needle = text;
    local ext_a, ext_b = needle:find("%.[a-zA-Z]*$")
    if ext_a then
        needle = needle:sub(1, ext_a - 1)
    end

    -- Combine extensions, text, and paths to find matches
    local count = #clink.matches
    local exts = split_on_semicolon(clink.getenv("PATHEXT"))
    for _, ext in ipairs(exts) do
        for _, path in ipairs(paths) do
            local mask = path.."\\"..needle.."*"..ext
            for _, file in ipairs(clink.findfiles(mask)) do
                -- treat the file slightly differently if it's relative.
                if mask:sub(1, 2) == ".\\" then
                    for i = #mask, 1, -1 do
                        if mask:sub(i, i) == "\\" then
                            file = mask:sub(3, i)..file
                            break
                        end
                    end
                end

                if is_match(text, file) then
                    count = count + 1
                    clink.add_match(file)
                end
            end
        end
    end

    -- No point monopolising completion if there was no matches...
    if count == 0 then
        return false
    end

    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)
