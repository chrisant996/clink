-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function set_match_generator(word)
    -- Skip this generator if first is in the rvalue.
    local leading = line_state.line:sub(1, line_state.first - 1)
    if leading:find("=") then
        return false
    end

    -- Enumerate environment variables and check for potential matches.
    local matches = {}
    for _, name in ipairs(os.getenvnames()) do
        table.insert(matches, name:lower())
    end

    return matches
end

--------------------------------------------------------------------------------
clink.arg.register_parser("set", set_match_generator)
