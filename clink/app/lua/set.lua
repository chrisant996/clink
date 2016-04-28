-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function set_match_generator(word)
    -- MODE4 : Needs support for marking matches as partial for '='
    return os.getenvnames()
end

--------------------------------------------------------------------------------
clink.arg.register_parser("set", set_match_generator)
