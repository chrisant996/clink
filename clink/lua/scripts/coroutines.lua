-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _coroutines = {}
local _after_coroutines = {}
local _coroutines_resumable = false

--------------------------------------------------------------------------------
local function clear_coroutines()
    _coroutines = {}
    _after_coroutines = {}
    _coroutines_resumable = false
end
clink.onbeginedit(clear_coroutines)

--------------------------------------------------------------------------------
local function after_coroutines()
    for _,func in pairs(_after_coroutines) do
        func()
    end
end

--------------------------------------------------------------------------------
function clink._after_coroutines(func)
    _after_coroutines[func] = func
end

--------------------------------------------------------------------------------
function clink._has_coroutines()
    return _coroutines_resumable
end

--------------------------------------------------------------------------------
function clink._resume_coroutines()
    if _coroutines_resumable then
        _coroutines_resumable = false
        for _,c in pairs(_coroutines) do
            local alive = coroutine.resume(c, true--[[async]])
            if not _coroutines_resumable and alive and coroutine.status(c) ~= "dead" then
                _coroutines_resumable = true
            end
        end
        after_coroutines()
    end
end

--------------------------------------------------------------------------------
function clink.addcoroutine(coroutine)
    _coroutines[coroutine] = coroutine
    _coroutines_resumable = true
end

--------------------------------------------------------------------------------
function clink.removecoroutine(coroutine)
    _coroutines[coroutine] = nil
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
-- TODO("COROUTINES: need to plumb an indication how long to wait, and on what event to wait.")
        while not yieldguard:ready() do
            coroutine.yield()
        end
    end
    return file
end
