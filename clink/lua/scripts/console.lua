-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
function console.screengrab(candidate_pattern, accept_pattern)
    local matches = {}

    local _end = console.gettop()
    local _start = _end + console.getheight() - 1

    if _start >= console.getnumlines() then
        _start = console.getnumlines() - 1
    end

    for i = _start,_end,-1 do
        local line = console.getlinetext(i)
        if line then
            -- Collect candidates from the line.
            local words = {}
            for word in line:gmatch(candidate_pattern) do
                -- Accept matching candidates.
                if word:match(accept_pattern) then
                    table.insert(words, word)
                end
            end

            -- Add the matches in reverse order so they're in proximity order.
            for j = #words,1,-1 do
                table.insert(matches, words[j])
            end
        end
    end

    return matches
end
