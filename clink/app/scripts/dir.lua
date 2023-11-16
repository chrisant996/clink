-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, dir).

--------------------------------------------------------------------------------
local function get_dir_arg(word, word_index, line_state) -- luacheck: no unused
    local info = line_state:getwordinfo(word_index)
    if info and info.offset + info.length < line_state:getcursor() then
        local ws = info.offset
        local endinfo = line_state:getwordinfo(line_state:getwordcount())
        if endinfo then
            local we = endinfo.offset + endinfo.length - 1
            word = line_state:getline():sub(ws, we)
            return word
        end
    end
end

--------------------------------------------------------------------------------
local function onarg_cd(arg_index, word, word_index, line_state) -- luacheck: no unused
    -- Match generation after this may be relative to the new directory.
    local dir = get_dir_arg(word, word_index, line_state)
    if dir then
        if line_state:getword(word_index - 1):lower() ~= "/d" then
            local drive = path.getdrive(dir)
            if drive then
                local cwd = os.getcwd()
                if path.getdrive(cwd) ~= drive then
                    return
                end
            end
        end
        os.chdir(dir)
    end
end

--------------------------------------------------------------------------------
local function onarg_pushd(arg_index, word, word_index, line_state) -- luacheck: no unused
    -- Match generation after this is relative to the new directory.
    local dir = get_dir_arg(word, word_index, line_state)
    if dir then
        os.chdir(word)
    end
end

--------------------------------------------------------------------------------
local function cd_delayinit(argmatcher)
    argmatcher:setdelayinit(nil)

    local descriptions = {
        ["/d"] = { "Also change drive" },
    }

    local f = io.popen("2>nul cd /?")
    if f then
        local seen = {}
        local pending = {}
        local function finish_pending()
            if pending.flag and pending.desc then
                local d = pending.desc:gsub("^%s+", ""):gsub("^([^.]*)%.%s.*$", "%1"):gsub("%(.*$", ""):gsub("[.%s]+$", "")
                descriptions[pending.flag] = { d }
            end
            pending.flag = nil
            pending.desc = nil
        end
        for line in f:lines() do
            line = unicode.fromcodepage(line)
            local flag, text
            if line:find("/[Dd] ") and not line:find("[%[%]]") then
                flag = "/D"
            end
            text = line:match("^%s*([^%s].*)$")
            if flag then
                flag = flag:lower()
                if not seen[flag] then
                    finish_pending()
                    pending.flag = flag
                    pending.desc = text
                    seen[flag] = true
                end
            elseif text and pending.flag then
                pending.desc = pending.desc.." "..text
            elseif pending.flag then
                finish_pending()
            end
        end
        f:close()
    end

    argmatcher:adddescriptions(descriptions)
end

--------------------------------------------------------------------------------
local function rd_delayinit(argmatcher)
    argmatcher:setdelayinit(nil)

    local descriptions = {
        ["/s"] = { "Remove files and directories recursively" },
        ["/q"] = { "Quiet mode, do not ask if ok to remove a directory tree with /S" },
    }

    local f = io.popen("2>nul rd /?")
    if f then
        local seen = {}
        local pending = {}
        local function finish_pending()
            if pending.flag and pending.desc then
                local d = pending.desc:gsub("^%s+", ""):gsub("^([^.]*)%.%s.*$", "%1"):gsub("%(.*$", ""):gsub("[.%s]+$", "")
                descriptions[pending.flag] = { d }
            end
            pending.flag = nil
            pending.desc = nil
        end
        for line in f:lines() do
            line = unicode.fromcodepage(line)
            local flag, text = line:match("^%s*(/[A-Za-z])%s+([^%s].*)$")
            if not flag and pending.flag then
                text = line:match("^%s+([^%s].*)$")
            end
            if flag then
                flag = flag:lower()
                if not seen[flag] then
                    finish_pending()
                    pending.flag = flag
                    pending.desc = text
                    seen[flag] = true
                end
            elseif text and pending.flag then
                pending.desc = pending.desc.." "..text
            elseif pending.flag then
                finish_pending()
            end
        end
        f:close()
    end

    argmatcher:adddescriptions(descriptions)
end

--------------------------------------------------------------------------------
clink.argmatcher("cd", "chdir")
:setcmdcommand()
:setdelayinit(cd_delayinit)
:addflags("/d")
:adddescriptions({
    ["/d"] = "Also change drive"
})
:addarg({onarg=onarg_cd, clink.dirmatches})
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("pushd")
:setcmdcommand()
:addarg({onarg=onarg_pushd, clink.dirmatches})
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("md", "mkdir")
:setcmdcommand()
:addarg(clink.dirmatches)
:loop()

--------------------------------------------------------------------------------
clink.argmatcher("rd", "rmdir")
:setcmdcommand()
:setdelayinit(rd_delayinit)
:addflags("/s", "/q")
:addarg(clink.dirmatches)
:loop()
