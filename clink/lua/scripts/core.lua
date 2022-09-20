-- Copyright (c) 2020 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- Used by `clink.print()` to suppress the usual trailing newline.  The table
-- address is unique, thus `clink.print()` can test for equality.
NONL = {}

--------------------------------------------------------------------------------
-- Provide a stub implementation of pause() if the debugger isn't loaded.
local _can_pause = true
if pause == nil then
    _can_pause = false
    pause = function (message)
        if message then
            print(message)
        end
        print("can't pause; debugger not loaded.")
    end
end

--------------------------------------------------------------------------------
-- This returns the file and line for the top stack frame, starting at start.
local function _get_top_frame(start, max)
    if not start or type(start) ~= "number" or start < 1 then
        start = 1
    end
    if not max or type(max) ~= "number" or max < 1 then
        max = 26
    else
        max = max + 1
    end

    local file, line
    for f = start, max, 1 do
        local t = debug.getinfo(f, "Sl")
        if not t then
            if file and line then
                return file, line
            end
            return
        end
        file = t.short_src
        line = t.currentline
    end
end

--------------------------------------------------------------------------------
-- This returns the file and line number for the top stack frame, starting at
-- start_frame (or the next closest stack frame for which a file and line number
-- are available).
local function _get_maybe_fileline(start_frame)
    local file, line = _get_top_frame(start_frame)
    if file and line then
        return " in "..file..":"..line
    end
    return ""
end



--------------------------------------------------------------------------------
-- This is the error handler used by native code calls into Lua scripts.
function _error_handler(message)
    if _can_pause and settings.get("lua.break_on_error") then
        pause("break on error: "..message)
    end
    if settings.get("lua.traceback_on_error") then
        print(debug.traceback(message, 2))
    else
        print(message)
    end
end

--------------------------------------------------------------------------------
-- This error handler function is for use by Lua scripts making protected calls.
-- If lua.break_on_error is set it activates the debugger. It always returns the
-- error message, which is then returned by pcall().  This doesn't handle
-- lua.traceback_on_error because the caller of pcall() is already expecting an
-- error message to be returned and may print it.  I'd rather allow scripts to
-- suppress error messages than force error messages to show up twice.
function _error_handler_ret(message)
    if _can_pause and settings.get("lua.break_on_error") then
        pause("break on error: "..message)
    end
    return debug.traceback(message, 2)
end

--------------------------------------------------------------------------------
-- This is the error handler used for reporting coroutine errors.
function _co_error_handler(co, message)
    if settings.get("lua.traceback_on_error") or
            (_can_pause and settings.get("lua.break_on_error")) then
        local m = debug.traceback(co, message)
        print(m)
        log.info(m)
    else
        print(message)
        if settings.get("lua.debug") then
            log.info(debug.traceback(co, message))
        end
    end
end



--------------------------------------------------------------------------------
-- This reports a compatibility message.
local remind_how_to_disable = true
function _compat_warning(message, suffix)
    local where = _get_maybe_fileline(2) -- 2 gets the caller's file and line.
    message = message or ""
    suffix = suffix or ""

    local compat = os.getenv("CLINK_COMPAT_WARNINGS")
    compat = compat and tonumber(compat) or 1

    log.info(debug.traceback(message..suffix, 2)) -- 2 omits this function.

    if compat == 0 then
        return
    end

    if remind_how_to_disable then
        remind_how_to_disable = false
        print("Compatibility warnings will be hidden if %CLINK_COMPAT_WARNINGS% == 0.")
        print("Consider updating the Lua scripts; otherwise functionality may be impaired.")
    end
    print(message..where.." (see log file for details).")
end



--------------------------------------------------------------------------------
--- -name:  clink.quote_split
--- -deprecated:
--- -arg:   str:string
--- -arg:   ql:string
--- -arg:   qr:string
--- -ret:   table
function clink.quote_split(str, ql, qr) -- luacheck: no unused
    _compat_warning("clink.quote_split() is not supported.")
    return {}
end



--------------------------------------------------------------------------------
function os.globdirs(pattern, extrainfo)
    local c, ismain = coroutine.running()
    if ismain then
        -- Use a fully native implementation for higher performance.
        return os._globdirs(pattern, extrainfo)
    elseif clink._is_coroutine_canceled(c) then
        return {}
    else
        -- Yield periodically.
        local t = {}
        local g = os._makedirglobber(pattern, extrainfo)
        while g:next(t) do
            coroutine.yield()
            if clink._is_coroutine_canceled(c) then
                t = {}
                break
            end
        end
        g:close()
        return t
    end
end

--------------------------------------------------------------------------------
function os.globfiles(pattern, extrainfo)
    local c, ismain = coroutine.running()
    if ismain then
        -- Use a fully native implementation for higher performance.
        return os._globfiles(pattern, extrainfo)
    elseif clink._is_coroutine_canceled(c) then
        return {}
    else
        -- Yield periodically.
        local t = {}
        local g = os._makefileglobber(pattern, extrainfo)
        while g:next(t) do
            coroutine.yield()
            if clink._is_coroutine_canceled(c) then
                t = {}
                break
            end
        end
        g:close()
        return t
    end
end
