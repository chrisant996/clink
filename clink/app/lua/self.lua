-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local null_parser = clink.arg.new_parser()
null_parser:disable_file_matching()

local inject_parser = clink.arg.new_parser()
inject_parser:set_flags(
    "--help",
    "--pid",
    "--profile",
    "--quiet",
    "--scripts"
)

local autorun_dashdash_parser = clink.arg.new_parser()
autorun_dashdash_parser:set_arguments({ "--" .. inject_parser })

local autorun_parser = clink.arg.new_parser()
autorun_parser:set_flags("--allusers", "--help")
autorun_parser:set_arguments(
    {
        "install"   .. autorun_dashdash_parser,
        "uninstall" .. null_parser,
        "show"      .. null_parser,
        "set"
    }
)

local set_parser = clink.arg.new_parser()
set_parser:disable_file_matching()
set_parser:set_flags("--help")
set_parser:set_arguments(
    {
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
        "use_altgr_substitute",
    }
)

local self_parser = clink.arg.new_parser()
self_parser:set_arguments(
    {
        "inject" .. inject_parser,
        "autorun" .. autorun_parser,
        "set" .. set_parser,
    }
)

clink.arg.register_parser("clink", self_parser)
clink.arg.register_parser("clink_x86", self_parser)
clink.arg.register_parser("clink_x64", self_parser)
