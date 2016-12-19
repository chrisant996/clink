-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local special_env_vars = {
    "cd", "date", "time", "random", "errorlevel",
    "cmdextversion", "cmdcmdline", "highestnumanodenumber"
}

--------------------------------------------------------------------------------
local envvar_generator = clink.generator(10)

function envvar_generator:generate(line_state, match_builder)
    local word = line_state:getendword()
    if word:sub(-1) ~= "%" then
        return false
    end

    local add_matches = function(matches)
        for _, i in ipairs(matches) do
            match_builder:addmatch({ match = i, suffix = "%" })
        end
    end

    add_matches(os.getenvnames())
    add_matches(special_env_vars)
    return true
end

--------------------------------------------------------------------------------
function envvar_generator:getprefixlength(word)
    local in_out = false
    local index
    for i = 1, #word do
        if word:sub(i, i) == "%" then
            in_out = not in_out
            index = i
        end
    end

    if in_out then
        return index
    end
end
