-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _coroutines = {}
local _coroutines_created = {}
local _after_coroutines = {}
local _coroutines_resumable = false
local _coroutine_infinite = nil

--------------------------------------------------------------------------------
local function clear_coroutines()
    _coroutines = {}
    _coroutines_created = {}
    _after_coroutines = {}
    _coroutines_resumable = false
    _coroutine_infinite = nil
end
clink.onbeginedit(clear_coroutines)

--------------------------------------------------------------------------------
local function after_coroutines()
    for _,func in pairs(_after_coroutines) do
        func()
    end
end

--------------------------------------------------------------------------------
local function next_entry_target(entry, now)
    if not entry.lastclock then
        return 0
    else
        -- Throttle any coroutine that's been running for 5 or more seconds and
        -- wants to run more frequently than every 5 seconds.  They still get to
        -- run, but only once every 5 seconds.
        local interval = entry.interval
        if interval < 5 and now and entry.firstclock and now - entry.firstclock > 5 then
            interval = 5
        end
        return entry.lastclock + interval
    end
end

--------------------------------------------------------------------------------
function clink._after_coroutines(func)
    if type(func) ~= "function" then
        error("bad argument #1 (function expected)")
    end
    _after_coroutines[func] = func
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
        for _,entry in pairs(_coroutines) do
            local this_target = next_entry_target(entry, now)
            if _coroutine_infinite == entry.coroutine then
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
function clink._resume_coroutines()
    if _coroutines_resumable then
        _coroutines_resumable = false
        local remove = {}
        for _,entry in pairs(_coroutines) do
            if coroutine.status(entry.coroutine) == "dead" then
                table.insert(remove, _)
            else
                _coroutines_resumable = true
                local now = os.clock()
                if next_entry_target(entry, now) < now then
                    if not entry.firstclock then
                        entry.firstclock = now
                    end
                    entry.resumed = entry.resumed + 1
                    if coroutine.resume(entry.coroutine, true--[[async]]) then
                        -- Use live clock so the interval excludes the execution
                        -- time of the coroutine.
                        entry.lastclock = os.clock()
                    else
                        table.insert(remove, _)
                    end
                end
            end
        end
        for _,c in ipairs(remove) do
            clink.removecoroutine(c)
        end
        after_coroutines()
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
function clink._diag_coroutines()
    local bold = "\x1b[1m"
    local norm = "\x1b[m"

    local total = 0
    local dead = 0
    local threads = {}
    local max_status_len = 0
    local max_resumed_len = 0
    local max_freq_len = 0
    for _,entry in pairs(_coroutines) do
        local src
        local resumed = tostring(entry.resumed)
        local status = coroutine.status(entry.coroutine)
        local freq = tostring(entry.interval)
        if entry.func then
            local info = debug.getinfo(entry.func, 'S')
            src=info.short_src.."("..info.linedefined..")"
        else
            src="<unknown>"
        end
        if max_status_len < #status then
            max_status_len = #status
        end
        if max_resumed_len < #resumed then
            max_resumed_len = #resumed
        end
        if max_freq_len < #freq then
            max_freq_len = #freq
        end
        table.insert(threads, { coroutine=entry.coroutine, status=status, resumed=resumed, freq=freq, src=src })
    end

    clink.print(bold.."coroutines:"..norm)
    print("  resumable", _coroutines_resumable)
    print("  popenyield", _coroutine_infinite)
    for _,t in ipairs(threads) do
        local col1 = tostring(t.coroutine):gsub("thread: ", "")
        local col2 = str_rpad(t.status, max_status_len)
        local col3 = "ran "..str_rpad(t.resumed, max_resumed_len)
        local col4 = "freq "..str_rpad(t.freq, max_freq_len)
        local col5 = t.src
        print("  "..col1..":  "..col2.."  "..col3.."  "..col4.."  "..col5)
    end
end

--------------------------------------------------------------------------------
function clink.addcoroutine(coroutine, interval)
    if type(coroutine) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if interval ~= nil and type(interval) ~= "number" then
        error("bad argument #2 (number or nil expected)")
    end
    _coroutines[coroutine] = { coroutine=coroutine, interval=interval or 0, resumed=0, func=_coroutines_created[coroutine] }
    _coroutines_created[coroutine] = nil
    _coroutines_resumable = true
end

--------------------------------------------------------------------------------
function clink.removecoroutine(coroutine)
    if type(coroutine) == "thread" then
        _coroutines[coroutine] = nil
    elseif coroutine ~= nil then
        error("bad argument #1 (coroutine expected)")
    end
end

--------------------------------------------------------------------------------
--- -name:  io.popenyield
--- -arg:   command:string
--- -arg:   [mode:string]
--- -ret:   file
--- -show:  local file = io.popenyield("git status")
--- -show:
--- -show:  while (true) do
--- -show:  &nbsp; local line = file:read("*line")
--- -show:  &nbsp; if not line then
--- -show:  &nbsp;   break
--- -show:  &nbsp; end
--- -show:  &nbsp; do_things_with(line)
--- -show:  end
--- -show:  file:close()
--- Runs <code>command</code> and returns a read file handle for reading output
--- from the command.  However, it yields until the command has closed the read
--- file handle and the output is ready to be read without blocking.
---
--- It is the same as <code>io.popen</code> except that it only supports read
--- mode, and it yields until the command has finished.
---
--- The <span class="arg">mode</span> cannot contain <code>"w"</code>, but can
--- contain <code>"r"</code> (read mode) and/or either <code>"t"</code> for text
--- mode (the default if omitted) or <code>"b"</code> for binary mode.
---
--- <strong>Note:</strong> if the <code>prompt.async</code> setting is disabled
--- then this turns into a call to `io.popen` instead.
function io.popenyield(command, mode)
    -- This outer wrapper is implemented in Lua so that it can yield.
    if settings.get("prompt.async") then
-- TODO("COROUTINES: ideally avoid having lots of outstanding old commands still running; yield until earlier one(s) complete?")
        local file, yieldguard = io.popenyield_internal(command, mode)
        if file and yieldguard then
            _coroutine_infinite = coroutine.running()
            while not yieldguard:ready() do
                coroutine.yield()
            end
            _coroutine_infinite = nil
        end
        return file
    else
        return io.popen(command, mode)
    end
end

--------------------------------------------------------------------------------
local orig_coroutine_create = coroutine.create
function coroutine.create(func)
    -- Remember original func for diagnostic purposes later.  The table is
    -- cleared at the beginning of each input line.
    local thread = orig_coroutine_create(func)
    _coroutines_created[thread] = func
    return thread
end
