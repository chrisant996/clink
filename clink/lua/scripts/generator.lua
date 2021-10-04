-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, generator).

--------------------------------------------------------------------------------
clink = clink or {}
local _generators = {}
local _generators_unsorted = false

--------------------------------------------------------------------------------
--- -name:  clink.match_display_filter
--- -deprecated: clink.ondisplaymatches
--- -var:   function
--- A match generator can set this varible to a filter function that is called
--- before displaying matches.  It is reset every time match generation is
--- invoked.  The filter function receives table argument containing the matches
--- that are going to be displayed, and it returns a table filtered as required
--- by the match generator.
---
--- See <a href="#filteringthematchdisplay">Filtering the Match Display</a> for
--- more information.
clink.match_display_filter = nil

--------------------------------------------------------------------------------
-- Deprecated.
local _current_builder = nil



--------------------------------------------------------------------------------
-- This global variable tracks which generator function, if any, stopped the
-- most recent generate pass.  It's useful for diagnostic purposes; the file and
-- number can be retrieved by:
--      local info = debug.getinfo(clink.generator_stopped, 'S')
--      print("file: "..info.short_src)
--      print("line: "..info.linedefined)
clink.generator_stopped = nil
local function generator_onbeginedit()
    clink.generator_stopped = nil
end
clink.onbeginedit(generator_onbeginedit)


--------------------------------------------------------------------------------
local function prepare()
    -- Sort generators by priority if required.
    if _generators_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(_generators, lambda)

        _generators_unsorted = false
    end
end

--------------------------------------------------------------------------------
function clink._reset_display_filter()
    clink.match_display_filter = nil
    clink._event_callbacks["onfiltermatches"] = nil
    clink._event_callbacks["ondisplaymatches"] = nil
end

--------------------------------------------------------------------------------
function clink._generate(line_state, match_builder)
    local impl = function ()
        clink.generator_stopped = nil

        for _, generator in ipairs(_generators) do
            local ret = generator:generate(line_state, match_builder)
            if ret == true then
                -- Remember the generator function that stopped.
                clink.generator_stopped = generator.generate
                return true
            end
        end

        return false
    end

    clink._reset_display_filter()

    prepare()
    _current_builder = match_builder

    local ok, ret = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("match generator failed:")
        print(ret)
        return
    end

    _current_builder = nil
    return ret or false
end

--------------------------------------------------------------------------------
function clink._get_word_break_info(line_state)
    local impl = function ()
        local truncate = 0
        local keep = 0
        for _, generator in ipairs(_generators) do
            if generator.getwordbreakinfo then
                local t, k = generator:getwordbreakinfo(line_state)
                t = t or 0
                k = k or 0
                if (t > truncate) or (t == truncate and k > keep) then
                    truncate = t
                    keep = k
                end
            end
        end

        return truncate, keep
    end

    prepare()

    local ok, ret1, ret2 = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("getting word break info failed:")
        print(ret1)
        return
    end

    return ret1, ret2
end

--------------------------------------------------------------------------------
--- -name:  clink.generator
--- -ver:   1.0.0
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new match generator object.  Define on the object a
--- <code>:generate()</code> function which gets called in increasing
--- <span class="arg">priority</span> order (low values to high values) when
--- generating matches for completion.  See
--- <a href="#matchgenerators">Match Generators</a> for more information.
function clink.generator(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_generators, ret)

    _generators_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
--- -name:  clink.add_match
--- -deprecated: builder:addmatch
--- -arg:   match:string
--- -ret:   nil
--- This is a shim that lets clink.register_match_generator continue to work
--- for now, despite being obsolete.
function clink.add_match(match)
    if _current_builder then
        _current_builder:addmatch(match)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.match_count
--- -deprecated:
--- -ret:   integer
--- This is no longer supported, and always returns 0.  If a script needs to
--- know how many matches it added, the script should keep track of the count
--- itself.
function clink.match_count()
    _compat_warning("clink.match_count() is no longer supported.")
    return 0
end

--------------------------------------------------------------------------------
--- -name:  clink.get_match
--- -deprecated:
--- -arg:   index:integer
--- -ret:   string
--- This is no longer supported, and always returns an empty string.  If a
--- script needs to access matches it added, the script should keep track of the
--- matches itself.
function clink.get_match()
    _compat_warning("clink.get_match() is no longer supported.")
    return ""
end

--------------------------------------------------------------------------------
--- -name:  clink.set_match
--- -deprecated:
--- -arg:   index:integer
--- -arg:   value:string
--- This is no longer supported, and does nothing.
function clink.set_match()
    _compat_warning("clink.set_match() is no longer supported.")
end

