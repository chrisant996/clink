-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

-- MODE4 : some of this is out of date.

--------------------------------------------------------------------------------
local nothing = clink:argmatcher()

local inject = clink:argmatcher()
:addflags(
    "--help",
    "--pid",
    "--profile",
    "--quiet",
    "--scripts"
)

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

local set = clink:argmatcher()
:generatefiles(false)
:addflags("--help")
:addarg(
    "ansi_code_support",
    "ctrld_exits",
    "exec_match_style",
    "history_dupe_mode",
    "history_expand_mode",
    "history_file_lines",
    "history_ignore_space",
    "history_io",
    "match_colour",
    "prompt_colour",
    "space_prefix_match_files",
    "strip_crlf_on_paste",
    "terminate_autoanswer",
    "use_altgr_substitute"
)

local history = clink:argmatcher()
:addflags("--help")
:addarg(
    "add",
    "clear"     .. nothing,
    "delete"    .. nothing,
    "expand"
)

clink
:argmatcher("clink", "clink_x86", "clink_x64")
:addarg(
    "inject"    .. inject,
    "autorun"   .. autorun,
    "set"       .. set,
    "history"   .. history
)
