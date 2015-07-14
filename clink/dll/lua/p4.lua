-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local p4_tree = {
    "add", "annotate", "attribute", "branch", "branches", "browse", "change",
    "changes", "changelist", "changelists", "client", "clients", "copy",
    "counter", "counters", "cstat", "delete", "depot", "depots", "describe",
    "diff", "diff2", "dirs", "edit", "filelog", "files", "fix", "fixes",
    "flush", "fstat", "grep", "group", "groups", "have", "help", "info",
    "integrate", "integrated", "interchanges", "istat", "job", "jobs", "label",
    "labels", "labelsync", "legal", "list", "lock", "logger", "login",
    "logout", "merge", "move", "opened", "passwd", "populate", "print",
    "protect", "protects", "reconcile", "rename", "reopen", "resolve",
    "resolved", "revert", "review", "reviews", "set", "shelve", "status",
    "sizes", "stream", "streams", "submit", "sync", "tag", "tickets", "unlock",
    "unshelve", "update", "user", "users", "where", "workspace", "workspaces"
}

clink.arg.register_parser("p4", p4_tree)

--------------------------------------------------------------------------------
local p4vc_tree = {
    "help", "branchmappings", "branches", "diff", "groups", "branch", "change",
    "client", "workspace", "depot", "group", "job", "label", "user", "jobs",
    "labels", "pendingchanges", "resolve", "revisiongraph", "revgraph",
    "streamgraph", "streams", "submit", "submittedchanges", "timelapse",
    "timelapseview", "tlv", "users", "workspaces", "clients", "shutdown"
}

clink.arg.register_parser("p4vc", p4vc_tree)
