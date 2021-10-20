-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, dir).

--------------------------------------------------------------------------------
clink.argmatcher("cd", "chdir")
:addflags("/d")
:adddescriptions({
    ["/d"] = "Also change drive"
})
:addarg(clink.dirmatches)
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("pushd", "md", "mkdir")
:addarg(clink.dirmatches)
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("rd", "rmdir")
:addflags("/s", "/q")
:adddescriptions({
    ["/s"] = "Remove files and directories recursively",
    ["/q"] = "Quiet mode, do not ask if ok to remove a directory tree with /S",
})
:addarg(clink.dirmatches)
:nofiles()
