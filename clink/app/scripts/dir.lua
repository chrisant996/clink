-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, dir).

--------------------------------------------------------------------------------
clink.argmatcher("cd", "chdir")
:addflags("/d")
:addarg(clink.dirmatches)
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("pushd", "md", "mkdir")
:addarg(clink.dirmatches)
:nofiles()

--------------------------------------------------------------------------------
clink.argmatcher("rd", "rmdir")
:addflags("/s", "/q")
:addarg(clink.dirmatches)
:nofiles()
