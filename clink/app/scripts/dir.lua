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
clink.argmatcher("cd", "chdir")
:setcmdcommand()
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
:addflags("/s", "/q")
:adddescriptions({
    ["/s"] = "Remove files and directories recursively",
    ["/q"] = "Quiet mode, do not ask if ok to remove a directory tree with /S",
})
:addarg(clink.dirmatches)
:loop()
