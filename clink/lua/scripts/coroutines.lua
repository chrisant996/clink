-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _coroutines = {}
local _coroutines_created = {}          -- Remembers creation info for each coroutine, for use by clink.addcoroutine.
local _after_coroutines = {}            -- Funcs to run after a pass resuming coroutines.
local _coroutines_resumable = false     -- When false, coroutines will no longer run.
local _coroutine_yieldguard = nil       -- Which coroutine is yielding inside popenyield.
local _coroutine_context = nil          -- Context for queuing io.popenyield calls from a same source.
local _coroutine_canceled = false       -- Becomes true if an orphaned io.popenyield cancels the coroutine.
local _coroutine_generation = 0         -- ID for current generation of coroutines.

local _dead = nil                       -- List of dead coroutines (only when "lua.debug" is set, or in DEBUG builds).
local _trimmed = 0                      -- Number of coroutines discarded from the dead list (overflow).

local print = clink.print

--------------------------------------------------------------------------------
-- Scheme for entries in _coroutines:
--
--  Initialized by clink.addcoroutine:
--      coroutine:      The coroutine.
--      func:           The function the coroutine runs.
--      interval:       Interval at which to schedule the coroutine.
--      context:        The context in which the coroutine was created.
--      generation:     The generation to which this coroutine belongs.
--
--  Updated by the coroutine management system:
--      resumed:        Number of times the coroutine has been resumed.
--      firstclock:     The os.clock() from the beginning of the first resume.
--      throttleclock:  The os.clock() from the end of the most recent yieldguard.
--      lastclock:      The os.clock() from the end of the last resume.
--      infinite:       Use INFINITE wait for this coroutine; it's actively inside popenyield.
--      queued:         Use INFINITE wait for this coroutine; it's queued inside popenyield.

--------------------------------------------------------------------------------
local function clear_coroutines()
    local preserve = {}
    if _coroutine_yieldguard then
        -- Preserve the active popenyield entry so the system can tell when to
        -- dequeue the next one.
        table.insert(preserve, _coroutines[_coroutine_yieldguard.coroutine])
    end

    for _, entry in pairs(_coroutines) do
        if entry.untilcomplete then
            table.insert(preserve, entry)
        end
    end

    _coroutines = {}
    _coroutines_created = {}
    _after_coroutines = {}
    _coroutines_resumable = false
    -- Don't touch _coroutine_yieldguard; it only gets cleared when the thread finishes.
    _coroutine_context = nil
    _coroutine_canceled = false
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
    if _coroutine_yieldguard and _coroutine_yieldguard.yieldguard:ready() then
        local entry = _coroutines[_coroutine_yieldguard.coroutine]
        if entry and entry.yieldguard == _coroutine_yieldguard.yieldguard then
            entry.throttleclock = os.clock()
            entry.yieldguard = nil
            _coroutine_yieldguard = nil
            for _,entry in pairs(_coroutines) do
                if entry.queued then
                    entry.queued = nil
                    break
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
local function get_coroutine_generation()
    local t = coroutine.running()
    if t and _coroutines[t] then
        return _coroutines[t].generation
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
    if yieldguard then
        _coroutine_yieldguard = { coroutine=t, yieldguard=yieldguard }
    else
        release_coroutine_yieldguard()
    end
    if t and _coroutines[t] then
        _coroutines[t].yieldguard = yieldguard
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
    _coroutine_canceled = true
    clink._cancel_coroutine()
    error((message or "").."canceling popenyield; coroutine is orphaned")
end

--------------------------------------------------------------------------------
local function check_generation(c)
    if get_coroutine_generation() == _coroutine_generation then
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
            if entry.yieldguard or entry.queued then
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
    _coroutine_canceled = false
end

--------------------------------------------------------------------------------
function clink._resume_coroutines()
    if not _coroutines_resumable then
        return
    end

    -- Protected call to resume coroutines.
    local remove = {}
    local impl = function()
        for _,entry in pairs(_coroutines) do
            if coroutine.status(entry.coroutine) == "dead" then
                table.insert(remove, _)
            else
                _coroutines_resumable = true
                local now = os.clock()
                if next_entry_target(entry, now) <= now then
                    local events
                    local old_rl_state
                    if not entry.firstclock then
                        entry.firstclock = now
                    end
                    entry.resumed = entry.resumed + 1
                    clink._set_coroutine_context(entry.context)
                    if entry.isgenerator then
                        old_rl_state = rl_state
                        rl_state = entry.rl_state
                        if not entry.keepevents then
                            events = clink._set_coroutine_events(entry.events)
                        end
                    end
                    local ok, ret = coroutine.resume(entry.coroutine, true--[[async]])
                    if entry.isgenerator then
                        entry.rl_state = rl_state
                        rl_state = old_rl_state
                        if not entry.keepevents then
                            entry.events = clink._set_coroutine_events(events)
                        end
                    end
                    if ok then
                        -- Use live clock so the interval excludes the execution
                        -- time of the coroutine.
                        entry.lastclock = os.clock()
                    else
                        if _coroutine_canceled then
                            entry.canceled = true
                        else
                            print("")
                            print("coroutine failed:")
                            print(ret)
                            entry.error = ret
                        end
                    end
                    if coroutine.status(entry.coroutine) == "dead" then
                        table.insert(remove, _)
                    end
                end
            end
        end
    end

    -- Prepare.
    _coroutines_resumable = false
    clink._set_coroutine_context(nil)

    -- Protected call.
    local ok, ret = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("coroutine failed:")
        print(ret)
        -- Don't return yet!  Need to do cleanup.
    end

    -- Cleanup.
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
        func()
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
        if _coroutine_yieldguard and duration and duration > 0 then
            _coroutine_yieldguard.yieldguard:wait(duration)
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
        for _ in pairs(t) do
            return true
        end
    end
