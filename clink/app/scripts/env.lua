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
            match_builder:addmatch("%"..i.."%", "word")
        end
    end

    add_matches(os.getenvnames())
    add_matches(special_env_vars)

    local amount = string.len(line_state:getendword())
    if amount > 1 then
        amount = 1 - amount
        match_builder:setprefixincluded(amount)
    else
        match_builder:setprefixincluded()
    end
    match_builder:setsuppressappend()
    match_builder:setsuppressquoting()
    return true
end

--------------------------------------------------------------------------------
function envvar_generator:getprefixlength(line_state)
    local word = line_state:getendword()
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
