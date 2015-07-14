-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local svn_tree = {
    "add", "blame", "praise", "annotate", "ann", "cat", "changelist", "cl",
    "checkout", "co", "cleanup", "commit", "ci", "copy", "cp", "delete", "del",
    "remove", "rm", "diff", "di", "export", "help", "h", "import", "info",
    "list", "ls", "lock", "log", "merge", "mergeinfo", "mkdir", "move", "mv",
    "rename", "ren", "propdel", "pdel", "pd", "propedit", "pedit", "pe",
    "propget", "pget", "pg", "proplist", "plist", "pl", "propset", "pset", "ps",
    "resolve", "resolved", "revert", "status", "stat", "st", "switch", "sw",
    "unlock", "update", "up"
}

clink.arg.register_parser("svn", svn_tree)
