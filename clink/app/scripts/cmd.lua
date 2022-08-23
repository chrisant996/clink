-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local cmd_generator = clink.generator(40)

--------------------------------------------------------------------------------
settings.add("color.cmdsep", "bold", "Color for & and | command separators")
settings.add("color.cmdredir", "bold", "Color for < and > redirection symbols")

--------------------------------------------------------------------------------
-- NOTE: Keep in sync with is_cmd_command() in cmd_tokenisers.cpp.
local cmd_commands = {
    "assoc", "attrib", "break", "call", "cd", "chcp", "chdir", "cls",
    "color", "copy", "date", "del", "dir", "dpath", "echo", "endlocal",
    "erase", "exit", "for", "format", "ftype", "goto", "help", "if", "md",
    "mkdir", "mklink", "more", "move", "path", "pause", "popd", "prompt",
    "pushd", "rd", "rem", "ren", "rename", "rmdir", "set", "setlocal",
    "shift", "start", "subst", "tasklist", "taskkill", "time", "title",
    "type", "ver", "verify", "vol",
}

--------------------------------------------------------------------------------
function cmd_generator:generate(line_state, match_builder) -- luacheck: no self
    -- Cmd commands only apply for the first word of a line.
    if line_state:getwordcount() > 1 then
        return false
    end

    -- If executable matching is disabled do nothing
    if not settings.get("exec.enable") or not settings.get("exec.commands") then
        return false
    end

    -- Cmd commands cannot be quoted.
    local word_info = line_state:getwordinfo(1)
    if word_info.quoted then
        return false
    end

    -- They should be skipped if the line's whitespace prefixed.
    if settings.get("exec.space_prefix") then
        local offset = line_state:getcommandoffset()
        if line_state:getline():sub(offset, offset):find("[ \t]") then
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
local cmd_classifier = clink.classifier(1)
function cmd_classifier:classify(commands) -- luacheck: no self
    if commands and commands[1] then
        -- Command separators and redirection symbols.
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
            elseif quote then -- luacheck: ignore 542
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

        -- Special case coloring for rem command.
        for _,command in pairs(commands) do
            line_state = command.line_state
            for word_index = 1, line_state:getwordcount(), 1 do
                local info = line_state:getwordinfo(word_index)
                if not info.redir then
                    if line_state:getword(word_index) == "rem" then
                        local color = settings.get("color.description")
                        if color == "" then
                            color = "0"
                        end
                        command.classifications:classifyword(1, "c")
                        command.classifications:applycolor(info.offset + info.length, #line, color)
                    end
                    break
                end
            end
        end
    end
end

local chain = clink.argmatcher():chaincommand()

local onoff = clink.argmatcher():addarg("ON", "OFF")
clink.argmatcher("cmd")
:addflags("/c"..chain, "/k"..chain, "/s", "/q", "/d", "/a", "/u")
:addflags("/t:"..clink.argmatcher():addarg({fromhistory=true}), "/e:"..onoff, "/f:"..onoff, "/v:"..onoff)
:nofiles()
