-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local cmd_module = clink:module(40)

--------------------------------------------------------------------------------
local cmd_commands = {
    "assoc", "break", "call", "cd", "chcp", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "diskcomp", "diskcopy", "echo", "endlocal", "erase",
    "exit", "for", "format", "ftype", "goto", "graftabl", "if", "md", "mkdir",
    "mklink", "more", "move", "path", "pause", "popd", "prompt", "pushd", "rd",
    "rem", "ren", "rename", "rmdir", "set", "setlocal", "shift", "start",
    "time", "title", "tree", "type", "ver", "verify", "vol"
}

--------------------------------------------------------------------------------
function cmd_module:generate(line_state, match_builder)
    -- Cmd commands only apply for the first word of a line.
    if line_state:getwordcount() > 1 then
        return false
    end

    -- They should be skipped if the the line's whitespace prefixed.
    if settings.get("exec.space_prefix") then
        local word_info = line_state:getwordinfo(1)
        local offset = line_state:getcommandoffset()
        if word_info.quoted then offset = offset + 1 end
        if word_info.offset > offset then
            return false
        end
    end

    -- If the word being completed is a relative path then commands don't apply.
    local text = line_state:getword(1)
    local text_dir = path.getdirectory(text) or ""
    if #text_dir ~= 0 then
        return false
    end

    match_builder:addmatches(cmd_commands)
    return false
end
