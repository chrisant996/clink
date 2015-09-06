-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink.matches = {}
clink.generators = {}

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
        find_func = os.globfiles
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

    clink.matches = {}
    clink.match_display_filter = nil

    for _, generator in ipairs(clink.generators) do
        if generator.f(text, first, last) == true then
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
