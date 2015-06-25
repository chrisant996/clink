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
clink.prompt = {}
clink.prompt.filters = {}

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
