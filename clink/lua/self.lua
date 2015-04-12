--
-- Copyright (c) 2012 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

--------------------------------------------------------------------------------
local null_parser = clink.arg.new_parser()
null_parser:disable_file_matching()

local inject_parser = clink.arg.new_parser()
inject_parser:set_flags(
    "--help",
    "--nohostcheck",
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
        "esc_clears_line",
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
