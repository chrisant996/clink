-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function register_filter(filter, priority)
    if priority == nil then
        priority = 999
    end

    table.insert(prompt.filters, {func=filter, prio=priority})
    table.sort(prompt.filters, function(a, b) return a.prio < b.prio end)
end

--------------------------------------------------------------------------------
local function filter_prompt(the_prompt)
    for _, filter in ipairs(prompt.filters) do
        local filtered, onwards = filter.func(the_prompt)
        if filtered ~= nil then
            if onwards == false then return filtered end
            the_prompt = filtered
        end
    end

    return the_prompt
end

--------------------------------------------------------------------------------
prompt = {
    filters         = {},
    register_filter = register_filter,
    filter          = filter_prompt,
}
