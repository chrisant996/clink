-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, env).

--------------------------------------------------------------------------------
local special_env_vars = {
    "cd", "date", "time", "random", "errorlevel",
    "cmdextversion", "cmdcmdline", "highestnumanodenumber"
}

--------------------------------------------------------------------------------
local envvar_generator = clink.generator(10)

--------------------------------------------------------------------------------
local function parse_percents(word)
    local in_out = false
    local index = nil

    -- Paired percent signs denote already-completed environment variables.
    -- So use envvar completion for abc%foo%def%USER but not for abc%foo%USER.
    for i = 1, #word do
        if word:sub(i, i) == "%" then
            in_out = not in_out
            if in_out then
                index = i - 1
            else
                index = i
            end
        end
    end

    return in_out, index
end

--------------------------------------------------------------------------------
function envvar_generator:generate(line_state, match_builder) -- luacheck: no self
    -- Does the word end with a percent sign?
    local word = line_state:getendword()
    if word:sub(-1) ~= "%" then
        return false
    end

    -- If expanding envvars, test whether there's an unterminated envvar.
    if settings.get("match.expand_envvars") then
        local in_out = parse_percents(word)
        if not in_out then
            return false
        end
    end

    local add_matches = function(matches)
        for _, i in ipairs(matches) do
            match_builder:addmatch("%"..i.."%", "word")
        end
    end

    -- Add env vars as matches.
    add_matches(os.getenvnames())
    add_matches(special_env_vars)

    match_builder:setsuppressappend()   -- Don't append a space character.
    match_builder:setsuppressquoting()  -- Don't quote envvars.
    return true
end

--------------------------------------------------------------------------------
function envvar_generator:getwordbreakinfo(line_state) -- luacheck: no self
    local word = line_state:getendword()
    local in_out, index = parse_percents(word)

    -- If there were any percent signs, return word break info to influence the
    -- match generators.
    if index then
        -- If expanding envvars, return the entire word so it can be expanded.
        -- This has a side effect that word breaks may confuse some match
        -- generators if they make unsafe assumptions.
        if not in_out and settings.get("match.expand_envvars") then
            return 0, #word
        end
        return index, (in_out and 1) or 0
    end
end
