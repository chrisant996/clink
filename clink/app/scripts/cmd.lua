-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local cmd_generator = clink.generator(40)

--------------------------------------------------------------------------------
settings.add("color.cmdsep", "bold", "Color for & and | command separators")
settings.add("color.cmdredir", "bold", "Color for < and > redirection symbols")

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

--------------------------------------------------------------------------------
local amp_classifier = clink.classifier(1)
function amp_classifier:classify(commands)
    if commands and commands[1] then
        local line_state = commands[1].line_state
        local classifications = commands[1].classifications
        local line = line_state:getline()
        local quote = false
        local i = 1
        local color_cmdsep = settings.get("color.cmdsep")
        local color_cmdredir = settings.get("color.cmdredir")
        while (i <= #line) do
            local c = line:sub(i,i)
            if c == '^' then
                i = i + 1
            elseif c == '"' then
                quote = not quote
            elseif quote then
            elseif c == '&' or c == '|' then
                classifications:applycolor(i, 1, color_cmdsep)
            elseif c == '>' or c == '<' then
                classifications:applycolor(i, 1, color_cmdredir)
                if line:sub(i,i+1) == '>&' then
                    i = i + 1
                    classifications:applycolor(i, 1, color_cmdredir)
                end
            end
            i = i + 1
        end
    end
end
