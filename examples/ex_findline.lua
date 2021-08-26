-- Searches upwards for a line containing "warn" or "error"
-- colored red or yellow.
function find_prev_colored_line(rl_buffer)
    local height = console.getheight()
    local cur_top = console.gettop()
    local offset = math.modf((height - 1) / 2) -- For vertically centering the found line.
    local start = cur_top + offset
    local found_index

    -- Only search if there's still room to scroll up.
    if start - offset > 1 then
        local match = console.findprevline(start - 1, "warn|error", "regex", {4,12,14}, "fore")
        if match ~= nil and match > 0 then
            found_index = match
        end
    end

    -- If scrolled up but no more matches, maintain the scroll position.
    if found_index == nil and cur_top <= console.getnumlines() - height then
        found_index = start
    end

    if found_index ~= nil then
        console.scroll("absolute", found_index - offset)
    else
        rl_buffer:ding()
    end
end

-- Searches downwards for a line containing "warn" or "error"
-- colored red or yellow.
function find_next_colored_line(rl_buffer)
    local height = console.getheight()
    local cur_top = console.gettop()
    local bottom = console.getnumlines()
    local offset = math.modf((height - 1) / 2) -- For vertically centering the found line.
    local start = cur_top + offset
    local found_index

    if cur_top > bottom - height then
        rl_buffer:ding()
        return
    end

    -- Only search if there's still room to scroll down.
    if start - offset + height - 1 < bottom then
        local match = console.findnextline(start + 1, "warn|error", "regex", {4,12,14}, "fore")
        if match ~= nil and match > 0 then
            found_index = match
        end
    end

    if found_index ~= nil then
        console.scroll("absolute", found_index - offset)
    else
        rl_buffer:ding()
    end
end
