-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local nothing = clink:argmatcher()

--------------------------------------------------------------------------------
local inject = clink:argmatcher()
:addflags("--help", "--pid", "--profile", "--quiet", "--nolog")

--------------------------------------------------------------------------------
local autorun_dashdash = clink:argmatcher()
:addarg("--" .. inject)

local autorun = clink:argmatcher()
:addflags("--allusers", "--help")
:addarg(
    "install"   .. autorun_dashdash,
    "uninstall" .. nothing,
    "show"      .. nothing,
    "set"
)

--------------------------------------------------------------------------------
local function set_handler(word_index, line_state)
    local ret = {}
    local name = line_state:getword(word_index) or ""
    local cmd = io.popen(CLINK_EXE.." set --list "..name, "r")
    for line in cmd:lines() do
        table.insert(ret, line)
    end
    return ret
end

local set = clink:argmatcher()
:addflags("--help")
:addarg(set_handler)
:addarg(set_handler)

--------------------------------------------------------------------------------
local history = clink:argmatcher()
:addflags("--help")
:addarg(
    "add",
    "clear"     .. nothing,
    "delete"    .. nothing,
    "expand"
)

--------------------------------------------------------------------------------
clink:argmatcher(
    "clink",
    "clink_x86.exe",
    "clink_x64.exe")
:addarg(
    "autorun"   .. autorun,
    "echo"      .. nothing,
    "history"   .. history,
    "inject"    .. inject,
    "set"       .. set
)
