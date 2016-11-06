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
local function pcall_dispatch(func, ...)
    local ok, ret = pcall(func, ...)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return
    end

    return ret
end

--------------------------------------------------------------------------------
function clink:_prepare()
    -- Sort generators by priority if required.
    if self._generators_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(self._generators, lambda)

        self._generators_unsorted = false
    end
end

--------------------------------------------------------------------------------
function clink:_generate(line_state, match_builder)
    local impl = function ()
        for _, generator in ipairs(self._generators) do
            local ret = generator:generate(line_state, match_builder)
            if ret == true then
                return true
            end
        end

        return false
    end

    clink:_prepare()
    local ret = pcall_dispatch(impl)
    return ret or false
end

--------------------------------------------------------------------------------
function clink:_get_prefix_length(word)
    local impl = function ()
        local ret = 0
        for _, generator in ipairs(self._generators) do
            if generator.getprefixlength then
                local i = generator:getprefixlength(word) or 0
                if i > ret then ret = i end
            end
        end

        return ret
    end

    clink:_prepare()
    local ret = pcall_dispatch(impl)
    return ret or 0
end
