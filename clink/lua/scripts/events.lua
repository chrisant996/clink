-- Copyright (c) 2020 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
clink._event_callbacks = clink._event_callbacks or {}

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it.
function clink._send_event(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        local _, func
        for _, func in ipairs(callbacks) do
            func(...)
        end
    end
end

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it, and if any
-- handler returns false then stop (returning nil does not stop).
function clink._send_event_cancelable(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        local _, func
        for _, func in ipairs(callbacks) do
            if func(...) == false then
                return
            end
        end
    end
end

--------------------------------------------------------------------------------
function clink._has_event_callbacks(event)
    local callbacks = clink._event_callbacks[event];
    if callbacks ~= nil then
        return #callbacks > 0
    end
end

--------------------------------------------------------------------------------
local function _add_event_callback(event, func)
    if type(func) ~= "function" then
        error(event.." requires a function", 2)
    end

    local callbacks = clink._event_callbacks[event]
    if callbacks == nil then
        callbacks = {}
        clink._event_callbacks[event] = callbacks
    end

    if callbacks[func] == nil then
        callbacks[func] = true -- This prevents duplicates.
        table.insert(callbacks, func)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.onbeginedit
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when Clink's edit
--- prompt is activated.
function clink.onbeginedit(func)
    _add_event_callback("onbeginedit", func)
end
