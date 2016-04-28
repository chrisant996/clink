-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local special_env_vars = {
    "cd", "date", "time", "random", "errorlevel",
    "cmdextversion", "cmdcmdline", "highestnumanodenumber"
}

--------------------------------------------------------------------------------
local function env_vars_display_filter(matches)
    local to_display = {}
    for _, m in ipairs(matches) do
        local _, _, out = m:find("(%%[^%%]+%%)$")
        table.insert(to_display, out)
    end

    return to_display
end

--------------------------------------------------------------------------------
local function env_vars_find_matches(candidates, prefix, part, result)
    for _, name in ipairs(candidates) do
        result:addmatch(prefix..'%'..name:lower()..'%')
    end
end

--------------------------------------------------------------------------------
local function env_vars_match_generator(text, first, last, result)
    local all = line_state.line:sub(1, last)

    -- Skip pairs of %s
    local i = 1
    for _, r in function () return all:find("%b%%", i) end do
        i = r + 2
    end

    -- Find a solitary %
    local i = all:find("%%", i)
    if not i then
        return false
    end

    if i < first then
        return false
    end

    local part = clink.lower(all:sub(i + 1))

    i = i - first
    local prefix = text:sub(1, i)

    env_vars_find_matches(os.getenvnames(), prefix, part, result)
    env_vars_find_matches(special_env_vars, prefix, part, result)

    if result:getmatchcount() >= 1 then
        clink.match_display_filter = env_vars_display_filter
        return true
    end

    return false
end

--------------------------------------------------------------------------------
clink.register_match_generator(env_vars_match_generator, 10)
