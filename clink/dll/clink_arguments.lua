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
local function traverse(generator, parts, text, first, last)
    -- Each part of the command line leading up to 'text' is considered as
    -- a level of the 'generator' tree.
    local part = parts[parts.n]
    parts.n = parts.n + 1

    -- Functions and booleans are leafs of the tree.
    local t = type(generator)
    if t == "function" then
        return generator(text, first, last)
    elseif t == "boolean" then
        return generator
    elseif t ~= "table" then
        return false
    end

    -- Key/value pair is a node of the tree.
    local next_gen = generator[part]
    if next_gen then
        return traverse(next_gen, parts, text, first, last)
    end

    -- Check generator[1] for behaviour flags.
    -- * = If generator is a leave in the tree, repeat it for ever.
    -- + = User must have typed at least one character for matches to be added.
    local repeat_leaf = false
    local allow_empty_text = true
    local node_flags = generator[clink.arg.node_flags_key]
    if node_flags then
        repeat_leaf = (node_flags:find("*") ~= nil)
        allow_empty_text = (node_flags:find("+") == nil)
    end

    -- See if we should early-out if we've no text to search with.
    if not allow_empty_text and text == "" then
        return false
    end

    for key, value in pairs(generator) do
        -- Strings are also leafs.
        if value == part and not repeat_leaf then
            return false
        end

        -- So we're in a node but don't have enough info yet to traverse
        -- further down the tree. Attempt to pull out keys or array entries
        -- and add them as matches.
        local candidate = key
        if type(key) == "number" then
            candidate = value
        end

        if candidate ~= clink.arg.node_flags_key then
            if type(candidate) == "string" then
                if clink.is_match(text, candidate) then
                    clink.add_match(candidate)
                end
            end
        end
    end

    return clink.match_count() > 0
end

--------------------------------------------------------------------------------
function clink.argument_match_generator(text, first, last)
    -- Extract the command name (naively)
    local leading = rl_line_buffer:sub(1, first - 1):lower()
    local cmd_start, cmd_end, cmd, ext = leading:find("^%s*([%w%-_]+)(%.*[%l]*)%s+")
    if not cmd_start then
        return false
    end

    -- Check to make sure the extension extracted is in pathext.
    if ext and ext ~= "" then
        if not clink.get_env("pathext"):lower():match(ext.."[;$]", 1, true) then
            return false
        end
    end

    -- Find a registered generator.
    local generator = clink.arg.generators[cmd]
    if generator == nil then
        return false
    end

    -- Split the command line into parts.
    local str = rl_line_buffer:sub(cmd_end, first - 1)
    local parts = {}
    for _, r, part in function () return str:find("^%s*([^%s]+)") end do
        if part:find("\"") then
        else
            table.insert(parts, part)
        end
        str = str:sub(r+1)
    end

    parts.n = 1
    return traverse(generator, parts, text, first, last)
end

--------------------------------------------------------------------------------
clink.register_match_generator(clink.argument_match_generator, -1)
