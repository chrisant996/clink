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
local colors = clink.argmatcher():addarg({fromhistory=true})
local onoff = clink.argmatcher():addarg("on", "off")

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
                local d = pending.desc:gsub("^%s+", ""):gsub("^([^.]*)%.%s.*$", "%1"):gsub("%(.*$", ""):gsub("[.%s]+$", "")
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
                if not descriptions[flag][1] then
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
        "/s", "/q", "/d", "/a", "/u",
        "/S", "/Q", "/D", "/A", "/U",
        "/t:"..colors, "/T:"..colors,
        "/e:on", "/e:off", "/e:"..onoff["e"], "/E:"..onoff["e"],
        "/f:on", "/f:off", "/f:"..onoff["f"], "/F:"..onoff["f"],
        "/v:on", "/v:off", "/v:"..onoff["v"], "/V:"..onoff["v"],
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
    })
end

clink.argmatcher("cmd"):setdelayinit(delayinit):nofiles()
