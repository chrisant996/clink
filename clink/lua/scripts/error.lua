-- Copyright (c) 2023 Christopher Antos
-- License: http://opensource.org/licenses/MIT

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
local function resolve_error_message(message)
    if message ~= nil then
        message = tostring(message)
    end
    return message or "(no error message)"
end

--------------------------------------------------------------------------------
-- This is the error handler used by native code calls into Lua scripts.
function _error_handler(message)
    message = resolve_error_message(message)
    if _can_pause and clink._is_break_on_error() then
        pause("break on error: "..message)
    end
    if settings.get("lua.traceback_on_error") then
        print(debug.traceback(message, 3))
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
    message = resolve_error_message(message)
    if _can_pause and clink._is_break_on_error() then
        pause("break on error: "..message)
    end
    return debug.traceback(message, 3)
end

--------------------------------------------------------------------------------
-- This is the error handler used for reporting coroutine errors.
function _co_error_handler(co, message)
    message = resolve_error_message(message)
    if settings.get("lua.traceback_on_error") or
            (_can_pause and clink._is_break_on_error()) then
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
