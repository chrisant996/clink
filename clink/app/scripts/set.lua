-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, set).

--------------------------------------------------------------------------------
local set_generator = clink.generator(41)

--------------------------------------------------------------------------------
function set_generator:generate(line_state, match_builder) -- luacheck: no unused
    if line_state:getwordcount() ~= 2 then
        return false
    end

    if line_state:getword(1) ~= "set" then
        return false
    end

    match_builder:addmatches(os.getenvnames(), "word")
    match_builder:setappendcharacter("=")
    return true
end
