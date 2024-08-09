-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function color_separators(line, start_offset, end_offset, classifications, color_cmdsep)
    local seen
    local num = 0
    for i = start_offset, end_offset do
        local c = line:sub(i,i)
        if c == '&' or c == '|' then
            if not seen then
                seen = c
            elseif seen ~= c then
                num = 9
            end
            num = num + 1
            if num <= 2 then
                classifications:applycolor(i, 1, color_cmdsep)
            else
                local color = settings.get("color.unrecognized")
                classifications:applycolor(i, 1, color)
            end
        elseif seen then
            num = 9
        end
    end
end

--------------------------------------------------------------------------------
local function color_before_first_word(line, start_offset, end_offset, classifications)
    for i = start_offset, end_offset do
        local c = line:sub(i,i)
        if c == '@' then
            local color = settings.get("color.cmd")
            classifications:applycolor(i, 1, color)
        end
    end
end

--------------------------------------------------------------------------------
local cmd_classifier = clink.classifier(1)
function cmd_classifier:classify(commands) -- luacheck: no self
    if commands and commands[1] then
        -- Redirection symbols and @ sign.
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
            elseif c == '>' or c == '<' then
                local err
                local x = i
                local color = color_cmdredir
                if line:sub(i,i+1) == '>&' then
                    i = i + 1
                    if line:find("^[^0-9]", i+1) then
                        color = settings.get("color.unrecognized")
                        err = true
                    end
                end
                if not err and line:sub(x-1,x-1):find("^[0-9]") then
                    if x == 2 or line:sub(x-2,x-2):find("^[ \t=;,()]") then
                        x = x - 1
                    elseif x > 2 and line:sub(x-2,x-2):find("^%@") then
                        -- A digit redirection cannot immediately follow @.
                        x = x - 1
                        color = settings.get("color.unrecognized")
                        err = true -- luacheck: no unused
                    end
                end
                classifications:applycolor(x, i+1-x, color)
            end
            i = i + 1
        end

        -- Command separators, redirection arguments, and rem command.
        local last_offset
        for _, command in ipairs(commands) do
            line_state = command.line_state
            if last_offset then
                color_separators(line, last_offset, line_state:getcommandoffset() - 1, classifications, color_cmdsep)
            end
            for word_index = 1, line_state:getwordcount(), 1 do
                local info = line_state:getwordinfo(word_index)
                if word_index == 1 and not info.quoted then
                    color_before_first_word(line, line_state:getrangeoffset(), info.offset - 1, classifications)
                end
                if info.redir then
                    classifications:applycolor(info.offset, info.length, color_cmdredir)
                elseif word_index == 1 and line_state:getword(word_index) == "rem" then
                    local color = settings.get("color.description")
                    if color == "" then
                        color = "0"
                    end
                    command.classifications:classifyword(1, "c")
                    command.classifications:applycolor(info.offset + info.length, #line, color)
                    break
                end
            end
            local info = line_state:getwordinfo(line_state:getwordcount())
            if info then
                last_offset = line_state:getrangeoffset() + line_state:getrangelength()
            end
        end
        color_separators(line, last_offset or 1, #line, classifications, color_cmdsep)
    end
end

local chain = clink.argmatcher():chaincommand("cmdquotes") -- CMD's command line has special handling for quotes.
local colors = clink.argmatcher():addarg({fromhistory=true})

local function first_sentence(s)
    return s:gsub("^%s+", ""):gsub("^([^.]*)%.%s.*$", "%1"):gsub("%(.*$", ""):gsub("[.%s]+$", "")
end

local function delayinit(argmatcher)
    argmatcher:setdelayinit(nil)

    local descriptions = {
        ["/c"] = {},
        ["/k"] = {},
        ["/s"] = {},
        ["/q"] = {},
        ["/d"] = {},
        ["/a"] = {},
        ["/u"] = {},
        ["/t:"] = {},
        ["/e:on"] = {},
        ["/e:off"] = {},
        ["/f:on"] = {},
        ["/f:off"] = {},
        ["/v:on"] = {},
        ["/v:off"] = {},
    }

    local function hide_cap_onoff()
        clink.onfiltermatches(function()
            return { "on", "off" }
        end)
        return {}
    end

    local onoff = {
        ["e"] = clink.argmatcher():addarg("on", "off", "ON", "OFF", hide_cap_onoff),
        ["f"] = clink.argmatcher():addarg("on", "off", "ON", "OFF", hide_cap_onoff),
        ["v"] = clink.argmatcher():addarg("on", "off", "ON", "OFF", hide_cap_onoff),
    }

    local f = io.popen("2>nul cmd.exe /?")
    if f then
        local pending = {}
        local function finish_pending()
            if pending.flag and pending.desc then
                local t = descriptions[pending.flag]
                if pending.arginfo then
                    table.insert(t, pending.arginfo)
                end
                local d = first_sentence(pending.desc)
                table.insert(t, d)
                local o = onoff[pending.flag:match("[a-z]")]
                if o then
                    local arg = pending.flag:match("^/.:(.+)$")
                    o:adddescriptions({[arg]={d}})
                end
            end
            pending.flag = nil
            pending.arginfo = nil
            pending.desc = nil
        end
        for line in f:lines() do
            line = unicode.fromcodepage(line)
            local flag, text = line:match("^(/[A-Za-z][^%s]*)%s+([^%s].*)$")
            if not flag and pending.flag then
                text = line:match("^%s+([^%s].*)$")
            end
            if flag then
                flag = flag:lower()
                local stripped, arginfo = flag:match("^(/t:)([^%s]*)")
                flag = stripped or flag
                if descriptions[flag] and not descriptions[flag][1] then
                    finish_pending()
                    pending.flag = flag
                    pending.arginfo = arginfo
                    pending.desc = text
                end
            elseif text and pending.flag then
                pending.desc = pending.desc.." "..text
            elseif pending.flag then
                finish_pending()
            end
        end
        f:close()
    end

    local desc_t = descriptions["/t:"]
    local num_t = #desc_t
    if num_t < 2 then
        if num_t < 1 then
            table.insert(desc_t, "fg")
        end
        table.insert(desc_t, "")
    end

    argmatcher
    :addflags({
        "/c"..chain, "/C"..chain,
        "/k"..chain, "/K"..chain,
        "/r"..chain, "/R"..chain,
        "/s", "/q", "/d", "/a", "/u",
        "/S", "/Q", "/D", "/A", "/U",
        "/t:"..colors, "/T:"..colors,
        "/e:on", "/e:off", "/e:"..onoff["e"], "/E:"..onoff["e"],
        "/f:on", "/f:off", "/f:"..onoff["f"], "/F:"..onoff["f"],
        "/v:on", "/v:off", "/v:"..onoff["v"], "/V:"..onoff["v"],
        "/x", "/y",
        "/X", "/Y",
        "/?",
    })
    :nofiles()
    :adddescriptions(descriptions)
    :hideflags({
        "/C", "/K",
        "/S", "/Q", "/D", "/A", "/U",
        "/T:",
        "/e:", "/E:",
        "/f:", "/F:",
        "/v:", "/V:",
        "/x", "/X",
        "/y", "/Y",
        "/r", "/R",
        "/?",
    })
end

clink.argmatcher("cmd"):setdelayinit(delayinit):nofiles()
