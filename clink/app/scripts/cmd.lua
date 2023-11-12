-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
settings.add("color.cmdsep", "bold", "Color for & and | command separators")
settings.add("color.cmdredir", "bold", "Color for < and > redirection symbols")

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
