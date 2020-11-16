-- Copyright (c) 2020 Christopher Antos
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
-- This is the error handler used by native code calls into Lua scripts.
function _error_handler(message)
    if _can_pause and settings.get("lua.break_on_error") then
        pause("break on error:")
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
        pause("break on error:")
    end
    return debug.traceback(message, 2)
end
