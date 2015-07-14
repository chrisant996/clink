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
    for _, name in ipairs(clink.get_env_var_names()) do
        if clink.is_match(word, name) then
            table.insert(matches, name:lower())
        end
    end

    clink.suppress_char_append()
    return matches
end

--------------------------------------------------------------------------------
clink.arg.register_parser("set", set_match_generator)
