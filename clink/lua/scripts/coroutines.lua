-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

-- luacheck: max line length 150

--------------------------------------------------------------------------------
clink = clink or {}
local _coroutines = {}
local _after_coroutines = {}            -- Funcs to run after a pass resuming coroutines.
local _coroutines_resumable = false     -- When false, coroutines will no longer run.
local _coroutine_yieldguard = {}        -- Which coroutine is yielding inside popenyield, for a given category of coroutines.
local _coroutine_context = nil          -- Context for queuing io.popenyield calls from a same source.
local _coroutine_generation = 0         -- ID for current generation of coroutines.

local _dead = nil                       -- List of dead coroutines (only when "lua.debug" is set, or in DEBUG builds).
local _trimmed = 0                      -- Number of coroutines discarded from the dead list (overflow).
local _pending_on_main = nil            -- Funcs to run when control returns to the main coroutine.

local _main_perthread_state = {}
clink.co_state = _main_perthread_state

local print = clink.print

--------------------------------------------------------------------------------
-- Scheme for entries in _coroutines:
--
--  Initialized by coroutine.create:
--      coroutine:      The coroutine.
--      func:           The function the coroutine runs.
--      interval:       Interval at which to schedule the coroutine.
--      resumed:        How many times the coroutine has been resumed.
--      context:        The context in which the coroutine was created.
--      generation:     The generation to which this coroutine belongs.
--      yield_category: The category, for serializing types of yieldguards.
--      isprompt:       True means this is a prompt coroutine.
--      isgenerator:    True means this is a generator coroutine.
--      state:          Global state context for the coroutine (contains variables that are swapped).
--      co_state:       Global state context for the coroutine (the table itself is swapped).
--      src:            The source code file and line for the coroutine function.
--
--  Updated by the coroutine management system:
--      resumed:        Number of times the coroutine has been resumed.
--      firstclock:     The os.clock() from the beginning of the first resume.
--      throttleclock:  The os.clock() from the end of the most recent yieldguard.
--      lastclock:      The os.clock() from the end of the last resume.
--      queued:         Use INFINITE wait for this coroutine; it's queued inside popenyield.
--      yieldguard:     Yielding due to io.popen, os.execute, etc.

--------------------------------------------------------------------------------
local function clear_coroutines()
    -- Preserve the active popenyield entries so the system can tell when to
    -- dequeue the next one.
    local preserve = {}
    for _, cyg in pairs(_coroutine_yieldguard) do
        table.insert(preserve, _coroutines[cyg.coroutine])
    end

    for _, entry in pairs(_coroutines) do
        if entry.untilcomplete then
            table.insert(preserve, entry)
        end
    end

    _coroutines = {}
    _after_coroutines = {}
    _coroutines_resumable = false
    -- Don't touch _coroutine_yieldguard; it only gets cleared when the thread finishes.
    _coroutine_context = nil
    _coroutine_generation = _coroutine_generation + 1

    _dead = (settings.get("lua.debug") or clink.DEBUG) and {} or nil
    _trimmed = 0

    for _, entry in ipairs(preserve) do
        _coroutines[entry.coroutine] = entry
        _coroutines_resumable = true
    end
end
clink.onbeginedit(clear_coroutines)

--------------------------------------------------------------------------------
local function release_coroutine_yieldguard()
    local nil_cats = {}
    for category, cyg in pairs(_coroutine_yieldguard) do
        if cyg.yieldguard:ready() then
            local entry = _coroutines[cyg.coroutine]
            if entry and entry.yieldguard == cyg.yieldguard then
                entry.throttleclock = os.clock()
                entry.yieldguard = nil
                table.insert(nil_cats, category)
                -- TODO: This is an arbitrary order, but the dequeue order
                -- should ideally be FIFO.
                for _,e in pairs(_coroutines) do
                    if e.queued and e.category == category then
                        e.queued = nil
                        break
                    end
                end
            end
        end
    end
    for _, cat in ipairs(nil_cats) do
        _coroutine_yieldguard[cat] = nil
    end
end

