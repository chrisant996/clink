-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, generator).

--------------------------------------------------------------------------------
clink = clink or {}
local _generators = {}
local _generators_unsorted = false
local file_match_generator = {}

if settings.get("lua.debug") or clink.DEBUG then
    clink.debug = clink.debug or {}
    clink.debug._generators = _generators
    clink.debug._file_match_generator = file_match_generator
end

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
local function advance_ignore_quotes(state)
    local word = state[1]
    local seek = #word
    if seek > 0 then
        while true do
            -- Finding a non-quote is success.
            if word:sub(seek, 1) ~= '"' then
                state[1] = word:sub(1, seek - 1)
                return true
            end
            -- Reaching the beginning is failure.
            if seek <= 1 then
                break
            end
            seek = seek -1
            -- Finding a `\"` digraph is a failure.
            if word:sub(seek, 1) == "\\" then
                break
            end
        end
    end
    return false
end

--------------------------------------------------------------------------------
local function is_dots(word)
    local state = { word }

    if not advance_ignore_quotes(state) then
        return false            -- Too short.
    elseif state[1]:sub(-1) ~= "." then
        return false            -- No dot at end.
    end

    if not advance_ignore_quotes(state) then
        return true             -- Exactly ".".
    elseif state[1]:sub(-1) == "." and not advance_ignore_quotes(state) then
        return true             -- Exactly "..".
    end

    local last = state[1]:sub(-1)
    if last == "/" or last == "\\" then
        return true             -- Ends with "\." or "\..".
    end

    return false                -- Else nope.
end

--------------------------------------------------------------------------------
function file_match_generator:generate(line_state, match_builder)
    local root = line_state:getendword()
    if root == "~" then
        root = path.join(root, "")
    end
    match_builder:addmatches(clink.filematches(root))
    return true
end

--------------------------------------------------------------------------------
function file_match_generator:getwordbreakinfo(line_state)
    local endword = line_state:getendword()
    local keep = #endword
    if endword == "~" then
        -- Tilde by itself should be expanded, so keep the whole word.
    elseif is_dots(endword) then
        -- `.` or `..` should be kept so that matches can include `.` or
        -- `..` directories.  Bash includes `.` and `..` but only when those
        -- match typed text (i.e. when there's no input text, they are not
        -- considered possible matches).
    else
        keep = endword:find("[/\\][^/\\]*$") or 0
        if keep < 2 and endword:sub(2, 2) == ":" then
            keep = 2
        end
    end
    return 0, keep
end



--------------------------------------------------------------------------------
local _match_generate_state = {}
local function cancel_match_generate_coroutine()
    local _, ismain = coroutine.running()
    if ismain and _match_generate_state.coroutine then
        -- Make things (e.g. globbers) short circuit to faciliate coroutine
        -- completing as quickly as possible.
        clink._cancel_coroutine(_match_generate_state.coroutine)
        if not _match_generate_state.started then
            -- If it never started, remove it from the scheduler.
            clink.removecoroutine(_match_generate_state.coroutine)
        end
        _match_generate_state = {}
    end
end

--------------------------------------------------------------------------------
function clink._make_match_generate_coroutine(line, lines, matches, builder, generation_id)
    -- Bail if there's already a match generator coroutine running.
    if _match_generate_state.coroutine then
        return
    end

    -- Create coroutine to generate matches.  The coroutine is automatically
    -- scheduled for resume while waiting for input.
    coroutine.override_isgenerator()
    local c = coroutine.create(function ()
        -- Mark that the coroutine has started.  If a canceled coroutine never
        -- started, it can be removed from the scheduler.
        _match_generate_state.started = true

        -- Generate matches.
        clink._generate(line, lines, builder)

        -- Coroutine completed, so stop tracking it.  Must stop tracking before
        -- calling clink.matches_ready() so it doesn't immediately bail out.
        local c = coroutine.running()
        if _match_generate_state.coroutine == c then
            _match_generate_state = {}
        end

        -- Check for cancelation.
        if not clink._is_coroutine_canceled(c) then
            -- PERF: This can potentially take some time, especially in Debug
            -- builds.
            if clink.matches_ready(generation_id) then
                clink._keep_coroutine_events(c)
            end
        else
            builder:clear_toolkit()
        end
    end)

    clink.setcoroutinename(c, "generate matches")
    _match_generate_state.coroutine = c
    _match_generate_state.started = nil
end



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
    _match_generate_state = {}
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
function clink._in_generate()
    return clink.co_state._current_builder and true
end

