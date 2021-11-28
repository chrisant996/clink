clink.argmatcher("findstr")
:addflags({
    "/b", "/e", "/l", "/r", "/s", "/i", "/x", "/v", "/n", "/m", "/o", "/p", "/offline",
    "/a:"..clink.argmatcher():addarg( "attr" ),
    "/f:"..clink.argmatcher():addarg( clink.filematches ),
    "/c:"..clink.argmatcher():addarg( "search_string" ),
    "/g:", -- This is the same as linking with clink.argmatcher():addarg(clink.filematches).
    "/d:"..clink.argmatcher():addarg( clink.dirmatches )
})
