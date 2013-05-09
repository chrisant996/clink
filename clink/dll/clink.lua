--
-- Copyright (c) 2012 Martin Ridgers
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

clink.prompt = {}
clink.prompt.filters = {}

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
function clink.is_point_in_quote(str, i)
    if i > #str then
        i = #str
    end

    local c = 1
    local q = string.byte("\"")
    for j = 1, i do
        if string.byte(str, j) == q then
            c = c * -1
        end
    end

    if c < 0 then
        return true
    end

    return false
end

--------------------------------------------------------------------------------
function clink.adjust_for_separator(buffer, first, last)
    local seps = nil
    if clink.get_host_process() ~= "cmd.exe" then
        seps = "|&"
    end

    if seps then
        -- Find any valid command separators and if found, manipulate the
        -- completion state a little bit.
        local leading = buffer:sub(1, first - 1)

        -- regex is: <sep> <whitespace> <not_seps> <eol>
        local regex = "["..seps.."]%s*([^"..seps.."]*)$"
        local sep_found, _, post_sep = leading:find(regex)

        if sep_found and not clink.is_point_in_quote(leading, sep_found) then
            local delta = #leading - #post_sep
            buffer = buffer:sub(delta + 1)
            first = first - delta
            last = last - delta
        end
    end

    return buffer, first, last
end

--------------------------------------------------------------------------------
function clink.generate_matches(text, first, last)
    rl_line_buffer, first, last = clink.adjust_for_separator(
        rl_line_buffer,
        first,
        last
    )

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
function clink.register_match_generator(func, priority)
    if priority == nil then
        priority = 999
    end

    table.insert(clink.generators, {f=func, p=priority})
    table.sort(clink.generators, function(a, b) return a["p"] < b["p"] end)
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
function clink.match_count()
    return #clink.matches
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
function clink.split(str, sep)
    local i = 1
    local ret = {}
    for _, j in function() return str:find(sep, i, true) end do
        table.insert(ret, str:sub(i, j - 1))
        i = j + 1
    end
    table.insert(ret, str:sub(i, j))

    return ret
end

--------------------------------------------------------------------------------
function clink.quote_split(str, ql, qr)
    if not qr then
        qr = ql
    end

    -- First parse in "pre[ql]quote_string[qr]" chunks
    local insert = table.insert
    local i = 1
    local needle = "%b"..ql..qr
    local parts = {}
    for l, r, quote in function() return str:find(needle, i) end do
        -- "pre"
        if l > 1 then
            insert(parts, str:sub(i, l - 1))
        end

        -- "quote_string"
        insert(parts, str:sub(l, r))
        i = r + 1
    end

    -- Second parse what remains as "pre[ql]being_quoted"
    local l = str:find(ql, i, true)
    if l then
        -- "pre"
        if l > 1 then
            insert(parts, str:sub(i, l - 1))
        end

        -- "being_quoted"
        insert(parts, str:sub(l))
    elseif i < #str then
        -- Finally add whatever remains...
        insert(parts, str:sub(i))
    end

    return parts
end

--------------------------------------------------------------------------------
function clink.prompt.register_filter(filter, priority)
    if priority == nil then
        priority = 999
    end

    table.insert(clink.prompt.filters, {f=filter, p=priority})
    table.sort(clink.prompt.filters, function(a, b) return a["p"] < b["p"] end)
end

--------------------------------------------------------------------------------
function clink.filter_prompt(prompt)
    local function add_ansi_codes(p)
        local c = tonumber(clink.get_setting_int("prompt_colour"))
        if c < 0 then
            return p
        end

        c = c % 16

        --[[
            <4              >=4             %2
            0 0  0 Black    4 1 -3 Blue     0
            1 4  3 Red      5 5  0 Magenta  1
            2 2  0 Green    6 3 -3 Cyan     0
            3 6  3 Yellow   7 7  0 Gray     1
        --]]

        -- Convert from cmd.exe colour indices to ANSI ones.
        local colour_id = c % 8
        if (colour_id % 2) == 1 then
            if colour_id < 4 then
                c = c + 3
            end
        elseif colour_id >= 4 then
            c = c - 3
        end

        -- Clamp
        if c > 15 then
            c = 15
        end

        -- Build ANSI code
        local code = "\x1b[0;"
        if c > 7 then
            c = c - 8
            code = code.."1;"
        end
        code = code..(c + 30).."m"

        return code..p.."\x1b[0m"
    end

    clink.prompt.value = prompt

    for _, filter in ipairs(clink.prompt.filters) do
        if filter.f() == true then
            return add_ansi_codes(clink.prompt.value)
        end
    end

    return add_ansi_codes(clink.prompt.value)
end

-- vim: expandtab