--------------------------------------------------------------------------------
local function get_coroutine_generation(c)
    if c and _coroutines[c] then
        return _coroutines[c].generation
    end
end

--------------------------------------------------------------------------------
local function is_prompt_coroutine(c)
    if c and _coroutines[c] then
        return _coroutines[c].isprompt
    end
end

--------------------------------------------------------------------------------
local function set_coroutine_yieldguard(yieldguard)
    local t = coroutine.running()
    local entry = _coroutines[t]
    if yieldguard then
        local cyg = { coroutine=t, yieldguard=yieldguard }
        if entry.yield_category then
            _coroutine_yieldguard[entry.yield_category] = cyg
        else
            table.insert(_coroutine_yieldguard, cyg)
        end
    else
        release_coroutine_yieldguard()
    end
    if t and entry then
        entry.yieldguard = yieldguard
    end
end

--------------------------------------------------------------------------------
local function set_coroutine_queued(queued)
    local t = coroutine.running()
    if t and _coroutines[t] then
        _coroutines[t].queued = queued and true or nil
    end
end

--------------------------------------------------------------------------------
local function cancel_coroutine(message)
    clink._cancel_coroutine()
    error((message or "").."canceling popenyield; coroutine is orphaned")
end

--------------------------------------------------------------------------------
local function check_generation(c)
    if get_coroutine_generation(c) == _coroutine_generation then
        return true
    end
    local entry = _coroutines[c]
    if entry and entry.untilcomplete then
        return true
    end
    return false
end

--------------------------------------------------------------------------------
local function next_entry_target(entry, now)
    if not entry.lastclock then
        return 0
    else
        -- Multiple kinds of throttling for coroutines that want to run more
        -- frequently than every 5 seconds:
        --  1.  Throttle if running for 5 or more seconds, but reset the elapsed
        --      timer every time io.popenyield() finishes.
        --  2.  Throttle if running for more than 30 seconds total.
        -- Throttled coroutines can only run once every 5 seconds.
        local interval = entry.interval
        local throttleclock = entry.throttleclock or entry.firstclock
        if now and interval < 5 then
            if throttleclock and now - throttleclock > 5 then
                interval = 5
            elseif entry.firstclock and now - entry.firstclock > 30 then
                interval = 5
            end
        end
        return entry.lastclock + interval
    end
end

--------------------------------------------------------------------------------
function clink._after_coroutines(func)
    if type(func) ~= "function" then
        error("bad argument #1 (function expected)")
    end
    _after_coroutines[func] = func      -- Prevent duplicates.
end

--------------------------------------------------------------------------------
function clink._has_coroutines()
    return _coroutines_resumable
end

--------------------------------------------------------------------------------
function clink._wait_duration()
    if _coroutines_resumable then
        local target
        local now = os.clock()
        release_coroutine_yieldguard()  -- Dequeue next if necessary.
        for _,entry in pairs(_coroutines) do
            local this_target = next_entry_target(entry, now)
            if entry.yieldguard or entry.queued then -- luacheck: ignore 542
                -- Yield until output is ready; don't influence the timeout.
            elseif not target or target > this_target then
                target = this_target
            end
        end
        if target then
            return target - now
        end
    end
end

--------------------------------------------------------------------------------
function clink._set_coroutine_context(context)
    _coroutine_context = context
end