--------------------------------------------------------------------------------
--- -name:  clink.match_files
--- -deprecated: clink.filematches
--- -arg:   pattern:string
--- -arg:   [full_path:boolean]
--- -arg:   [find_func:function]
function clink.match_files(pattern, full_path, find_func)
    -- This is ported from Clink v0.4.9 as identically as possible to minimize
    -- behavioral differences.  However, that was NOT a good implementation of
    -- the functionality, and it malfunctions with some inputs.

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
    local num = 0
    for _, i in ipairs(glob) do
        local full = base..i
        clink.add_match(full)
        num = num + 1
    end
    return num
end

--------------------------------------------------------------------------------
--- -name:  clink.match_words
--- -deprecated: builder:addmatches
--- -arg:   text:string
--- -arg:   words:table
--- This adds words that match the text.
function clink.match_words(text, words)
    local num = 0
    for _, i in ipairs(words) do
        if clink.is_match(text, i) then
            clink.add_match(i)
            num = num + 1
        end
    end
    return num
end

--------------------------------------------------------------------------------
--- -name:  clink.compute_lcd
--- -deprecated:
--- -arg:   text:string
--- -arg:   matches:table
--- -ret:   string
--- This is no longer supported, and always returns an empty string.
function clink.compute_lcd()
    _compat_warning("clink.compute_lcd() is no longer supported.")
    return ""
end

--------------------------------------------------------------------------------
--- -name:  clink.is_single_match
--- -deprecated:
--- -arg:   matches:table
--- -ret:   boolean
--- This is no longer supported, and always returns false.
function clink.is_single_match()
    _compat_warning("clink.is_single_match() is no longer supported.")
    return false
end

--------------------------------------------------------------------------------
--- -name:  clink.is_match
--- -deprecated: clink.generator
--- -arg:   needle:string
--- -arg:   candidate:string
--- -ret:   boolean
--- This returns true if <span class="arg">needle</span> is a prefix of
--- <span class="arg">candidate</span> with a case insensitive comparison.
---
--- Normally in Clink v1.x and higher the <span class="arg">needle</span> will
--- be an empty string because the generators are no longer responsible for
--- filtering matches.  The match pipeline itself handles that internally now.
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
--- -name:  clink.matches_are_files
--- -deprecated: builder:addmatch
--- -arg:   [files:boolean]
--- This is no longer needed, because now it's inferred from the match type when
--- adding matches.
function clink.matches_are_files(files)
    if _current_builder then
        _current_builder:setmatchesarefiles(files)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.suppress_char_append
--- -deprecated: builder:setsuppressappend
function clink.suppress_char_append()
    if _current_builder then
        _current_builder:setsuppressappend(true)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.suppress_quoting
--- -deprecated: builder:setsuppressquoting
function clink.suppress_quoting()
    if _current_builder then
        _current_builder:setsuppressquoting(true)
    end
end

--------------------------------------------------------------------------------
--- -name:  rl_state
--- -deprecated: line
--- -var:   table
--- This is an obsolete global variable that was set while running match
--- generators.  It has been superseded by the <a href="#line">line</a> type
--- parameter passed into match generator functions when using the new
--- <a href="#clink.generator">clink.generator</a> API.

--------------------------------------------------------------------------------
--- -name:  clink.register_match_generator
--- -deprecated: clink.generator
--- -arg:   func:function
--- -arg:   priority:integer
--- Registers a generator function for producing matches.  This behaves
--- similarly to v0.4.8, but not identically.  The Clink schema has changed
--- significantly enough that there is no direct 1:1 translation; generators are
--- called at a different time than before and have access to more information
--- than before.
--- -show:  -- Deprecated form:
--- -show:  local function match_generator_func(text, first, last)
--- -show:  &nbsp; -- `text` is the word text.
--- -show:  &nbsp; -- `first` is the index of the beginning of the end word.
--- -show:  &nbsp; -- `last` is the index of the end of the end word.
--- -show:  &nbsp; -- `clink.add_match()` is used to add matches.
--- -show:  &nbsp; -- return true if handled, or false to let another generator try.
--- -show:  end
--- -show:  clink.register_match_generator(match_generator_func, 10)
--- -show:
--- -show:  -- Replace with new form:
--- -show:  local g = clink.generator(10)
--- -show:  function g:generate(line_state, match_builder)
--- -show:  &nbsp; -- `line_state` is a <a href="#line">line</a> object.
--- -show:  &nbsp; -- `match_builder:<a href="#builder:addmatch">addmatch</a>()` is used to add matches.
--- -show:  &nbsp; -- return true if handled, or false to let another generator try.
--- -show:  end
function clink.register_match_generator(func, priority)
    local g = clink.generator(priority)
    function g:generate(line_state, match_builder)
        local text = line_state:getendword()
        local info = line_state:getwordinfo(line_state:getwordcount())
        local first = info.offset
        local last = first + info.length - 1
        -- // TODO: adjust_for_separator()?

        return func(text, first, last)
    end
end
