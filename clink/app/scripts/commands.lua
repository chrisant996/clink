-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function collect_number_matches()
    local matches = console.screengrab("[^%w]*(%w%w[%w]+)", "^%x+$")
    matches["nosort"] = true
    rl.setmatches(matches, "word")
end

--------------------------------------------------------------------------------
function clink._complete_numbers()
    collect_number_matches()
    rl.invokecommand("complete")
end

--------------------------------------------------------------------------------
function clink._menu_complete_numbers()
    local last_rl_func, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._menu_complete_numbers" and
            last_lua_func ~= "clink._menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("menu-complete")
end

--------------------------------------------------------------------------------
function clink._menu_complete_numbers_backward()
    local last_rl_func, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._menu_complete_numbers" and
            last_lua_func ~= "clink._menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("menu-complete-backward")
end

--------------------------------------------------------------------------------
function clink._old_menu_complete_numbers()
    local last_rl_func, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._old_menu_complete_numbers" and
            last_lua_func ~= "clink._old_menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("old-menu-complete")
end

--------------------------------------------------------------------------------
function clink._old_menu_complete_numbers_backward()
    local last_rl_func, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._old_menu_complete_numbers" and
            last_lua_func ~= "clink._old_menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("old-menu-complete-backward")
end

--------------------------------------------------------------------------------
function clink._popup_complete_numbers()
    collect_number_matches()
    rl.invokecommand("clink-popup-complete")
end
