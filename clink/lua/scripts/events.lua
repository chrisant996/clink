-- Copyright (c) 2020 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
clink._event_callbacks = clink._event_callbacks or {}

--------------------------------------------------------------------------------
local bold = "\x1b[1m"                  -- Bold (bright).
local header = "\x1b[36m"               -- Cyan.
local norm = "\x1b[m"                   -- Normal.

--------------------------------------------------------------------------------
local function log_cost(tick, c)
    local elapsed = (os.clock() - tick) * 1000
    local cost = c.cost
    if not cost then
        cost = { last=0, total=0, num=0, peak=0 }
        c.cost = cost
    end

    cost.last = elapsed
    cost.total = cost.total + elapsed
    cost.num = cost.num + 1
    if cost.peak < elapsed then
        cost.peak = elapsed
    end
end

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it.
function clink._send_event(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        for _, c in ipairs(callbacks) do
            if c.func then
                local tick = os.clock()
                c.func(...)
                log_cost(tick, c)
            end
        end
    end
end

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it.  If any
-- handler returns a string then stop.
function clink._send_event_string_out(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        for _, c in ipairs(callbacks) do
            if c.func then
                local tick = os.clock()
                local s = c.func(...)
                log_cost(tick, c)
                if type(s) == "string" then
                    return s
                end
            end
        end
    end
    return nil
end

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it, and if any
-- handler returns false then stop (returning nil does not stop).
function clink._send_event_cancelable(event, ...)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        for _, c in ipairs(callbacks) do
            if c.func then
                local tick = os.clock()
                local cancel = (c.func(...) == false)
                log_cost(tick, c)
                if cancel then
                    return
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- Sends a named event to all registered callback handlers for it and pass the
-- provided string argument.  The first return value replaces string, unless
-- nil.  If any handler returns false as the second return value then stop
-- (returning nil does not stop).
function clink._send_event_cancelable_string_inout(event, string)
    local callbacks = clink._event_callbacks[event]
    if callbacks ~= nil then
        for _, c in ipairs(callbacks) do
            if c.func then
                local tick = os.clock()
                local s,continue = c.func(string)
                log_cost(tick, c)
                if s then
                    string = s
                end
                if continue == false then
                    break
                end
            end
        end
        return string;
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
        table.insert(callbacks, { func=func })
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.oninject
--- -ver:   1.1.21
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when Clink is injected
--- into a CMD process.  The function is called only once per session.
function clink.oninject(func)
    _add_event_callback("oninject", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.onbeginedit
--- -ver:   1.1.11
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when Clink's edit
--- prompt is activated.  The function receives no arguments and has no return
--- values.
---
--- Starting in v1.3.18 <span class="arg">func</span> may optionally return a
--- string.  If a string is returned, it is executed as a command line without
--- showing a prompt and without invoking the input line editor.
---
--- <strong>Note:</strong>  Be very careful if you return a string; this has the
--- potential to interfere with the user's ability to use CMD.  Mistakes in the
--- command string can have the potential to cause damage to the system very
--- quickly.  It is also possible for a script to cause an infinite loop, and
--- therefore <kbd>Ctrl</kbd>-<kbd>Break</kbd> causes the next string to be
--- ignored.
function clink.onbeginedit(func)
    _add_event_callback("onbeginedit", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.onprovideline
--- -ver:   1.3.18
--- -arg:   func:function
---
--- Registers <span class="arg">func</span> to be called after the
--- <a href="#clink.onbeginedit">onbeginedit</a> event but before the input line
--- editor starts.  If <span class="arg">func</span> returns a string, it is
--- executed as a command line without showing a prompt.  The input line editor
--- is skipped, and the <a href="#clink.onendedit">onendedit</a> and
--- <a href="#clink.onfilterinput">onfilterinput</a> events happen immediately.
---
--- <strong>Note:</strong>  Be very careful when returning a string; this can
--- interfere with the user's ability to use CMD.  Mistakes in the command
--- string can have potential to cause damage to the system very quickly.  It is
--- also possible for a script to cause an infinite loop, and therefore
--- <kbd>Ctrl</kbd>-<kbd>Break</kbd> skips the next
--- <a href="#clink.onprovideline">onprovideline</a> event, allowing the user
--- to regain control.
function clink.onprovideline(func)
    _add_event_callback("onprovideline", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.onendedit
--- -ver:   1.1.20
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when Clink's edit
--- prompt ends.  The function receives a string argument containing the input
--- text from the edit prompt.
---
--- <strong>Breaking Change in v1.2.16:</strong>  The ability to replace the
--- user's input has been moved to a separate
--- <a href="#clink.onfilterinput">onfilterinput</a> event.
function clink.onendedit(func)
    _add_event_callback("onendedit", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.onfilterinput
--- -ver:   1.2.16
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called after Clink's edit
--- prompt ends (it is called after the <a href="#clink.onendedit">onendedit</a>
--- event).  The function receives a string argument containing the input text
--- from the edit prompt.  The function returns up to two values.  If the first
--- is not nil then it's a string that replaces the edit prompt text.  If the
--- second is not nil and is false then it stops further onfilterinput handlers
--- from running.
---
--- Starting in v1.3.13 <span class="arg">func</span> may return a table of
--- strings, and each is executed as a command line.
---
--- <strong>Note:</strong>  Be very careful if you replace the text; this has
--- the potential to interfere with or even ruin the user's ability to enter
--- command lines for CMD to process.
function clink.onfilterinput(func)
    _add_event_callback("onfilterinput", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.oncommand
--- -ver:   1.3.12
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when the command word
--- changes in the edit line.
---
--- The function receives 2 arguments:  the <a href="#line_state">line_state</a>
--- for the command, and a table with the following scheme:
--- -show:  {
--- -show:  &nbsp;   command =   -- [string] The command.
--- -show:  &nbsp;   quoted  =   -- [boolean] Whether the command is quoted in the command line.
--- -show:  &nbsp;   type    =   -- [string] "unrecognized", "executable", or "command" (a CMD command name).
--- -show:  &nbsp;   file    =   -- [string] The file that would be executed, or an empty string.
--- -show:  }
---
--- The function has no return values.
function clink.oncommand(func)
    _add_event_callback("oncommand", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.ondisplaymatches
--- -ver:   1.1.12
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called when Clink is about to
--- display matches.  See <a href="#filteringthematchdisplay">Filtering the
--- Match Display</a> for more information.
--- -show:  local function my_filter(matches, popup)
--- -show:  &nbsp;   local new_matches = {}
--- -show:  &nbsp;   for _,m in ipairs(matches) do
--- -show:  &nbsp;       if m.match:find("[0-9]") then
--- -show:  &nbsp;           -- Ignore matches with one or more digits.
--- -show:  &nbsp;       else
--- -show:  &nbsp;           -- Keep the match, and also add * prefix to directory matches.
--- -show:  &nbsp;           if m.type:find("^dir") then
--- -show:  &nbsp;               m.display = "*"..m.match
--- -show:  &nbsp;           end
--- -show:  &nbsp;           table.insert(new_matches, m)
--- -show:  &nbsp;       end
--- -show:  &nbsp;   end
--- -show:  &nbsp;   return new_matches
--- -show:  end
--- -show:
--- -show:  function my_match_generator:generate(line_state, match_builder)
--- -show:  &nbsp;   ...
--- -show:  &nbsp;   clink.ondisplaymatches(my_filter)
--- -show:  end
function clink.ondisplaymatches(func)
    -- For now, only one handler at a time.  I wanted it to be a chain of
    -- handlers, but that implies the output from one handler will be input to
    -- the next.  It got messy trying to keep it simple and flexible without
    -- creating stability loopholes.  That's why this not-really-an-event is
    -- wedged in amongst the real events.
    clink._event_callbacks["ondisplaymatches"] = {}
    _add_event_callback("ondisplaymatches", func)
end

--------------------------------------------------------------------------------
function clink._send_ondisplaymatches_event(matches, popup)
    local callbacks = clink._event_callbacks["ondisplaymatches"]
    if callbacks ~= nil then
        local c = callbacks[1]
        if c and c.func then
            local tick = os.clock()
            local ret = c.func(matches, popup)
            log_cost(tick, c)
            return ret
        end
    end
    return matches
end

--------------------------------------------------------------------------------
--- -name:  clink.onfiltermatches
--- -ver:   1.1.41
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called after Clink generates
--- matches for completion.  See <a href="#filteringmatchcompletions">
--- Filtering Match Completions</a> for more information.
function clink.onfiltermatches(func)
    _add_event_callback("onfiltermatches", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.oninputlinechanged
--- -ver:   1.4.18
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called after an editing
--- command (key binding) makes changes in the input line.
---
--- The function receives one argument, a <span class="arg">line</span> string
--- which contains the new contents of the input line.
---
--- The function has no return values.
---
--- Here is a script that demonstrates a lighthearted example of how this could
--- be used.  Any time the input line contains "marco" it replaces the prompt
--- with "POLO!".
--- -show:  local has_marco
--- -show:  local polo = clink.promptfilter(-1)
--- -show:
--- -show:  function polo:filter()
--- -show:  &nbsp;   if has_marco then
--- -show:  &nbsp;       return "\x1b[44;96mPOLO!\x1b[m ", false
--- -show:  &nbsp;   end
--- -show:  end
--- -show:
--- -show:  local function onbeginedit()
--- -show:  &nbsp;   has_marco = nil
--- -show:  end
--- -show:
--- -show:  local function oninputlinechanged(line)
--- -show:  &nbsp;   local new_marco = line:find("marco") and true
--- -show:  &nbsp;   if has_marco ~= new_marco then
--- -show:  &nbsp;       has_marco = new_marco
--- -show:  &nbsp;       clink.refilterprompt()
--- -show:  &nbsp;   end
--- -show:  end
--- -show:
--- -show:  clink.onbeginedit(onbeginedit)
--- -show:  clink.oninputlinechanged(oninputlinechanged)
--- -show:
--- -show:  function clink.oninputlinechanged(func)
--- -show:  &nbsp;   _add_event_callback("oninputlinechanged", func)
--- -show:  end
function clink.oninputlinechanged(func)
    _add_event_callback("oninputlinechanged", func)
end

--------------------------------------------------------------------------------
--- -name:  clink.onaftercommand
--- -ver:   1.2.50
--- -arg:   func:function
--- Registers <span class="arg">func</span> to be called after every editing
--- command (key binding).
function clink.onaftercommand(func)
    _add_event_callback("onaftercommand", func)
end

--------------------------------------------------------------------------------
function clink._send_onfiltermatches_event(matches, completion_type, filename_completion_desired)
    local ret = nil
    local callbacks = clink._event_callbacks["onfiltermatches"]
    if callbacks ~= nil then
        for _, c in ipairs(callbacks) do
            if c and c.func then
                local tick = os.clock()
                local m = c.func(matches, completion_type, filename_completion_desired)
                log_cost(tick, c)
                if m ~= nil then
                    matches = m
                    ret = matches
                end
            end
        end
    end
    return ret
end

--------------------------------------------------------------------------------
function clink._set_coroutine_events(new_events)
    local old_events = {}
    new_events = new_events or {}

    old_events.match_display_filter = clink.match_display_filter
    old_events.ondisplaymatches = clink._event_callbacks["ondisplaymatches"]
    old_events.onfiltermatches = clink._event_callbacks["onfiltermatches"]

    clink.match_display_filter = new_events.match_display_filter
    clink._event_callbacks["ondisplaymatches"] = new_events.ondisplaymatches
    clink._event_callbacks["onfiltermatches"] = new_events.onfiltermatches

    return old_events
end

--------------------------------------------------------------------------------
local function pad_string(s, len)
    if #s < len then
        s = s..string.rep(" ", len - #s)
    end
    return s
end

--------------------------------------------------------------------------------
local function max_len(a, b)
    a = a or b or 0
    b = b or a or 0
    return a > b and a or b
end

--------------------------------------------------------------------------------
local function collect_event_src(t, event)
    local callbacks = clink._event_callbacks[event]
    if not callbacks[1] then
        return
    end

    local tsub = {}
    t[event] = tsub

    local any_cost
    local longest = 24
    for _,c in ipairs(callbacks) do
        if c.func then
            local info = debug.getinfo(c.func, 'S')
            if not clink._is_internal_script(info.short_src) then
                local src = info.short_src..":"..info.linedefined
                local entry = { src=src, cost=c.cost }
                table.insert(tsub, entry)
                if longest < #src then
                    longest = #src
                end
                if not any_cost and entry.cost then
                    any_cost = true
                end
            end
        end
    end
    tsub.any_cost = any_cost
    t.longest = max_len(t.longest, longest)
end

--------------------------------------------------------------------------------
local function print_event_src(t, event)
    local tsub = t[event]

    if tsub[1] then
        local longest = t.longest
        if tsub.any_cost then
            clink.print(string.format("  %s           %slast    avg     peak%s",
                    pad_string(event..":", longest), header, norm))
        else
            clink.print("  "..event..":")
        end
        for _,entry in ipairs(tsub) do
            if entry.cost then
                clink.print("", string.format("%s  %4u ms %4u ms %4u ms",
                        pad_string(entry.src, longest),
                        entry.cost.last, entry.cost.total / entry.cost.num, entry.cost.peak))
            else
                clink.print("", string.format("%s", entry.src))
            end
        end
        return true
    end
end

--------------------------------------------------------------------------------
function clink._diag_events(arg)
    arg = (arg and arg >= 1)
    if not arg and not settings.get("lua.debug") then
        return
    end

    local sorted_events = {}
    for event_name in pairs(clink._event_callbacks) do
        table.insert(sorted_events, event_name)
    end
    table.sort(sorted_events, function(a, b) return a < b end)

    local t = {}
    for _,event_name in ipairs(sorted_events) do
        collect_event_src(t, event_name)
    end

    if t.longest then
        clink.print(bold.."events:"..norm)
        for _,event_name in ipairs(sorted_events) do
            print_event_src(t, event_name)
        end
    else
        print("  no event callbacks registered")
    end
end
