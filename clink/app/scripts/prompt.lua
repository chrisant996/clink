-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local prompt_filters = {}
local prompt_filters_unsorted = false



--------------------------------------------------------------------------------
function clink._filter_prompt(prompt)
    -- Sort by priority if required.
    if prompt_filters_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(prompt_filters, lambda)

        prompt_filters_unsorted = false
    end

    -- Protected call to prompt filters.
    local impl = function(prompt)
        for _, filter in ipairs(prompt_filters) do
            local filtered, onwards = filter:filter(prompt)
            if filtered ~= nil then
                if onwards == false then return filtered end
                prompt = filtered
            end
        end

        return prompt
    end

    local ok, ret = pcall(impl, prompt)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return false
    end

    return ret
end

--------------------------------------------------------------------------------
--- -name:  clink.promptfilter
--- -arg:   [priority:integer]
--- -ret:   table
function clink.promptfilter(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(prompt_filters, ret)

    prompt_filters_unsorted = true
    return ret
end
