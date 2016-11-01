-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

clink._generators = {}

--------------------------------------------------------------------------------
function clink:generator(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(self._generators, ret)

    self._generators_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
function clink:_generate_impl(line_state, match_builder)
    for _, generator in ipairs(self._generators) do
        local ret = generator:generate(line_state, match_builder)
        if ret == true then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function clink:_generate(line_state, match_builder)
    -- Sort generators by priority if required.
    if self._generators_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(self._generators, lambda)

        self._generators_unsorted = false
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
