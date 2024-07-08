-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, app, commands).

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
    local _, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._menu_complete_numbers" and
            last_lua_func ~= "clink._menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("menu-complete")
end

--------------------------------------------------------------------------------
function clink._menu_complete_numbers_backward()
    local _, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._menu_complete_numbers" and
            last_lua_func ~= "clink._menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("menu-complete-backward")
end

--------------------------------------------------------------------------------
function clink._old_menu_complete_numbers()
    local _, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._old_menu_complete_numbers" and
            last_lua_func ~= "clink._old_menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("old-menu-complete")
end

--------------------------------------------------------------------------------
function clink._old_menu_complete_numbers_backward()
    local _, last_lua_func = rl.getlastcommand()
    if last_lua_func ~= "clink._old_menu_complete_numbers" and
            last_lua_func ~= "clink._old_menu_complete_numbers_backward" then
        collect_number_matches()
    end
    rl.invokecommand("old-menu-complete-backward")
end

--------------------------------------------------------------------------------
function clink._popup_complete_numbers()
    collect_number_matches()
    rl.invokecommand("clink-select-complete")
end

--------------------------------------------------------------------------------
function clink._popup_show_help(rl_buffer)
    local bindings = rl.getkeybindings(false, rl_buffer:getargument())
    if #bindings <= 0 then
        rl_buffer:refreshline()
        return
    end

    local items = {}
    for _,kb in ipairs(bindings) do
        table.insert(items, { value=kb.binding, display=kb.key, description=kb.binding.."\t"..kb.desc })
    end

    local binding = clink.popuplist("Key Bindings", items)
    rl_buffer:refreshline()
    if binding then
        rl_buffer:setargument()
        rl.invokecommand(binding)
    end
end

--------------------------------------------------------------------------------
function clink._diagnostics(rl_buffer)
    local arg = rl_buffer:getargument()
    clink._diag_coroutines()
    clink._diag_refilter()
    clink._diag_events(arg)                 -- When arg >= 1 or lua.debug is set.
    if arg then
        clink._diag_argmatchers(arg)        -- When arg >= 2.
        clink._diag_prompts(arg)            -- When arg >= 1 or lua.debug is set.
        clink._diag_generators(arg)         -- When arg >= 3.
        clink._diag_classifiers(arg)        -- When arg >= 2 or lua.debug is set.
        clink._diag_suggesters(arg)         -- When arg >= 2 or lua.debug is set.
        clink._diag_completions_dirs(arg)   -- When arg >= 1 or lua.debug is set.
    end
    if clink._diag_custom then
        clink._diag_custom(arg)
    end
end
