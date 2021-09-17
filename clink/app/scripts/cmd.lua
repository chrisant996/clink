-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local cmd_generator = clink.generator(40)

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, cmd).

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
function clink.is_cmd_command(word)
    local lower_word = clink.lower(word)
    for _,i in ipairs(cmd_commands) do
        if lower_word == i then
            return true
        end
    end
    return false
end

--------------------------------------------------------------------------------
function cmd_generator:generate(line_state, match_builder)
    -- Cmd commands only apply for the first word of a line.
    if line_state:getwordcount() > 1 then
        return false
    end

    -- If executable matching is disabled do nothing
    if not settings.get("exec.enable") or not settings.get("exec.commands") then
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

    match_builder:addmatches(cmd_commands, "cmd")
    return false
end
