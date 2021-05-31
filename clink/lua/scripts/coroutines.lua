-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _coroutines = {}
local _after_coroutines = {}
local _coroutines_resumable = false
local _coroutine_infinite = nil

--------------------------------------------------------------------------------
local function clear_coroutines()
    _coroutines = {}
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
local function next_entry_target(entry)
    if not entry.lastclock then
        return 0
    else
        return entry.lastclock + entry.frequency
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
        for _,entry in pairs(_coroutines) do
            local this_target = next_entry_target(entry)
            if _coroutine_infinite == entry.coroutine then
                -- Yield until output is ready; don't influence the timeout.
            elseif not target or target > this_target then
                target = this_target
            end
        end
        if target then
            return target - os.clock()
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
                if next_entry_target(entry) < os.clock() then
                    if coroutine.resume(entry.coroutine, true--[[async]]) then
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
function clink.addcoroutine(coroutine, frequency)
    if type(coroutine) ~= "thread" then
        error("bad argument #1 (coroutine expected)")
    end
    if frequency ~= nil and type(frequency) ~= "number" then
        error("bad argument #2 (number or nil expected)")
    end
    _coroutines[coroutine] = { coroutine=coroutine, frequency=frequency or 0 }
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
function io.popenyield(command, mode)
    -- This outer wrapper is implemented in Lua so that it can yield.
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
end
