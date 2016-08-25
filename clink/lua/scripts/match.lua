-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

clink._modules = {}

--------------------------------------------------------------------------------
function clink:module(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(self._modules, ret)

    self._modules_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
function clink:_generate_impl(line_state, match_builder)
    for _, module in ipairs(self._modules) do
        local ret = module:generate(line_state, match_builder)
        if ret == true then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function clink:_generate(line_state, match_builder)
    -- Sort modules by priority if required.
    if self._modules_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(self._modules, lambda)

        self._modules_unsorted = false
    end

    -- Protected call to generate matches.
    local ok, ret = pcall(self._generate_impl, self, line_state, match_builder)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return false
    end

    return ret
end
