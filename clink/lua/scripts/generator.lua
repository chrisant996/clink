-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _generators = {}
local _generators_unsorted = false



--------------------------------------------------------------------------------
local function pcall_dispatch(func, ...)
    local ok, ret = pcall(func, ...)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return
    end

    return ret
end

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
function clink._generate(line_state, match_builder)
    local impl = function ()
        for _, generator in ipairs(_generators) do
            local ret = generator:generate(line_state, match_builder)
            if ret == true then
                return true
            end
        end

        return false
    end

    prepare()
    local ret = pcall_dispatch(impl)
    return ret or false
end

--------------------------------------------------------------------------------
function clink._get_prefix_length(line_state)
    local impl = function ()
        local ret = 0
        for _, generator in ipairs(_generators) do
            if generator.getprefixlength then
                local i = generator:getprefixlength(line_state) or 0
                if i > ret then ret = i end
            end
        end

        return ret
    end

    prepare()
    local ret = pcall_dispatch(impl)
    return ret or 0
end

--------------------------------------------------------------------------------
--- -name:  clink.generator
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new match generator object.  Define on the object a
--- <code>generate()</code> function which gets called in increasing
--- <em>priority</em> order (low values to high values) when generating matches
--- for completion.  See <a
--- href="#matchgenerators">Match Generators</a> for more information.
function clink.generator(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_generators, ret)

    _generators_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
-- Deprecated.
local _current_builder = nil
local _any_added = false
local _any_pathish = false

--------------------------------------------------------------------------------
--- -name:  clink.add_match
--- -arg:   match:string
--- -ret:   nil
--- -deprecated: builder:addmatch
--- This is a shim that lets clink.register_match_generator continue to work
--- for now, despite being obsolete.
function clink.add_match(match)
    if _current_builder then
        _current_builder:addmatch(match)
        _any_added = true
        if match:find("\\") then
            _any_pathish = true
        end
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.is_match
--- -arg:   needle:string
--- -arg:   candidate:string
--- -ret:   true
--- -deprecated: clink.generator
--- This is no longer needed, and simply returns true now.
function clink.is_match(needle, candidate)
    return true
end

--------------------------------------------------------------------------------
--- -name:  rl_state
--- -var:   table
--- -deprecated: line
--- This is an obsolete global variable that was set while running match
--- generators.  Its table scheme is <em style="white-space:nowrap">{
--- line_buffer:string, point:integer}</em>, but it has been superseded by the
--- <a href="#line">line</a> type parameter passed into match generator
--- functions when using the new <a href="#clink.generator">clink.generator</a>
--- API.

--------------------------------------------------------------------------------
--- -name:  clink.register_match_generator
--- -arg:   func:function
--- -arg:   priority:integer
--- -show:  -- Deprecated form:
--- -show:  local function match_generator_func(text, first, last, match_builder)
--- -show:  &nbsp; -- `text` is the line text.
--- -show:  &nbsp; -- `first` is the index of the beginning of the end word.
--- -show:  &nbsp; -- `last` is the index of the end of the end word.
--- -show:  &nbsp; -- `clink.add_match()` is used to add matches.
--- -show:  &nbsp; -- return true if handled, or false to let another generator try.
--- -show:  end
--- -show:  clink.register_match_generator(match_generator_func, 10)<br/>
--- -show:  -- Replace with new form:
--- -show:  local g = clink.generator(10)
--- -show:  function g:generate(line_state, match_builder)
--- -show:  &nbsp; -- `line_state` is a <a href="#line">line</a> object.
--- -show:  &nbsp; -- `match_builder:<a href="#builder:addmatch">addmatch</a>()` is used to add matches.
--- -show:  &nbsp; -- return true if handled, or false to let another generator try.
--- -show:  end
--- -deprecated: clink.generator
--- Registers a generator function for producing matches.  This behaves
--- similarly to v0.4.8, but not identically.  The Clink schema has changed
--- significantly enough that there is no direct 1:1 translation; generators are
--- called at a different time than before and have access to more information
--- than before.
function clink.register_match_generator(func, priority)
    local g = clink.generator(priority)
    function g:generate(line_state, match_builder)
        _current_builder = match_builder

        local text = line_state:getendword()
        local info = line_state:getwordinfo(line_state:getwordcount())
        local first = info.offset
        local last = first + info.length - 1
        -- // TODO: adjust_for_separator()?

        _any_added = false;
        _any_pathish = false;

        local handled = func(text, first, last)

        -- Ugh; git_autocomplete_branch.lua needs setprefixincluded(true) if
        -- func added otherwise branch name completion gets stuck at path
        -- separators.  This attempts to be as backwardly compatible as we can.
        if _any_added and not _any_pathish then
            match_builder:setprefixincluded(true)
        end

        _current_builder = nil
        return handled
    end
end
