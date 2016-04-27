-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local generators = {}

--------------------------------------------------------------------------------
local function generate_matches_impl(line_state, match_builder)
    -- clink.match_display_filter = nil MODE4

    for _, generator in ipairs(generators) do
        local ret = generator.f(line_state, match_builder)
        if ret == true then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function clink.generate_matches(line_state, match_builder)
    local ok, ret = pcall(generate_matches_impl, line_state, match_builder)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return false
    end

    return ret
end

--------------------------------------------------------------------------------
function clink.register_match_generator(func, priority)
    if priority == nil then
        priority = 999
    end

    table.insert(generators, {f=func, p=priority})
    table.sort(generators, function(a, b) return a["p"] < b["p"] end)
end