--------------------------------------------------------------------------------
function clink._generate(line_state, line_states, match_builder, old_filtering)
    local impl = function ()
        clink.generator_stopped = nil

        -- Backward compatibility shim.
        rl_state = { line_buffer = line_state:getline(), point = line_state:getcursor() }

        -- Cancel any coroutines for match generation (apart from this one).
        cancel_match_generate_coroutine()

        -- Run match generators.
        for _, generator in ipairs(_generators) do
            local ret = generator:generate(line_state, match_builder)
            if ret == true then
                -- Remember the generator function that stopped.
                clink.generator_stopped = generator.generate
                return true
            end
        end

        if file_match_generator:generate(line_state, match_builder) then
            return true
        end

        return false
    end

    clink._reset_display_filter()
    clink.co_state.use_old_filtering = old_filtering
    clink.co_state.argmatcher_line_states = line_states

    prepare()
    clink.co_state._current_builder = match_builder

    local ok, ret = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("match generator failed:")
        print(ret)
        clink.co_state._current_builder = nil
        clink.co_state.use_old_filtering = nil
        clink.co_state.argmatcher_line_states = nil
        rl_state = nil
        return
    end

    clink.co_state._current_builder = nil
    clink.co_state.use_old_filtering = nil
    clink.co_state.argmatcher_line_states = nil
    rl_state = nil
    return ret or false
end

--------------------------------------------------------------------------------
function clink._get_word_break_info(line_state)
    local impl = function ()
        local truncate = 0
        local keep = 0

        local doeach = function (generator)
            local t, k = generator:getwordbreakinfo(line_state)
            t = t or 0
            k = k or 0
            if (t > truncate) or (t == truncate and k > keep) then
                truncate = t
                keep = k
            end
        end

        for _, generator in ipairs(_generators) do
            if generator.getwordbreakinfo then
                doeach(generator)
            end
        end

        doeach(file_match_generator)

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
    if clink.co_state._current_builder then
        clink.co_state._current_builder:deprecated_addmatch(match)
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
    local glob
    pattern = pattern:gsub("/", "\\")
    if type(find_func) == "function" then
        glob = find_func(pattern, true)
    end

    -- Get glob's base.
    local base = ""
    local i = pattern:find("[\\:][^\\:]*$")
    if i and full_path then
        base = pattern:sub(1, i)
    end

    -- Match them.
    local num = 0
    if type(glob) == "table" then
        for _, i in ipairs(glob) do
            local full = base..tostring(i)
            clink.add_match(full)
            num = num + 1
        end
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
--- This is only needed when using deprecated APIs.  It's automatically inferred
--- from the match type when using the current APIs.
function clink.matches_are_files(files)
    if clink.co_state._current_builder then
        clink.co_state._current_builder:setmatchesarefiles(files)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.suppress_char_append
--- -deprecated: builder:setsuppressappend
function clink.suppress_char_append()
    if clink.co_state._current_builder then
        clink.co_state._current_builder:setsuppressappend(true)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.suppress_quoting
--- -deprecated: builder:setsuppressquoting
function clink.suppress_quoting()
    if clink.co_state._current_builder then
        clink.co_state._current_builder:setsuppressquoting(true)
    end
end

--------------------------------------------------------------------------------
--- -name:  rl_state
--- -deprecated: line_state
--- -var:   table
--- This is an obsolete global variable that was set while running match
--- generators.  It has been superseded by the
--- <a href="#line_state">line_state</a> type parameter passed into match
--- generator functions when using the new
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
--- -show:  &nbsp; -- `line_state` is a <a href="#line_state">line_state</a> object.
--- -show:  &nbsp; -- `match_builder:<a href="#builder:addmatch">addmatch</a>()` is used to add matches.
--- -show:  &nbsp; -- return true if handled, or false to let another generator try.
--- -show:  end
function clink.register_match_generator(func, priority)
    local g = clink.generator(priority)
    function g:generate(line_state, match_builder)
        local text = line_state:getendword()
        local info = line_state:getwordinfo(line_state:getwordcount())
        local first = info.offset
        local last
        if clink.co_state.use_old_filtering then
            last = line_state:getcursor() - 1
        else
            last = first + info.length - 1
        end
        -- // TODO: adjust_for_separator()?

        return func(text, first, last)
    end
end

--------------------------------------------------------------------------------
function clink._diag_generators()
    if not settings.get("lua.debug") then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.
    local print = clink.print

    local any = false

    clink.print(bold.."generators:"..norm)
    for _,generator in ipairs (_generators) do
        if generator.generate then
            local info = debug.getinfo(generator.generate, 'S')
            if info.short_src ~= "?" then
                print("  "..info.short_src..":"..info.linedefined)
                any = true
            end
        end
    end

    if not any then
        print("  no generators registered")
    end
end
