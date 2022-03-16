-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local prompt_filters = {}
local prompt_filters_unsorted = false

if settings.get("lua.debug") or clink.DEBUG then
    clink.debug = clink.debug or {}
    clink.debug._prompt_filters = prompt_filters
end



--------------------------------------------------------------------------------
local prompt_filter_current = nil       -- Current running prompt filter.
local prompt_filter_coroutines = {}     -- Up to one coroutine per prompt filter, with cached return value.

--------------------------------------------------------------------------------
local function set_current_prompt_filter(filter)
    prompt_filter_current = filter
    clink._set_coroutine_context(filter)
end



--------------------------------------------------------------------------------
local function _do_filter_prompt(type, prompt, rprompt, line, cursor, final)
    -- Sort by priority if required.
    if prompt_filters_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(prompt_filters, lambda)

        prompt_filters_unsorted = false
    end

    local filter_func_name = type.."filter"
    local right_filter_func_name = type.."rightfilter"

    -- Protected call to prompt filters.
    local impl = function(prompt, rprompt)
        local filtered, onwards
        for _, filter in ipairs(prompt_filters) do
            set_current_prompt_filter(filter)

            -- Always call :filter() to help people to write backward compatible
            -- prompt filters.  Otherwise it's too easy to write Lua code that
            -- works on "new" Clink versions but throws a Lua exception on Clink
            -- versions that don't support RPROMPT.
            local func
            func = filter[filter_func_name]
            if func or #type == 0 then
                filtered, onwards = func(filter, prompt)
                if filtered ~= nil then
                    prompt = filtered
                    if onwards == false then return prompt, rprompt end
                end
            end

            func = filter[right_filter_func_name]
            if func then
                filtered, onwards = func(filter, rprompt)
                if filtered ~= nil then
                    rprompt = filtered
                    if onwards == false then return prompt, rprompt end
                end
            end
        end

        return prompt, rprompt
    end

    -- Backward compatibility shim.
    local old_rl_state = rl_state
    if line and final then
        rl_state = { line_buffer = line, point = cursor }
    else
        rl_state = nil
    end

    set_current_prompt_filter(nil)
    local ok, ret, rret = xpcall(impl, _error_handler_ret, prompt, rprompt)
    set_current_prompt_filter(nil)

    rl_state = old_rl_state

    if not ok then
        print("")
        print("prompt filter failed:")
        print(ret)
        return false
    end

    return ret, rret
end

--------------------------------------------------------------------------------
function clink._filter_prompt(prompt, rprompt, line, cursor)
    return _do_filter_prompt("", prompt, rprompt, line, cursor)
end

--------------------------------------------------------------------------------
function clink._filter_transient_prompt(prompt, rprompt, line, cursor, final)
    return _do_filter_prompt("transient", prompt, rprompt, line, cursor, final)
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
--- -ver:   1.0.0
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new promptfilter object that is applied in increasing
--- <span class="arg">priority</span> order (low values to high values).  Define
--- on the object a <code>:filter()</code> function that takes a string argument
--- which contains the filtered prompt so far.  The function can return nil to
--- have no effect, or can return a new prompt string.  It can optionally stop
--- further prompt filtering by also returning false.  See
--- <a href="#customisingtheprompt">Customizing the Prompt</a> for more
--- information.
--- -show:  local foo_prompt = clink.promptfilter(80)
--- -show:  function foo_prompt:filter(prompt)
--- -show:  &nbsp;   -- Insert the date at the beginning of the prompt.
--- -show:  &nbsp;   return os.date("%a %H:%M").." "..prompt
--- -show:  end
function clink.promptfilter(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(prompt_filters, ret)

    prompt_filters_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
--- -name:  clink.prompt.register_filter
--- -deprecated: clink.promptfilter
--- -arg:   filter_func:function
--- -arg:   [priority:integer]
--- -ret:   table
--- Registers a prompt filter function.  The capabilities are the same as before
--- but the syntax is changed.
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
local function print_filter_src(type)
    local any = false
    for _,prompt in ipairs (prompt_filters) do
        local func = prompt[type]
        if func then
            local info = debug.getinfo(func, 'S')
            if info.short_src ~= "?" then
                if not any then
                    clink.print("  "..type..":")
                    any = true
                end
                clink.print("", info.short_src..":"..info.linedefined)
            end
        end
    end
    return any
end

--------------------------------------------------------------------------------
function clink._diag_prompts()
    if not settings.get("lua.debug") then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.

    clink.print(bold.."prompt filters:"..norm)

    local any = false
    any = print_filter_src("filter") or any
    any = print_filter_src("rightfilter") or any
    any = print_filter_src("transientfilter") or any
    any = print_filter_src("transientrightfilter") or any

    if not any then
        clink.print("  no prompt filters registered")
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
--- -ver:   1.2.10
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
        coroutine.override_src(func)
        coroutine.override_isprompt()
        local c = coroutine.create(function (async)
            -- Call the supplied function.
            local o = func(async)
            -- Update the entry indicating completion.
            entry.done = true
            entry.refilter = true
            entry.result = o
        end)

        if async then
            -- Add the coroutine.
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
            clink.removecoroutine(c)
        end
    end

    -- Return the result, if any.
    return entry.result
end
