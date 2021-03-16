-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
--- -name:  console.screengrab
--- -arg:   candidate_pattern:string
--- -arg:   accept_pattern:string
--- -ret:   table
--- -show:  local matches = console.screengrab(
--- -show:  &nbsp;       "[^%w]*(%w%w[%w]+)",   -- Words with 3 or more letters are candidates.
--- -show:  &nbsp;       "^%x+$")               -- A candidate containing only hexadecimal digits is a match.
--- Uses the provided Lua string patterns to collect text from the current
--- console screen and returns a table of matching text snippets.  The snippets
--- are ordered by distance from the input line.
---
--- For example <span class="arg">candidate_pattern</span> could specify a
--- pattern that identifies words, and <span class="arg">accept_pattern</span>
--- could specify a pattern that matches words composed of hexadecimal digits.
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
