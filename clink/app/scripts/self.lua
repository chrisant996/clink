-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local nothing = clink.argmatcher()

--------------------------------------------------------------------------------
local dir_matcher = clink.argmatcher():addarg(clink.dir_matches)

--------------------------------------------------------------------------------
local inject = clink.argmatcher()
:addflags(
    "--help",
    "--pid",
    "--profile"..dir_matcher,
    "--quiet",
    "--nolog",
    "--scripts"..dir_matcher)

--------------------------------------------------------------------------------
local autorun_dashdash = clink.argmatcher()
:addarg("--" .. inject)

local autorun = clink.argmatcher()
:addflags(
    "--allusers",
    "--help")
:addarg(
    "install"   .. autorun_dashdash,
    "uninstall" .. nothing,
    "show"      .. nothing,
    "set")

--------------------------------------------------------------------------------
local echo = clink.argmatcher()
:addflags("--help")
:nofiles()

--------------------------------------------------------------------------------
local function is_prefix3(s, ...)
    for _,i in ipairs({ ... }) do
        if #i < 3 or #s < 3 then
            if i == s then
                return true
            end
        else
            if i:sub(1, #s) == s then
                return true
            end
        end
    end
    return false
end

--------------------------------------------------------------------------------
local function classify_to_end(idx, line_state, classify, wc)
    while idx <= line_state:getwordcount() do
        classify:classifyword(idx, wc)
        idx = idx + 1
    end
end

--------------------------------------------------------------------------------
local function color_handler(word_index, line_state, classify)
    local i = word_index
    local include_clear = true
    local include_bold = true
    local include_bright = true
    local include_underline = true
    local include_color = true
    local include_on = true
    local include_sgr = true
    local invalid = false

    while i <= line_state:getwordcount() do
        local word = line_state:getword(i)

        include_clear = false

        if word ~= "" and word ~= "sgr" then
            include_sgr = false
        end

        if word == "on" then
            if not include_on then
                invalid = true
                break
            end
            include_bold = false
            include_bright = true
            include_underline = false
            include_color = true
            include_on = false
        elseif is_prefix3(word, "bold", "nobold", "dim") then
            if not include_bold then
                invalid = true
                break
            end
            include_bold = false
        elseif is_prefix3(word, "bright") then
            if not include_bright then
                invalid = true
                break
            end
            include_bright = false
        elseif is_prefix3(word, "underline", "nounderline") then
            if not include_underline then
                invalid = true
                break
            end
            include_underline = false
        elseif is_prefix3(word, "default", "normal", "black", "red", "green", "yellow", "blue", "cyan", "magenta", "white") then
            if not include_color then
                invalid = true
                break
            end
            include_bold = false
            include_bright = false
            include_underline = false
            include_color = false
        elseif word == "sgr" then
            if not include_sgr then
                invalid = true
                break
            end
            if classify then
                while i <= line_state:getwordcount() do
                    classify:classifyword(i, "a") --arg
                    i = i + 1
                end
                return true -- classify has been handled
            end
            return {}
        elseif word ~= "" then
            invalid = true
            break
        end

        if classify then
            classify:classifyword(i, "a") --arg
        end

        i = i + 1
    end

    if classify and invalid then
        classify_to_end(i, line_state, classify, "n") --none
    end
    if classify or invalid then
        return {}
    end

    local list = {}
    if include_on then
        table.insert(list, "on")
    end
    if include_bold then
        table.insert(list, "bold")
        table.insert(list, "nobold")
    end
    if include_bright then
        table.insert(list, "bright")
    end
    if include_underline then
        table.insert(list, "underline")
        table.insert(list, "nounderline")
    end
    if include_color then
        table.insert(list, "default")
        table.insert(list, "normal")
        table.insert(list, "black")
        table.insert(list, "red")
        table.insert(list, "green")
        table.insert(list, "yellow")
        table.insert(list, "blue")
        table.insert(list, "cyan")
        table.insert(list, "magenta")
        table.insert(list, "white")
    end
    if include_sgr then
        table.insert(list, "sgr")
    end
    if include_clear then
        table.insert(list, "clear")
    end
    if #list == 0 then
        return nil
    end
    return list
end

--------------------------------------------------------------------------------
local function set_handler(match_word, word_index, line_state)
    return settings.list()
end

--------------------------------------------------------------------------------
local function value_handler(match_word, word_index, line_state, builder, classify)
    local name = ""
    if word_index <= 3 then
        return
    end

    -- Use relative positioning to get the word, in case flags were used.
    name = line_state:getword(word_index - 1)
    local info = settings.list(name)
    if not info then
        return
    end

    if info.type == "color" then
        return color_handler(word_index, line_state)
    elseif info.type == "string" then
        return clink.filematches(line_state:getendword())
    else
        return info.values
    end
end

--------------------------------------------------------------------------------
local function classify_handler(arg_index, word, word_index, line_state, classify)
    if arg_index == 1 then
        -- Classify the setting name.
        local info = settings.list(word, true)
        if info then
            classify:classifyword(word_index, "a") --arg
        else
            classify_to_end(word_index, line_state, classify, "n") --none
            return true
        end

        -- Classify the setting value.
        local idx = word_index + 1
        if info.type == "color" then
            color_handler(idx, line_state, classify)
            return true
        elseif info.type == "string" then
            -- If there are no matches listed, then it's a string field.  In
            -- that case classify the rest of the line as "other" words so they
            -- show up in a uniform color.
            classify_to_end(idx, line_state, classify, "o") --other
            return true
        elseif info.type == "integer" then
            classify:classifyword(idx, "o") --other
        else
            local t = "n" --none
            local value = clink.lower(line_state:getword(idx))
            for _,i in ipairs(info.values) do
                if clink.lower(i) == value then
                    t = "a" --arg
                    break
                end
            end
            classify:classifyword(idx, t)
        end

        -- Anything further is unrecognized.
        classify_to_end(idx + 1, line_state, classify, "n") --none
    end
    return true
end

--------------------------------------------------------------------------------
local set = clink.argmatcher()
:addflags("--help")
:addarg(set_handler)
:addarg(value_handler)
:setclassifier(classify_handler)

--------------------------------------------------------------------------------
local history = clink.argmatcher()
:addflags("--help")
:addarg(
    "add",
    "clear"     .. nothing,
    "compact"   .. nothing,
    "delete"    .. nothing,
    "expand")

--------------------------------------------------------------------------------
clink.argmatcher(
    "clink",
    "clink_x86.exe",
    "clink_x64.exe")
:addarg(
    "autorun"   .. autorun,
    "echo"      .. echo,
    "history"   .. history,
    "info"      .. nothing,
    "inject"    .. inject,
    "set"       .. set)
:addflags(
    "--help",
    "--profile"..dir_matcher,
    "--version")

--------------------------------------------------------------------------------
local set_generator = clink.generator(clink.argmatcher_generator_priority - 1)

function set_generator:generate(line_state, match_builder)
    local first_word = clink.lower(path.getname(line_state:getword(1)))
    if path.getbasename(first_word) ~= "clink" and first_word ~= "clink_x64.exe" and first_word ~= "clink_x86.exe" then
        return
    end

    local index = 2
    while index < line_state:getwordcount() do
        local word = line_state:getword(index)
        if word == "--help" then
        elseif word == "--version" then
        elseif word == "--profile" then
            index = index + 1
        else
            break
        end
        index = index + 1
    end

    if line_state:getword(index) ~= "set" then
        return
    end

    index = index + 1
    if line_state:getword(index) == "--help" then
        index = index + 1
    end

    if index == line_state:getwordcount() then
        return
    end

    index = index + 1
    local matches = value_handler(line_state:getword(index), index, line_state)
    if matches then
        match_builder:addmatches(matches, "arg")
    end
    return true
end