--------------------------------------------------------------------------------
local _coroutines_fallback_state = {}
function clink._resume_coroutines()
    if not _coroutines_resumable then
        return
    end

    -- Protected call to resume coroutines.
    local remove = {}
    local co
    local impl = function()
        for c,entry in pairs(_coroutines) do
            co = c
            if coroutine.status(c) == "dead" then
                table.insert(remove, c)
            elseif not check_generation(c) and not entry.yieldguard then
                entry.canceled = true
                table.insert(remove, c)
            else
                _coroutines_resumable = true
                local now = os.clock()
                if next_entry_target(entry, now) <= now then
                    if not entry.firstclock then
                        entry.firstclock = now
                    end
                    entry.resumed = entry.resumed + 1
                    clink._set_coroutine_context(entry.context)
                    local ok, ret
                    if entry.isprompt or entry.isgenerator then
                        ok, ret = coroutine.resume(c, true--[[async]])
                    else
                        ok, ret = coroutine.resume(c)
                    end
                    if ok then
                        -- Use live clock so the interval excludes the execution
                        -- time of the coroutine.
                        entry.lastclock = os.clock()
                    else
                        if not entry.canceled then
                            print("")
                            print("coroutine failed:")
                            _co_error_handler(c, ret)
                            entry.error = ret
                        end
                    end
                    if coroutine.status(c) == "dead" then
                        table.insert(remove, c)
                    end
                end
            end
        end
    end

    -- Prepare.
    _coroutines_resumable = false
    _coroutines_fallback_state = {}
    clink._set_coroutine_context(nil)

    -- Protected call.
    local ok, ret = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("coroutine failed:")
        _co_error_handler(co, ret)
        -- Don't return yet!  Need to do cleanup.
    end

    -- Cleanup.
    _coroutines_fallback_state = {}
    clink._set_coroutine_context(nil)
    for _,c in ipairs(remove) do
        clink.removecoroutine(c)
    end
    if _dead and #_dead > 20 then
        -- Trim the dead list to 20 entries.
        local t = {}
        local last = #_dead
        for i = last - 20 + 1, last, 1 do
            table.insert(t, _dead[i])
        end
        _trimmed = _trimmed + last - 20
        _dead = t
    end
    for _,func in pairs(_after_coroutines) do
        -- Protected call.
        ok, ret = xpcall(func, _error_handler_ret)
        if not ok then
            print("")
            print("callback failed:")
            print(ret)
            return
        end
    end
end

--------------------------------------------------------------------------------
function clink._finish_coroutine(c)
    while true do
        local status = coroutine.status(c)
        if not status or status == "dead" then
            break
        end

        local duration = clink._wait_duration()
        if duration and duration > 0 then
            local entry = _coroutines[c]
            if entry.yield_category then
                local cyg = _coroutine_yieldguard[entry.yield_category]
                if cyg then
                    cyg.yieldguard:wait(duration)
                end
            end
        end

        -- Must run all coroutines:  there could be inter-dependencies, and the
        -- target coroutine may be blocking on another coroutine's yieldguard.
        clink._resume_coroutines()
    end
end

--------------------------------------------------------------------------------
function clink._cancel_coroutine(c)
    if not c then
        c = coroutine.running()
    end

    local entry = _coroutines[c]
    if entry then
        -- Causes all globbers in the coroutine to short circuit.
        entry.canceled = true
    end
end

--------------------------------------------------------------------------------
function clink._is_coroutine_canceled(c)
    local entry = _coroutines[c]
    return entry and entry.canceled
end

--------------------------------------------------------------------------------
function clink._keep_coroutine_events(c)
    local entry = _coroutines[c]
    if entry then
        entry.keepevents = true
    end
end



