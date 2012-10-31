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
local git_argument_tree = {
    -- Porcelain and ancillary commands from git's man page.
    "add", "am", "archive", "bisect", "branch", "bundle", "checkout",
    "cherry-pick", "citool", "clean", "clone", "commit", "describe", "diff",
    "fetch", "format-patch", "gc", "grep", "gui", "init", "log", "merge", "mv",
    "notes", "pull", "push", "rebase", "reset", "revert", "rm", "shortlog",
    "show", "stash", "status", "submodule", "tag", "config", "fast-export",
    "fast-import", "filter-branch", "lost-found", "mergetool", "pack-refs",
    "prune", "reflog", "relink", "remote", "repack", "replace", "repo-config",
    "annotate", "blame", "cherry", "count-objects", "difftool", "fsck",
    "get-tar-commit-id", "help", "instaweb", "merge-tree", "rerere",
    "rev-parse", "show-branch", "verify-tag", "whatchanged"
}

clink.arg.register_tree("git", git_argument_tree)

-- vim: expandtab
