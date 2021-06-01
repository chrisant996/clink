-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _classifiers = {}
local _classifiers_unsorted = false

--------------------------------------------------------------------------------
-- This global variable tracks which generator function, if any, stopped the
-- most recent generate pass.  It's useful for diagnostic purposes; the file and
-- number can be retrieved by:
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
-- Receives a table of line_state_lua/lua_word_classifications pairs.
function clink._classify(commands)
    local impl = function ()
        clink.classifier_stopped = nil

        for _, classifier in ipairs(_classifiers) do
            local ret = classifier:classify(commands)
            if ret == true then
                -- Remember the classifier function that stopped.
                clink.classifier_stopped = classifier.classify
                return true
            end
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
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new word classifier object.  Define on the object a
--- <code>:classify()</code> function which gets called in increasing
--- <span class="arg">priority</span> order (low values to high values) when
--- classifying words for coloring the input.  See
--- <a href="#classifywords">Coloring The Input Text</a> for more information.
function clink.classifier(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_classifiers, ret)

    _classifiers_unsorted = true
    return ret
end
