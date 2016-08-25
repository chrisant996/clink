-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

clink._prompt_filters = {}

--------------------------------------------------------------------------------
function clink:promptfilter(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(self._prompt_filters, ret)
    return ret
end

--------------------------------------------------------------------------------
function clink:_filter_prompt_impl(prompt)
    for _, filter in ipairs(self._prompt_filters) do
        local filtered, onwards = filter:filter(prompt)
        if filtered ~= nil then
            if onwards == false then return filtered end
            prompt = filtered
        end
    end

    return prompt
end

--------------------------------------------------------------------------------
function clink:_filter_prompt(prompt)
    -- Sort by priority if required.
    if self._prompt_filters_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(self._prompt_filters, lambda)

        self._prompt_filters_unsorted = false
    end

    -- Protected call to prompt filters.
    local ok, ret = pcall(self._filter_prompt_impl, self, prompt)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return false
    end

    return ret
end
