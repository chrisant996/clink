-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, classifier).

--------------------------------------------------------------------------------
clink = clink or {}
local _classifiers = {}
local _classifiers_unsorted = false

if settings.get("lua.debug") or clink.DEBUG then
    -- Make it possible to inspect these locals in the debugger.
    clink.debug = clink.debug or {}
    clink.debug._classifiers = _classifiers
end

--------------------------------------------------------------------------------
-- This global variable tracks which classifier function, if any, stopped the
-- most recent classify pass.  It's useful for diagnostic purposes; the file and
-- line number can be retrieved by:
--      local info = debug.getinfo(clink.classifier_stopped, 'S')
--      print("file: "..info.short_src)
--      print("line: "..info.linedefined)
clink.classifier_stopped = nil
local function classifier_onbeginedit()
    clink.classifier_stopped = nil
end
clink.onbeginedit(classifier_onbeginedit)


--------------------------------------------------------------------------------
local function prepare()
    -- Sort classifiers by priority if required.
    if _classifiers_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(_classifiers, lambda)

        _classifiers_unsorted = false
    end
end

--------------------------------------------------------------------------------
local elapsed_this_pass = 0
local force_diag_classifiers
local function log_cost(tick, classifier)
    local elapsed = (os.clock() - tick) * 1000
    local cost = classifier.cost
    if not cost then
        cost = { last=0, total=0, num=0, peak=0 }
        classifier.cost = cost
    end

    cost.last = elapsed
    cost.total = cost.total + elapsed
    cost.num = cost.num + 1
    if cost.peak < elapsed then
        cost.peak = elapsed
    end

    elapsed_this_pass = elapsed_this_pass + elapsed
end

--------------------------------------------------------------------------------
local function prepare_classify(commands, test)
    for _, command in ipairs(commands) do
        command.line_state:_reset_shift()
        command.classifications:_set_line_state(command.line_state, test)
    end
end

--------------------------------------------------------------------------------
-- Receives a table of line_state_lua/lua_word_classifications pairs.
function clink._classify(commands, test)
    local impl = function ()
        clink.classifier_stopped = nil
        elapsed_this_pass = 0

        for _, classifier in ipairs(_classifiers) do
            if classifier.classify then
                prepare_classify(commands, test)
                local tick = os.clock()
                local ret = classifier:classify(commands)
                log_cost(tick, classifier)
                if ret == true then
                    -- Remember the classifier function that stopped.
                    clink.classifier_stopped = classifier.classify
                    return true
                end
            end
        end

        if elapsed_this_pass > 0.010 then
            force_diag_classifiers = true
        end

        return false
    end

    prepare()

    local ok, ret = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("word classifier failed:")
        print(ret)
        return
    end

    return ret or false
end

--------------------------------------------------------------------------------
--- -name:  clink.classifier
--- -ver:   1.1.49
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new word classifier object.  Define on the object a
--- <code>:classify()</code> function which gets called in increasing
--- <span class="arg">priority</span> order (low values to high values) when
--- classifying words for coloring the input.  See
--- <a href="#classifywords">Coloring the Input Text</a> for more information.
function clink.classifier(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_classifiers, ret)

    _classifiers_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
local function pad_string(s, len)
    if #s < len then
        s = s..string.rep(" ", len - #s)
    end
    return s
end

--------------------------------------------------------------------------------
function clink._diag_classifiers(arg)
    arg = (arg and arg >= 2)
    if not arg and not settings.get("lua.debug") and not force_diag_classifiers then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local header = "\x1b[36m"       -- Cyan.
    local norm = "\x1b[m"           -- Normal.

    local any_cost
    local t = {}
    local longest = 24
    for _,classifier in ipairs (_classifiers) do
        if classifier.classify then
            local info = debug.getinfo(classifier.classify, 'S')
            if not clink._is_internal_script(info.short_src) then
                local src = info.short_src..":"..info.linedefined
                table.insert(t, { src=src, cost=classifier.cost })
                if longest < #src then
                    longest = #src
                end
                if not any_cost and classifier.cost then
                    any_cost = true
                end
            end
        end
    end

    if t[1] then
        if any_cost then
            clink.print(string.format("%s%s%s     %slast    avg     peak%s",
                    bold, pad_string("classifiers:", longest + 2), norm,
                    header, norm))
        else
            clink.print(bold.."classifiers:"..norm)
        end
        for _,entry in ipairs (t) do
            if entry.cost then
                clink.print(string.format("  %s  %4u ms %4u ms %4u ms",
                        pad_string(entry.src, longest),
                        entry.cost.last, entry.cost.total / entry.cost.num, entry.cost.peak))
            else
                clink.print(string.format("  %s", entry.src))
            end
        end
    end
end
