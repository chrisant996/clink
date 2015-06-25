--
-- Copyright (c) 2015 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

--------------------------------------------------------------------------------
clink.matches = {}
clink.generators = {}

--------------------------------------------------------------------------------
function clink.is_single_match(matches)
    if #matches <= 1 then
        return true
    end

    local first = matches[1]:lower()
    for i = 2, #matches, 1 do
        if first ~= matches[i]:lower() then
            return false
        end
    end

    return true
end

--------------------------------------------------------------------------------
function clink.compute_lcd(text, list)
    local list_n = #list
    if list_n < 2 then
        return
    end

    -- Find min and max limits
    local max = 100000
    for i = 1, #list, 1 do
        local j = #(list[i])
        if max > j then
            max = j
        end
    end

    -- For each character in the search range...
    local mid = #text
    local lcd = ""
    for i = 1, max, 1 do
        local same = true
        local l = list[1]:sub(i, i)
        local m = l:lower()

        -- Compare character at the index with each other character in the
        -- other matches.
        for j = 2, list_n, 1 do
            local n = list[j]:sub(i, i):lower()
            if m ~= n then
                same = false
                break
            end
        end

        -- If all characters match then use first match's character.
        if same then
            lcd = lcd..l
        else
            -- Otherwise use what the user's typed or if we're past that then
            -- bail out.
            if i <= mid then
                lcd = lcd..text:sub(i, i)
            else
                break
            end
        end
    end

    return lcd
end

--------------------------------------------------------------------------------
function clink.match_words(text, words)
    local count = clink.match_count()

    for _, i in ipairs(words) do
        if clink.is_match(text, i) then
            clink.add_match(i)
        end
    end

    return clink.match_count() - count
end

--------------------------------------------------------------------------------
function clink.match_files(pattern, full_path, find_func)
    -- Fill out default values
    if type(find_func) ~= "function" then
        find_func = clink.find_files
    end

    if full_path == nil then
        full_path = true
    end

    if pattern == nil then
        pattern = "*"
    end

    -- Glob files.
    pattern = pattern:gsub("/", "\\")
    local glob = find_func(pattern, true)

    -- Get glob's base.
    local base = ""
    local i = pattern:find("[\\:][^\\:]*$")
    if i and full_path then
        base = pattern:sub(1, i)
    end

    -- Match them.
    local count = clink.match_count()

    for _, i in ipairs(glob) do
        local full = base..i
        clink.add_match(full)
    end

    return clink.match_count() - count
end

--------------------------------------------------------------------------------
function clink.match_count()
    return #clink.matches
end

--------------------------------------------------------------------------------
function clink.add_match(match)
    if type(match) == "table" then
        for _, i in ipairs(match) do
            table.insert(clink.matches, i)
        end

        return
    end

    table.insert(clink.matches, match)
end

--------------------------------------------------------------------------------
function clink.set_match(i, value)
    clink.matches[i] = value
end

--------------------------------------------------------------------------------
function clink.get_match(i)
    return clink.matches[i]
end

--------------------------------------------------------------------------------
function clink.is_match(needle, candidate)
    if needle == nil then
        error("Nil needle value when calling clink.is_match()", 2)
    end

    if clink.lower(candidate:sub(1, #needle)) == clink.lower(needle) then
        return true
    end
    return false
end

--------------------------------------------------------------------------------
function clink.generate_matches(text, first, last)
    local line_buffer
    local point

    line_buffer, point, first, last = clink.adjust_for_separator(
        rl_state.line_buffer,
        rl_state.point,
        first,
        last
    )

    rl_state.line_buffer = line_buffer
    rl_state.point = point

    clink.matches = {}
    clink.match_display_filter = nil

    for _, generator in ipairs(clink.generators) do
        if generator.f(text, first, last) == true then
            if #clink.matches > 1 then
                -- Catch instances where there's many entries of a single match
                if clink.is_single_match(clink.matches) then
                    clink.matches = { clink.matches[1] }
                    return true;
                end

                -- First entry in the match list should be the user's input,
                -- modified here to be the lowest common denominator.
                local lcd = clink.compute_lcd(text, clink.matches)
                table.insert(clink.matches, 1, lcd)
            end

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
