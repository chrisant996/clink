-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink.generators = {}

--------------------------------------------------------------------------------
function clink.generate_matches(text, first, last, result)
    local line
    local cursor

    line, cursor, first, last = clink.adjust_for_separator(
        line_state.line,
        line_state.cursor,
        first,
        last
    )

    line_state.line = line
    line_state.cursor = cursor

    clink.match_display_filter = nil

    for _, generator in ipairs(clink.generators) do
        if generator.f(text, first, last, result) == true then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function clink.register_match_generator(func, priority)
    if priority == nil then
        priority = 999
    end

    table.insert(clink.generators, {f=func, p=priority})
    table.sort(clink.generators, function(a, b) return a["p"] < b["p"] end)
end