--------------------------------------------------------------------------------
local function str_rpad(s, width, pad)
    if width <= #s then
        return s
    end
    return s..string.rep(pad or " ", width - #s)
end

--------------------------------------------------------------------------------
local function table_has_elements(t)
    if t then
        for _ in pairs(t) do -- luacheck: ignore 512
            return true
        end
    end
end

--------------------------------------------------------------------------------
local function spairs(t, order)
    -- collect the keys
    local keys = {}
    for k in pairs(t) do keys[#keys+1] = k end

    -- if order function given, sort by it by passing the table and keys a, b,
    -- otherwise just sort the keys
    if order then
        table.sort(keys, function(a,b) return order(t, a, b) end)
    else
        table.sort(keys)
    end

    -- return the iterator function
    local i = 0
    return function()
        i = i + 1
        if keys[i] then
            return keys[i], t[keys[i]]
        end
    end
end

--------------------------------------------------------------------------------
function clink._diag_coroutines(arg) -- luacheck: no unused
    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.
    local red = "\x1b[31m"          -- Red.
    local yellow = "\x1b[33m"       -- Yellow.
    local green = "\x1b[32m"        -- Green.
    local cyan = "\x1b[36m"         -- Cyan.
    local statcolor = "\x1b[35m"    -- Magenta.
    local deadlistcolor = "\x1b[90m" -- Bright black.

    local mixed_gen = false
    local show_gen = false
    local max_resumed_len = 0
    local max_freq_len = 0

    local function collect_diag(list, threads)
        for _,entry in pairs(list) do
            local resumed = tostring(entry.resumed)
            local status = entry.status or coroutine.status(entry.coroutine)
            local freq = tostring(entry.interval)
            if max_resumed_len < #resumed then
                max_resumed_len = #resumed
            end
            if max_freq_len < #freq then
                max_freq_len = #freq
            end
            if entry.generation ~= _coroutine_generation then
                if list == _coroutines then
                    mixed_gen = true
                end
                show_gen = true
            end
            table.insert(threads, { entry=entry, status=status, resumed=resumed, freq=freq })
        end
    end

    local function list_diag(threads, plain)
        for _,t in ipairs(threads) do
            local key = tostring(t.entry.coroutine):gsub("thread: ", "")..":"
            local gen = (t.entry.generation == _coroutine_generation) and "" or (yellow.."gen "..tostring(t.entry.generation)..plain.."  ")
            local status = (t.status == "suspended") and "" or (statcolor..t.status..plain.."  ")
            if t.entry.error then
                gen = red.."error"..plain.."  "..gen
            end
            if t.entry.yieldguard then
                status = status..yellow.."yieldguard"..plain.."  "
            end
            if t.entry.queued then
                status = status..yellow.."queued"..plain.."  "
            end
            if t.entry.canceled then
                status = status..cyan.."canceled"..plain.."  "
            else -- luacheck: ignore 542
                -- TODO: Show when throttled.
            end
            local res = "resumed "..str_rpad(t.resumed, max_resumed_len)
            local freq = "freq "..str_rpad(t.freq, max_freq_len)
            -- TODO: Show next wakeup time.
            local src = tostring(t.entry.src)
            print(plain.."  "..key.."  "..gen..status..res.."  "..freq.."  "..src..norm)
            if t.entry.error then
                print(plain.."  "..str_rpad("", #key + 2)..red..t.entry.error..norm)
            end
        end
    end

    local threads = {}
    local deadthreads = {}

    collect_diag(_coroutines, threads)
    if _dead then
        collect_diag(_dead, deadthreads)
    end

    -- Only list coroutines if there are any, or if there's unfinished state.
    local any_cyg = next(_coroutine_yieldguard) and true or false
    if table_has_elements(threads) or _coroutines_resumable or any_cyg then
        clink.print(bold.."coroutines:"..norm)
        if show_gen then
            print("  generation", (mixed_gen and yellow or norm).."gen ".._coroutine_generation..norm)
        end
        print("  resumable", _coroutines_resumable)
        print("  wait_duration", clink._wait_duration())
        for category, cyg in spairs(_coroutine_yieldguard) do
            local yg = cyg.yieldguard
            print("  "..category)
            print("    yieldguard      "..(yg:ready() and green.."ready"..norm or yellow.."yield"..norm))
            print("    yieldcommand    \""..yg:command().."\"")
        end
        list_diag(threads, norm)
    end

    -- Only list dead coroutines if there are any.
    if table_has_elements(_dead) then
        clink.print(bold.."dead coroutines:"..norm)
        if _trimmed > 0 then
            print("  "..deadlistcolor.."... ".._trimmed.." not listed ..."..norm)
        end
        list_diag(deadthreads, deadlistcolor)
    end
end



--------------------------------------------------------------------------------
local function restore_coroutine_state(entry, thread)
    if not entry then
        entry = _coroutines_fallback_state[thread]
        if not entry then
            entry = { state = {} }
            _coroutines_fallback_state[thread] = entry
        end
    end

    local state = entry.state
    local old_state = {}

    old_state.cwd = os.getcwd()
    if state.cwd and state.cwd ~= old_state.cwd then
        os.chdir(state.cwd)
    end

    if entry.isgenerator then
        old_state.rl_state = rl_state
        rl_state = state.rl_state
        if not entry.keepevents then
            old_state.events = clink._set_coroutine_events(state.events)
        end
    end

    old_state.global_modes = clink._save_global_modes()
    clink._restore_global_modes(state.global_modes)

    entry.old_state = old_state
end

--------------------------------------------------------------------------------
local function save_coroutine_state(entry, thread)
    if not entry then
        entry = _coroutines_fallback_state[thread]
        if not entry then -- Should be impossible; but return to avoid errors.
            return
        end
    end

    local state = entry.state
    local old_state = entry.old_state

    state.cwd = os.getcwd()
    if old_state and state.cwd ~= old_state.cwd then
        os.chdir(old_state.cwd)
    end

    if entry.isgenerator then
        state.rl_state = rl_state
        if old_state then
            rl_state = old_state.rl_state
            if not entry.keepevents then
                state.events = clink._set_coroutine_events(old_state.events)
            end
        end
    end

    -- When not old_state then this is a new coroutine.
    state.global_modes = clink._save_global_modes(not old_state--[[new_coroutine]]);
    if old_state then
        clink._restore_global_modes(old_state.global_modes)
    end

    entry.old_state = nil
end

--------------------------------------------------------------------------------
--- -name:  clink.addcoroutine
--- -ver:   1.2.10
--- -deprecated: clink.setcoroutineinterval
--- -arg:   coroutine:coroutine
--- -arg:   [interval:number]
--- Prior to v1.3.1 this was undocumented, and coroutines had to be manually
--- added in order to be scheduled for resume while waiting for input.  Starting
--- in v1.3.1 this is no longer necessary, but it can still be used to override
--- the interval at which to resume the coroutine.
function clink.addcoroutine(c, interval)
    if type(c) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if interval ~= nil and type(interval) ~= "number" then
        error("bad argument #2 (number or nil expected)")
    end

    -- Change the interval for a coroutine.
    if _coroutines[c] and not _coroutines[c].throttled then
        _coroutines[c].interval = interval
    end
end

--------------------------------------------------------------------------------
function clink.removecoroutine(c)
    if type(c) == "thread" then
        release_coroutine_yieldguard()
        if _dead then
            local entry = _coroutines[c]
            if entry then
                local status = coroutine.status(c)
                -- Clear references.
                entry.status = (status == "dead") and status or "abandoned ("..status..")"
                entry.coroutine = tostring(c)
                entry.func = nil
                entry.context = nil
                entry.events = nil
                entry.state = nil
                entry.old_state = nil
                -- Move the coroutine's tracking entry to the dead list.
                table.insert(_dead, entry)
            end
        end
        _coroutines[c] = nil
        _coroutines_resumable = false
        for _ in pairs(_coroutines) do -- luacheck: ignore 512
            _coroutines_resumable = true
            break
        end
    elseif c ~= nil then
        error("bad argument #1 (coroutine expected)")
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.setcoroutineinterval
--- -ver:   1.3.1
--- -arg:   coroutine:coroutine
--- -arg:   [interval:number]
--- Overrides the interval at which a coroutine is resumed.  All coroutines are
--- automatically added with an interval of 0 by default, so calling this is
--- only needed when you want to change the interval.
---
--- Coroutines are automatically resumed while waiting for input while editing
--- the input line.
---
--- If a coroutine's interval is less than 5 seconds and the coroutine has been
--- alive for more than 5 seconds, then the coroutine is throttled to run no
--- more often than once every 5 seconds (regardless how much total time is has
--- spent running).  Throttling is meant to prevent long-running coroutines from
--- draining battery power, interfering with responsiveness, or other potential
--- problems.
function clink.setcoroutineinterval(c, interval)
    if type(c) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if interval ~= nil and type(interval) ~= "number" then
        error("bad argument #2 (number or nil expected)")
    end
    if not _coroutines[c] then
        if settings.get("lua.strict") then
            error("bad argument #1 (coroutine does not exist)")
        end
        return
    end

    -- Override the interval.  The scheduler never trusts the interval, so it's
    -- ok to blindly set the interval here even if the coroutine is currently
    -- being throttled.
    _coroutines[c].interval = interval
end

--------------------------------------------------------------------------------
--- -name:  clink.runcoroutineuntilcomplete
--- -ver:   1.3.5
--- -arg:   coroutine:coroutine
--- By default, a coroutine is canceled if it doesn't complete before an edit
--- line ends.  In some cases it may be necessary for a coroutine to run until
--- it completes, even if it spans multiple edit lines.
---
--- <strong>Use this with caution:</strong>  This can potentially cause
--- performance problems or cause prompt filtering to experience delays.
function clink.runcoroutineuntilcomplete(c)
    if type(c) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if not _coroutines[c] then
        if settings.get("lua.strict") then
            error("bad argument #1 (coroutine does not exist)")
        end
        return
    end

    -- Set the coroutine to run until complete.
    _coroutines[c].untilcomplete = true
end

--------------------------------------------------------------------------------
--- -name:  clink.runonmain
--- -ver:   1.5.21
--- -arg:   func:function
--- Runs <span class="arg">func</span> on the main coroutine.
---
--- If main is the current coroutine, then <span class="arg">func</span> runs
--- immediately.
---
--- If main is not the current coroutine, then <span class="arg">func</span> is
--- scheduled to run when control returns to the main coroutine.
function clink.runonmain(func)
    local _, ismain = coroutine.running()
    if ismain then
        func()
    else
        _pending_on_main = _pending_on_main or {}
        if not _pending_on_main[func] then
            table.insert(_pending_on_main, func)
            _pending_on_main[func] = true
        end
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.setcoroutinename
--- -ver:   1.3.1
--- -arg:   coroutine:coroutine
--- -arg:   name:string
--- Sets a name for the coroutine.  This is purely for diagnostic purposes.
function clink.setcoroutinename(c, name)
    if type(c) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if name and name ~= "" and _coroutines[c] then
        _coroutines[c].src = "'"..name.."'"
    end
end



--------------------------------------------------------------------------------
--- -name:  io.popenyield
--- -ver:   1.2.10
--- -arg:   command:string
--- -arg:   [mode:string]
--- -ret:   file, function (see remarks below)
--- This behaves similar to
--- <a href="https://www.lua.org/manual/5.2/manual.html#pdf-io.popen">io.popen()</a>
--- except that it only supports read mode and when used in a coroutine it
--- yields until the command has finished.
---
--- The <span class="arg">command</span> argument is the command to run.
---
--- The <span class="arg">mode</span> argument is the optional mode to use.  It
--- can contain "r" (read mode) and/or either "t" for text mode (the default if
--- omitted) or "b" for binary mode.  Write mode is not supported, so it cannot
--- contain "w".
---
--- This runs the specified command and returns a read file handle for reading
--- output from the command.  It yields until the command has finished and the
--- complete output is ready to be read without blocking.
---
--- In v1.3.31 and higher, it may also return a function.  If the second return
--- value is a function then it can be used to get the exit status for the
--- command.  The function returns the same values as
--- <a href="https://www.lua.org/manual/5.2/manual.html#pdf-os.execute">os.execute()</a>.
--- The function may be used only once, and it closes the read file handle, so
--- if the function is used then do not use <code>file:close()</code>.  Or, if
--- the second return value is not a function, then the exit status may be
--- retrieved from calling <code>file:close()</code> on the returned file handle.
---
--- <strong>Compatibility Note:</strong> when <code>io.popen()</code> is used in
--- a coroutine, it is automatically redirected to <code>io.popenyield()</code>.
--- This means on success the second return value from <code>io.popen()</code>
--- in a coroutine may not be nil as callers might normally expect.
---
--- <strong>Note:</strong> if the
--- <code><a href="#prompt_async">prompt.async</a></code> setting is disabled,
--- or while a <a href="#transientprompts">transient prompt filter</a> is
--- executing, or if used outside of a coroutine, then this behaves like
--- <a href="https://www.lua.org/manual/5.2/manual.html#pdf-io.popen">io.popen()</a>
--- instead.
--- -show:  local file = io.popenyield("git status")
--- -show:
--- -show:  while (true) do
--- -show:  &nbsp;   local line = file:read("*line")
--- -show:  &nbsp;   if not line then
--- -show:  &nbsp;       break
--- -show:  &nbsp;   end
--- -show:  &nbsp;   do_things_with(line)
--- -show:  end
--- -show:  file:close()
--- Here is an example showing how to get the exit status, if desired:
--- -show:  -- Clink v1.3.31 and higher return a pclose function, for optional use.
--- -show:  local file, pclose = io.popenyield("kubectl.exe")
--- -show:  if file then
--- -show:  &nbsp;   local ok, what, code = pclose()
--- -show:  end
function io.popenyield(command, mode)
    -- This outer wrapper is implemented in Lua so that it can yield.
    local c, ismain = coroutine.running()
    local can_async = false
    if not ismain then
        -- Prompt coroutines may not run async under certain conditions.
        if is_prompt_coroutine(c) then
            can_async = settings.get("prompt.async") and not clink.istransientpromptfilter()
        else
            can_async = true
        end
    end
    if can_async then
        -- Yield to ensure only one yieldable API is active at a time.
        local category = _coroutines[c] and _coroutines[c].yield_category
        if _coroutine_yieldguard[category] then
            set_coroutine_queued(true)
            while _coroutine_yieldguard[category] do
                coroutine.yield()
                if clink._is_coroutine_canceled(c) then
                    break
                end
            end
            set_coroutine_queued(false)
            if clink._is_coroutine_canceled(c) then
                return io.open("nul")
            end
        end
        -- Cancel if not from the current generation.
        if not check_generation(c) then
            local message = (type(command) == string) and command..": " or ""
            cancel_coroutine(message)
            return io.open("nul")
        end
        -- Start the popenyield.
        local file, yieldguard = io.popenyield_internal(command, mode)
        if file and yieldguard then
            set_coroutine_yieldguard(yieldguard)
            while not yieldguard:ready() do
                coroutine.yield()
                -- Do not allow canceling once the process has been spawned.
                -- This enforces no more than one spawned background process is
                -- running at a time.
            end
            set_coroutine_yieldguard(nil)
        end
        -- Make a pclose function.
        local state = { yg=yieldguard }
        local pclose = file and yieldguard and function ()
            -- Only allow to run once.
            if state.zombie then
                error("function already used; can only be used once.")
                return
            end
            state.zombie = true
            -- Close the file.
            file:close()
            -- Make ready() wait for process exit.
            yieldguard:set_need_completion()
            -- Yield until ready.
            set_coroutine_yieldguard(yieldguard)
            while not yieldguard:ready() do
                coroutine.yield()
                -- Do not allow canceling.  This enforces no more than one spawned
                -- background process is running at a time.
            end
            set_coroutine_yieldguard(nil)
            -- Return exit status.
            return yieldguard:results()
        end
        return file, pclose
    else
        return io.popen(command, mode)
    end
end

--------------------------------------------------------------------------------
-- MAGIC:  Redirect io.popen to io.popenyield when used in read mode, so that
-- match generators automatically yield in coroutines.
local old_io_popen = io.popen
io.popen = function (command, mode)
    if not mode or not mode:find("w") then
        local _, ismain = coroutine.running()
        if not ismain then
            return io.popenyield(command, mode)
        end
    end

    return old_io_popen(command, mode)
end

--------------------------------------------------------------------------------
-- MAGIC:  Redirect os.execute to os.executeyield when used in a coroutine.
local old_os_execute = os.execute
os.execute = function (command)
    -- This outer wrapper is implemented in Lua so that it can yield.
    local c, ismain = coroutine.running()
    if ismain or command == nil then
        return old_os_execute(command)
    end
    -- Yield to ensure only one yieldable API is active at a time.
    local category = _coroutines[c] and _coroutines[c].yield_category
    if _coroutine_yieldguard[category] then
        set_coroutine_queued(true)
        while _coroutine_yieldguard[category] do
            coroutine.yield()
            if clink._is_coroutine_canceled(c) then
                break
            end
        end
        set_coroutine_queued(false)
        if clink._is_coroutine_canceled(c) then
            return nil, "exit", -1, "canceled"
        end
    end
    -- Cancel if not from the current generation.
    if not check_generation(c) then
        local message = (type(command) == string) and command..": " or ""
        cancel_coroutine(message)
        return nil, "exit", -1, "canceled"
    end
    -- Start the executeyield.
    local yieldguard = os.executeyield_internal(command)
    if yieldguard then
        set_coroutine_yieldguard(yieldguard)
        while not yieldguard:ready() do
            coroutine.yield()
            -- Do not allow canceling once the process has been spawned.
            -- This enforces no more than one spawned background process is
            -- running at a time.
        end
        set_coroutine_yieldguard(nil)
        return yieldguard:results()
    else
        return nil, "exit", -1, "failed"
    end
end

--------------------------------------------------------------------------------
local override_coroutine_src_func = nil
function coroutine.override_src(func)
    override_coroutine_src_func = func
end

--------------------------------------------------------------------------------
local override_coroutine_isprompt = nil
function coroutine.override_isprompt()
    override_coroutine_isprompt = true
end

--------------------------------------------------------------------------------
local override_coroutine_isgenerator = nil
function coroutine.override_isgenerator()
    override_coroutine_isgenerator = true
end

--------------------------------------------------------------------------------
local orig_coroutine_create = coroutine.create
function coroutine.create(func) -- luacheck: ignore 122
    -- Get src of func.
    local src = override_coroutine_src_func or func
    if src then
        local info = debug.getinfo(src, 'S')
        src = info.short_src..":"..info.linedefined
    else
        src = "<unknown>"
    end
    override_coroutine_src_func = nil

    -- Get role.
    local isprompt = override_coroutine_isprompt
    local isgenerator = override_coroutine_isgenerator
    override_coroutine_isprompt = nil
    override_coroutine_isgenerator = nil

    local entry = {
        interval=0,
        resumed=0,
        func=func,
        context=_coroutine_context,
        generation=_coroutine_generation,
        isprompt=isprompt,
        isgenerator=isgenerator,
        yield_category=(isprompt and "prompt" or (isgenerator and "generator")),
        state={},
        co_state={},
        src=src,
    }
    save_coroutine_state(entry)

    local thread = orig_coroutine_create(func)
    entry.coroutine = thread
    _coroutines[thread] = entry

    -- Wake up idle processing.
    _coroutines_resumable = true
    clink.kick_idle()
    return thread
end

--------------------------------------------------------------------------------
local orig_coroutine_resume = coroutine.resume
function coroutine.resume(co, ...) -- luacheck: ignore 122
    local entry = _coroutines[co]
    restore_coroutine_state(entry, co)

    local old_co_state = clink.co_state
    clink.co_state = entry.co_state

    local ret = table.pack(orig_coroutine_resume(co, ...))

    if ret and not ret[1] and ret[2] then
        local err = tostring(ret[2])
        entry.error = err
        if settings.get("lua.debug") then
            local full_err = debug.traceback(co, err)
            log.info("error in coroutine:  "..full_err)
        end
    end

    clink.co_state = old_co_state
    save_coroutine_state(entry, co)

    if _pending_on_main then
        local _, ismain = coroutine.running()
        if ismain then
            for _, func in ipairs(_pending_on_main) do
                -- Protected call.
                local ok
                ok, ret = xpcall(func, _error_handler_ret)
                if not ok then
                    print("")
                    print("runonmain callback failed:")
                    print(ret)
                    -- Don't return; finish the loop since the queued
                    -- functions may be unrelated.
                end
            end
            _pending_on_main = nil
        end
    end

    return table.unpack(ret)
end