end

--------------------------------------------------------------------------------
function clink._diag_coroutines()
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
    local threads = {}
    local deadthreads = {}
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
            end
            local res = "resumed "..str_rpad(t.resumed, max_resumed_len)
            local freq = "freq "..str_rpad(t.freq, max_freq_len)
            local src = tostring(t.entry.src)
            print(plain.."  "..key.."  "..gen..status..res.."  "..freq.."  "..src..norm)
            if t.entry.error then
                print(plain.."  "..str_rpad("", #key + 2)..red..t.entry.error..norm)
            end
        end
    end

    collect_diag(_coroutines, threads)
    if _dead then
        collect_diag(_dead, deadthreads)
    end

    -- Only list coroutines if there are any, or if there's unfinished state.
    if table_has_elements(threads) or _coroutines_resumable or _coroutine_yieldguard then
        clink.print(bold.."coroutines:"..norm)
        if show_gen then
            print("  generation", (mixed_gen and yellow or norm).."gen ".._coroutine_generation..norm)
        end
        print("  resumable", _coroutines_resumable)
        print("  wait_duration", clink._wait_duration())
        if _coroutine_yieldguard then
            local yg = _coroutine_yieldguard.yieldguard
            print("  yieldguard", (yg:ready() and green.."ready"..norm or yellow.."yield"..norm))
            print("  yieldcommand", '"'..yg:command()..'"')
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
    if _coroutines[c] then
        if not _coroutines[c].throttled then
            _coroutines[c].interval = interval
        end
        return
    end

    -- Add a new coroutine.
    local created_info = _coroutines_created[c] or {}
    _coroutines[c] = {
        coroutine=c,
        interval=interval or created_info.interval or 0,
        resumed=0,
        func=created_info.func,
        context=created_info.context,
        generation=created_info.generation,
        isprompt=created_info.isprompt,
        isgenerator=created_info.isgenerator,
        rl_state=rl_state,
        src=created_info.src,
    }
    _coroutines_created[c] = nil
    _coroutines_resumable = true
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
                -- Move the coroutine's tracking entry to the dead list.
                table.insert(_dead, entry)
            end
        end
        _coroutines[c] = nil
        _coroutines_resumable = false
        for _ in pairs(_coroutines) do
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
--- <strong>Note:</strong>  Use with caution.  This can potentially cause
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
--- -ret:   file
--- This is the same as
--- <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>(<span class="arg">command</span>, <span class="arg">mode</span>)</code>
--- except that it only supports read mode and it yields until the command has
--- finished:
---
--- Runs <span class="arg">command</span> and returns a read file handle for
--- reading output from the command.  It yields until the command has finished
--- and the complete output is ready to be read without blocking.
---
--- The <span class="arg">mode</span> can contain "r" (read mode) and/or either
--- "t" for text mode (the default if omitted) or "b" for binary mode.  Write
--- mode is not supported, so it cannot contain "w".
---
--- <strong>Note:</strong> if the <code>prompt.async</code> setting is disabled,
--- or while a <a href="#transientprompts">transient prompt filter</a> is
--- executing, then this behaves like
--- <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>(<span class="arg">command</span>, <span class="arg">mode</span>)</code>
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
        if _coroutine_yieldguard then
            set_coroutine_queued(true)
            while _coroutine_yieldguard do
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
        return file
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
    if _coroutine_yieldguard then
        set_coroutine_queued(true)
        while _coroutine_yieldguard do
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
function coroutine.create(func)
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

    -- Remember original func for diagnostic purposes later.  The table is
    -- cleared at the beginning of each input line.
    local cwd = os.getcwd()
    local thread = orig_coroutine_create(func)
    _coroutines_created[thread] = {
        func=func,
        context=_coroutine_context,
        generation=_coroutine_generation,
        isprompt=isprompt,
        isgenerator=isgenerator,
        cwd=cwd,
        src=src
    }
    clink.addcoroutine(thread)

    -- Wake up idle processing.
    clink.kick_idle()
    return thread
end

--------------------------------------------------------------------------------
local orig_coroutine_resume = coroutine.resume
function coroutine.resume(co, ...)
    local entry = _coroutines[co]

    if entry and entry.cwd and entry.cwd ~= os.getcwd() then
        os.chdir(entry.cwd)
    end

    local ret = table.pack(orig_coroutine_resume(co, ...))

    if entry then
        entry.cwd = os.getcwd()
    end

    return table.unpack(ret)
end
