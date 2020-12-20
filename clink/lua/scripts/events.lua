-- Copyright (c) 2020 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
clink._event_callbacks = clink._event_callbacks or {}

--------------------------------------------------------------------------------
function clink._send_event(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        for _, func in ipairs(callbacks) do
            func(...)
        end
    end
end

--------------------------------------------------------------------------------
local function _add_event_callback(event, func)
    if type(func) ~= "function" then
        error(event.." requires a function", 2)
    end

    local callbacks = clink._event_callbacks["onbeginedit"]
    if callbacks == nil then
        clink._event_callbacks["onbeginedit"] = {}
        callbacks = clink._event_callbacks["onbeginedit"]
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
