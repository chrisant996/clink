-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function set_match_generator(word)
    local ret = {}
    for _, i in ipairs(os.getenvnames()) do
        table.insert(ret, { match = i, suffix = "=" })
    end

    return ret
end

--------------------------------------------------------------------------------
clink.arg.register_parser("set", set_match_generator)
