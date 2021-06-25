-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local prompt_filters = {}
local prompt_filters_unsorted = false



--------------------------------------------------------------------------------
local prompt_filter_current = nil       -- Current running prompt filter.
local prompt_filter_coroutines = {}     -- Up to one coroutine per prompt filter, with cached return value.

--------------------------------------------------------------------------------
local function set_current_prompt_filter(filter)
    prompt_filter_current = filter
    clink._set_coroutine_context(filter)
end



--------------------------------------------------------------------------------
function clink._filter_prompt(prompt)
    -- Sort by priority if required.
    if prompt_filters_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(prompt_filters, lambda)

        prompt_filters_unsorted = false
    end

    -- Protected call to prompt filters.
    local impl = function(prompt)
        for _, filter in ipairs(prompt_filters) do
            set_current_prompt_filter(filter)
            local filtered, onwards = filter:filter(prompt)
            if filtered ~= nil then
                if onwards == false then return filtered end
                prompt = filtered
            end
        end

        return prompt
    end

    set_current_prompt_filter(nil)
    local ok, ret = xpcall(impl, _error_handler_ret, prompt)
    set_current_prompt_filter(nil)
    if not ok then
        print("")
        print("prompt filter failed:")
        print(ret)
        return false
    end

    return ret
end

--------------------------------------------------------------------------------
function clink._diag_refilter()
    local refilter,redisplay = clink.get_refilter_redisplay_count()
    if refilter > 0 or redisplay > 0 then
        clink.print("\x1b[1mprompt refilter:\x1b[m")
        print("  refilter", refilter)
        print("  redisplay", redisplay)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.promptfilter
--- -arg:   [priority:integer]
--- -ret:   table
--- -show:  local foo_prompt = clink.promptfilter(80)
--- -show:  function foo_prompt:filter(prompt)
--- -show:  &nbsp; -- Insert the date at the beginning of the prompt.
--- -show:  &nbsp; return os.date("%a %H:%M").." "..prompt
--- -show:  end
--- Creates and returns a new promptfilter object that is applied in increasing
--- <span class="arg">priority</span> order (low values to high values).  Define
--- on the object a <code>:filter()</code> function that takes a string argument
--- which contains the filtered prompt so far.  The function can return nil to
--- have no effect, or can return a new prompt string.  It can optionally stop
--- further prompt filtering by also returning false.  See
--- <a href="#customisingtheprompt">Customising The Prompt</a> for more
--- information.
function clink.promptfilter(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(prompt_filters, ret)

    prompt_filters_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
--- -name:  clink.prompt.register_filter
--- -arg:   filter_func:function
--- -arg:   [priority:integer]
--- -ret:   table
--- -show:  -- Deprecated form:
--- -show:  function foo_prompt()
--- -show:  &nbsp; clink.prompt.value = "FOO "..clink.prompt.value.." >>"
--- -show:  &nbsp; --return true  -- Returning true stops further filtering.
--- -show:  end
--- -show:  clink.prompt.register_filter(foo_prompt, 10)
--- -show:
--- -show:  -- Replace with new form:
--- -show:  local foo_prompt = clink.promptfilter(10)
--- -show:  function foo_prompt:filter(prompt)
--- -show:  &nbsp; return "FOO "..prompt.." >>" --,false  -- Adding ,false stops further filtering.
--- -show:  end
--- -deprecated: clink.promptfilter
--- Registers a prompt filter function.  The capabilities are the same as before
--- but the syntax is changed.
clink.prompt = clink.prompt or {}
function clink.prompt.register_filter(filter, priority)
    local o = clink.promptfilter(priority)
    function o:filter(the_prompt)
        clink.prompt.value = the_prompt
        local stop = filter(the_prompt)
        return clink.prompt.value, not stop
    end
end



--------------------------------------------------------------------------------
local function clear_prompt_coroutines()
    prompt_filter_coroutines = {}
end
clink.onbeginedit(clear_prompt_coroutines)

--------------------------------------------------------------------------------
-- Refilter at most once per resume; so if N prompt coroutines finish in the
-- same pass the prompt doesn't refilter separately N times.
local function refilterprompt_after_coroutines()
    local refilter = false
    for _,entry in pairs(prompt_filter_coroutines) do
        if entry.refilter then
            refilter = true
            entry.refilter = false
        end
    end
    if refilter then
        clink.refilterprompt()
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.promptcoroutine
--- -arg:   func:function
--- -ret:   [return value from func]
--- Creates a coroutine to run the <span class="arg">func</span> function in the
--- background.  Clink will automatically resume the coroutine repeatedly while
--- input line editing is idle.  When the <span class="arg">func</span> function
--- completes, Clink will automatically refresh the prompt by triggering prompt
--- filtering again.
---
--- A coroutine is only created the first time each prompt filter calls this API
--- during a given input line session.  Subsequent calls reuse the
--- already-created coroutine.  (E.g. pressing <kbd>Enter</kbd> ends an input
--- line session.)
---
--- The API returns nil until the <span class="arg">func</span> function has
--- finished.  After that, the API returns whatever the
--- <span class="arg">func</span> function returned.  The API returns one value;
--- if multiple return values are needed, return them in a table.
---
--- If the <code>prompt.async</code> setting is disabled, then the coroutine
--- runs to completion immediately before returning.  Otherwise, the coroutine
--- runs during idle while editing the input line.  The
--- <span class="arg">func</span> function receives one argument: true if it's
--- running in the background, or false if it's running immediately.
---
--- See <a href="#asyncpromptfiltering">Asynchronous Prompt Filtering</a> for
--- more information.
---
--- <strong>Note:</strong> each prompt filter can have at most one prompt
--- coroutine.
function clink.promptcoroutine(func)
    if not prompt_filter_current then
        error("clink.promptcoroutine can only be used in a prompt filter", 2)
    end

    local entry = prompt_filter_coroutines[prompt_filter_current]
    if entry == nil then
        local info = debug.getinfo(func, 'S')
        src=info.short_src..":"..info.linedefined

        entry = { done=false, refilter=false, result=nil, src=src }
        prompt_filter_coroutines[prompt_filter_current] = entry

        local async = settings.get("prompt.async")

        -- Wrap the supplied function to track completion and end result.
        local dependency_inversion = { c=nil }
        coroutine.override_src(func)
        local c = coroutine.create(function (async)
            -- Call the supplied function.
            local o = func(async)
            -- Update the entry indicating completion.
            entry.done = true
            entry.refilter = true
            entry.result = o
            -- Refresh the prompt.
            if async then
                clink.removecoroutine(dependency_inversion.c)
            end
        end)
        dependency_inversion.c = c

        if async then
            -- Add the coroutine.
            clink.addcoroutine(c)
            clink._after_coroutines(refilterprompt_after_coroutines)
        else
            -- Run the coroutine synchronously if async is disabled.
            local max_iter = 25
            for iteration = 1, max_iter + 1, 1 do
                -- Pass false to let it know it is not async.
                local result, _ = coroutine.resume(c, false--[[async]])
                if result then
                    if coroutine.status(c) == "dead" then
                        break
                    end
                else
                    if _ and type(_) == "string" then
                        _error_handler(_)
                    end
                    break
                end
                -- Cap iterations when running synchronously, in case it's
                -- poorly behaved.
                if iteration >= max_iter then
                    -- Ideally this could print an error message about the
                    -- abandoning a misbehaving coroutine, but it would mess up
                    -- the prompt and input line display.
                    break
                end
            end
            -- Update the entry indicating completion.
            entry.done = true
        end
    end

    -- Return the result, if any.
    return entry.result
end
