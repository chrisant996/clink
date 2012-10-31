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

clink.arg = {}
clink.arg.generators = {}
clink.arg.node_flags_key = "\x01"

clink.prompt = {}
clink.prompt.filters = {}

--------------------------------------------------------------------------------
function clink.compute_lcd(text, list)
    if #list < 2 then
        return
    end

    local early_out = #text
    local lcd = list[1]
    for i = 2, #list, 1 do
        for j = 1, #lcd, 1 do
            if lcd:sub(1, j):lower() ~= list[i]:sub(1, j):lower() then
                lcd = lcd:sub(1, j - 1)
                break
            end
        end

        if #lcd <= early_out then
            break
        end
    end

    return text..lcd:sub(early_out + 1)
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
function clink.generate_matches(text, first, last)
    clink.matches = {}
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
function clink.arg.register_tree(cmd, generator)
    clink.arg.generators[cmd:lower()] = generator
end

--------------------------------------------------------------------------------
function clink.arg.tree_node(flags, content)
    local node = {}
    for key, arg in pairs(content) do
        node[key] = arg
    end

    node[clink.arg.node_flags_key] = flags
    return node
end

--------------------------------------------------------------------------------
function clink.arg.node_transpose(a, b)
    local c = {}
    for _, i in ipairs(a) do
        c[i] = b
    end

    return c
end

--------------------------------------------------------------------------------
function clink.arg.node_merge(a, b)
    c = {}
    d = {a, b}
    for _, i in ipairs(d) do
        if type(i) ~= "table" then
            i = {i}
        end

        for j, k in pairs(i) do
            if type(j) == "number" then
                table.insert(c, k)
            else
                c[j] = k
            end
        end
    end

    return c
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
    clink.prompt.value = prompt

    for _, filter in ipairs(clink.prompt.filters) do
        if filter.f() == true then
            return clink.prompt.value
        end
    end

    return clink.prompt.value
end

-- vim: expandtab
