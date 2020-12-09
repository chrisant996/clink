-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local nothing = clink.argmatcher()

--------------------------------------------------------------------------------
local dir_matcher = clink.argmatcher():addarg(clink.dir_matches)

--------------------------------------------------------------------------------
local inject = clink.argmatcher()
:addflags(
    "--help",
    "--pid",
    "--profile"..dir_matcher,
    "--quiet",
    "--nolog",
    "--scripts"..dir_matcher)

--------------------------------------------------------------------------------
local autorun_dashdash = clink.argmatcher()
:addarg("--" .. inject)

local autorun = clink.argmatcher()
:addflags(
    "--allusers",
    "--help")
:addarg(
    "install"   .. autorun_dashdash,
    "uninstall" .. nothing,
    "show"      .. nothing,
    "set")

--------------------------------------------------------------------------------
local echo = clink.argmatcher()
:addflags("--help")
:nofiles()

--------------------------------------------------------------------------------
local function color_handler(line_state)
    local i = 4
    local include_bright = true
    local include_underline = true
    local include_color = true
    local include_on = true
    local include_sgr = true

    while i < line_state:getwordcount() do
        local word = line_state:getword(i)

        if word ~= "" then
            include_sgr = false
        end

        if word == "on" then
            if not include_on then
                return {}
            end
            include_bright = true
            include_underline = false
            include_color = true
            include_on = false
        elseif word == "bold" or word == "dim" or word == "bright" then
            if not include_bright then
                return {}
            end
            include_bright = false
        elseif word == "underline" or word == "nounderline" then
            if not include_underline then
                return {}
            end
            include_underline = false
        elseif word == "black" or word == "red" or word == "green" or word == "yellow" or word == "blue" or word == "cyan" or word == "magenta" or word == "white" then
            if not include_color then
                return {}
            end
            include_bright = false
            include_underline = false
            include_color = false
        else
            return {}
        end

        i = i + 1
    end

    local list = {}
    if include_on then
        table.insert(list, "on")
        if include_bright then
            table.insert(list, "bold")
        end
    end
    if include_bright then
        table.insert(list, "bright")
        table.insert(list, "dim")
    end
    if include_underline then
        table.insert(list, "underline")
        table.insert(list, "nounderline")
    end
    if include_color then
        table.insert(list, "default")
        table.insert(list, "normal")
        table.insert(list, "black")
        table.insert(list, "red")
        table.insert(list, "green")
        table.insert(list, "yellow")
        table.insert(list, "blue")
        table.insert(list, "cyan")
        table.insert(list, "magenta")
        table.insert(list, "white")
    end
    if include_sgr then
        table.insert(list, "sgr")
    end
    return list
end

--------------------------------------------------------------------------------
local function set_handler(match_word, word_index, line_state)
    local name = ""
    local color = false
    if word_index > 3 then
        name = line_state:getword(3)
        if name:sub(1, 6) == "color." then
            color = true
        elseif word_index > 4 then
            return {}
        end
    end

    local ret = {}
    for line in io.popen('"'..CLINK_EXE..'" set --list '..name, "r"):lines() do
        table.insert(ret, line)
    end

    -- If it's a recognized color setting, then go through a custom handler to
    -- account for the "attr color on color" syntax state machine.
    if color and #ret > 0 then
        return color_handler(line_state)
    end

    return ret
end

local set = clink.argmatcher()
:addflags("--help")
:addarg(set_handler):loop()

--------------------------------------------------------------------------------
local history = clink.argmatcher()
:addflags("--help")
:addarg(
    "add",
    "clear"     .. nothing,
    "compact"   .. nothing,
    "delete"    .. nothing,
    "expand")

--------------------------------------------------------------------------------
clink.argmatcher(
    "clink",
    "clink_x86.exe",
    "clink_x64.exe")
:addarg(
    "autorun"   .. autorun,
    "echo"      .. echo,
    "history"   .. history,
    "info"      .. nothing,
    "inject"    .. inject,
    "set"       .. set)
:addflags(
    "--help",
    "--profile"..dir_matcher,
    "--version")
