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
local function set_handler(match_word, word_index, line_state)
    local name = ""
    if word_index > 3 then
        name = line_state:getword(word_index - 1)
    end

    local ret = {}
    for line in io.popen('"'..CLINK_EXE..'" set --list '..name, "r"):lines() do
        table.insert(ret, line)
    end

    return ret
end

local set = clink.argmatcher()
:addflags("--help")
:addarg(set_handler)

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
