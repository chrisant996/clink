-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local prompt_filters = {}
local prompt_filters_unsorted = false

if settings.get("lua.debug") or clink.DEBUG then
    -- Make it possible to inspect these locals in the debugger.
    clink.debug = clink.debug or {}
    clink.debug._prompt_filters = prompt_filters
end



--------------------------------------------------------------------------------
local prompt_filter_current = nil       -- Current running prompt filter.
local prompt_filter_coroutines = {}     -- Up to one coroutine per prompt filter, with cached return value.

--------------------------------------------------------------------------------
local bold = "\x1b[1m"                  -- Bold (bright).
local header = "\x1b[36m"               -- Cyan.
local norm = "\x1b[m"                   -- Normal.

--------------------------------------------------------------------------------
local function set_current_prompt_filter(filter)
    prompt_filter_current = filter
    clink._set_coroutine_context(filter)
end



--------------------------------------------------------------------------------
local function log_cost(tick, filter, func_name)
    local elapsed = (os.clock() - tick) * 1000
    local tname = "cost"..func_name
    local cost = filter[tname]
    if not cost then
        cost = { last=0, total=0, num=0, peak=0 }
        filter[tname] = cost
    end

    cost.last = elapsed
    cost.total = cost.total + elapsed
    cost.num = cost.num + 1
    if cost.peak < elapsed then
        cost.peak = elapsed
    end
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

    local pre,suf,rpre,rsuf = "","","",""

    -- Protected call to prompt filters.
    local impl = function(prompt, rprompt) -- luacheck: ignore 432
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
                local tick = os.clock()
                filtered, onwards = func(filter, prompt)
                log_cost(tick, filter, filter_func_name)
                if filtered ~= nil then
                    prompt = filtered
                end
            end

            if onwards ~= false then
                func = filter[right_filter_func_name]
                if func then
                    local tick = os.clock()
                    filtered, onwards = func(filter, rprompt)
                    log_cost(tick, filter, right_filter_func_name)
                    if filtered ~= nil then
                        rprompt = filtered
                    end
                end
            end

            func = filter.surround
            if func then
                local a,b,c,d = func(filter)
                -- Earlier prompt filters take precedence
                if a then pre = pre .. a end
                if b then suf = b .. suf end
                if c then rpre = rpre .. c end
                if d then rsuf = d .. rsuf end
            end

            if onwards == false then
                break
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

    if ret then
        ret = pre .. ret .. suf
    end
    if rret and rret ~= "" then
        rret = rpre .. rret .. rsuf
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
function clink._diag_refilter(arg) -- luacheck: no unused
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
    function o:filter(the_prompt) -- luacheck: no self
        clink.prompt.value = the_prompt
        local stop = filter(the_prompt)
        return clink.prompt.value, not stop
    end
end

--------------------------------------------------------------------------------
local function pad_string(s, len)
    if #s < len then
        s = s..string.rep(" ", len - #s)
    end
    return s
end

--------------------------------------------------------------------------------
local function max_len(a, b)
    a = a or b or 0
    b = b or a or 0
    return a > b and a or b
end

--------------------------------------------------------------------------------
local function collect_filter_src(t, type)
    local tsub = {}
    t[type] = tsub

    local any_cost
    local longest = 24
    for _,prompt in ipairs (prompt_filters) do
        local func = prompt[type]
        if func then
            local info = debug.getinfo(func, 'S')
            if not clink._is_internal_script(info.short_src) then
                local src = info.short_src..":"..info.linedefined
                local cost = prompt["cost"..type]
                table.insert(tsub, { src=src, cost=cost })
                if longest < #src then
                    longest = #src
                end
                if not any_cost and cost then
                    any_cost = true
                end
            end
        end
    end
    tsub.any_cost = any_cost
    t.longest = max_len(t.longest, longest)
end

--------------------------------------------------------------------------------
local function print_filter_src(t, type)
    local tsub = t[type]

    if tsub[1] then
        local longest = t.longest
        if tsub.any_cost then
            clink.print(string.format("  %s           %slast    avg     peak%s",
                    pad_string(type..":", longest), header, norm))
        else
            clink.print("  "..type..":")
        end
        for _,entry in ipairs (tsub) do
            if entry.cost then
                clink.print(string.format("        %s  %4u ms %4u ms %4u ms",
                        pad_string(entry.src, longest),
                        entry.cost.last, entry.cost.total / entry.cost.num, entry.cost.peak))
            else
                clink.print(string.format("        %s", entry.src))
            end
        end
        return true
    end
end

--------------------------------------------------------------------------------
function clink._diag_prompts(arg)
    arg = (arg and arg >= 1)
    if not arg and not settings.get("lua.debug") then
        return
    end

    clink.print(bold.."prompt filters:"..norm)

    local t = {}
    collect_filter_src(t, "filter")
    collect_filter_src(t, "rightfilter")
    collect_filter_src(t, "transientfilter")
    collect_filter_src(t, "transientrightfilter")

    local any = false
    any = print_filter_src(t, "filter") or any
    any = print_filter_src(t, "rightfilter") or any
    any = print_filter_src(t, "transientfilter") or any
    any = print_filter_src(t, "transientrightfilter") or any

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
--- If the <code><a href="#prompt_async">prompt.async</a></code> setting is
--- disabled, then the coroutine runs to completion immediately before
--- returning.  Otherwise, the coroutine runs during idle while editing the
--- input line.  The <span class="arg">func</span> function receives one
--- argument: true if it's running in the background, or false if it's running
--- immediately.
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
        local src=info.short_src..":"..info.linedefined

        entry = { done=false, refilter=false, result=nil, src=src }
        prompt_filter_coroutines[prompt_filter_current] = entry

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

        local async = settings.get("prompt.async")

        if async then
            -- Add the coroutine.
            clink._after_coroutines(refilterprompt_after_coroutines)
        else
            -- Run the coroutine synchronously if async is disabled.
            local max_iter = 25
            for iteration = 1, max_iter + 1, 1 do
                -- Pass false to let it know it is not async.
                local ok, ret = coroutine.resume(c, false--[[async]])
                if ok then
                    if coroutine.status(c) == "dead" then
                        break
                    end
                else
                    if ret and type(ret) == "string" then
                        _error_handler(ret)
                        entry.error = ret
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
