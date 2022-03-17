-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, arguments).

--------------------------------------------------------------------------------
local _arglink = {}
_arglink.__index = _arglink
setmetatable(_arglink, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _arglink._new(key, matcher)
    return setmetatable({
        _key = key,
        _matcher = matcher,
    }, _arglink)
end



--------------------------------------------------------------------------------
local _argmatcher_fromhistory = {}
local _argmatcher_fromhistory_root
local _delayinit_generation = 0
local _clear_onuse_coroutine = {}
local _clear_delayinit_coroutine = {}

--------------------------------------------------------------------------------
clink.onbeginedit(function ()
    _delayinit_generation = _delayinit_generation + 1

    -- Clear dangling coroutine references in matchers.  Otherwise if a
    -- coroutine doesn't finish before a new edit line begins, there will be
    -- references that can't be garbage collected until the next time the
    -- matcher performs delayed initialization.
    for m,_ in pairs(_clear_onuse_coroutine) do
        m._onuse_coroutine = nil
    end
    for m,a in pairs(_clear_delayinit_coroutine) do
        for i,_ in pairs(a) do
            m._init_coroutine[i] = nil
        end
    end
    _clear_onuse_coroutine = {}
    _clear_delayinit_coroutine = {}
end)



--------------------------------------------------------------------------------
local _argreader = {}
_argreader.__index = _argreader
setmetatable(_argreader, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argreader._new(root, line_state)
    local reader = setmetatable({
        _matcher = root,
        _realmatcher = root,
        _line_state = line_state,
        _arg_index = 1,
        _stack = {},
    }, _argreader)
    return reader
end

--------------------------------------------------------------------------------
--[[
local enable_tracing = true
function _argreader:trace(...)
    if self._tracing then
        print(...)
    end
end
function _argreader:starttracing(word)
    if enable_tracing then
        self._tracing = true
        self._dbgword = word
        self:trace()
        self:trace(word, "BEGIN", self._matcher, "stack", #self._stack, "arg_index", self._arg_index)
    end
end
--]]

--------------------------------------------------------------------------------
local function do_delayed_init(list, matcher, arg_index)
    -- Don't init while generating matches from history, as that could be
    -- excessively expensive (could run thousands of callbacks).
    if _argmatcher_fromhistory and _argmatcher_fromhistory.argmatcher then
        return
    end

    -- Track flags initialization as position 0.
    if matcher._flags and list == matcher._flags._args[1] then
        arg_index = 0
    end

    -- New edit line starts a new generation number.  Reset any delay init
    -- callbacks that didn't finish.
    if (matcher._init_generation or 0) < _delayinit_generation then
        matcher._init_coroutine = nil
        matcher._init_generation = _delayinit_generation
    end

    local _, ismain = coroutine.running()
    local async_delayinit = not ismain or not clink._in_generate()

    -- Start the delay init callback if it hasn't already started.
    local c = matcher._init_coroutine and matcher._init_coroutine[arg_index]
    if not c then
        if not matcher._init_coroutine then
            matcher._init_coroutine = {}
        end

        -- Run the delayinit callback in a coroutine so typing is responsive.
        c = coroutine.create(function ()
            -- Invoke the delayinit callback and add the results to the arg
            -- slot's list of matches.
            local addees = list.delayinit(matcher, arg_index)
            matcher:_add(list, addees)
            -- Mark the init callback as finished.
            matcher._init_coroutine[arg_index] = nil
            _clear_delayinit_coroutine[matcher][arg_index] = nil
            list.delayinit = nil
            -- If originally started from not-main, then reclassify.
            if async_delayinit then
                clink._invalidate_matches()
                clink.reclassifyline()
            end
        end)
        matcher._init_coroutine[arg_index] = c

        -- Set up to be able to efficiently clear dangling coroutine references,
        -- e.g. in case a coroutine doesn't finish before a new edit line.
        if not _clear_delayinit_coroutine[matcher] then
            _clear_delayinit_coroutine[matcher] = {}
        end
        _clear_delayinit_coroutine[matcher][arg_index] = c
    end

    -- Finish (run) the coroutine immediately only when the main coroutine is
    -- generating matches.
    if not async_delayinit then
        clink._finish_coroutine(c)
    else
        -- Run the coroutine up to the first yield, so that if it doesn't need
        -- to yield at all then it completes right now.
        coroutine.resume(c)
    end
end

--------------------------------------------------------------------------------
-- When word_index is < 0, skip classifying the word, and skip trying to figure
-- out whether a `-foo:` word should avoid following a linked parser.  This only
-- happens when parsing extra words from expanding a doskey alias.
--
-- On return, the _argreader should be primed for generating matches for the
-- NEXT word in the line.
function _argreader:update(word, word_index)
    local arg_match_type = "a" --arg
    local line_state = self._line_state

    --[[
    self._dbgword = word
    self:trace(word, "update")
    --]]

    -- Check for flags and switch matcher if the word is a flag.
    local is_flag
    local next_is_flag
    local end_flags
    local matcher = self._matcher
    local realmatcher = self._realmatcher
    local pushed_flags
    if not self._noflags then
        is_flag = matcher:_is_flag(word)
    end
    if is_flag then
        if matcher._flags then
            local arg = matcher._flags._args[1]
            if arg and arg.delayinit then
                do_delayed_init(arg, matcher, 0)
            end
            if word == matcher._endofflags then
                self._noflags = true
                end_flags = true
            end
            self:_push(matcher._flags, matcher)
            arg_match_type = "f" --flag
            pushed_flags = true
        else
            return
        end
    end
    if not is_flag and realmatcher._flagsanywhere == false then
        self._noflags = true
    else
        next_is_flag = not self._noflags and matcher:_is_flag(line_state:getword(word_index + 1))
    end

    -- Update matcher after possible _push.
    matcher = self._matcher
    realmatcher = self._realmatcher
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]
    local next_arg_index = arg_index + 1

    -- If arg_index is out of bounds we should loop if set or return to the
    -- previous matcher if possible.
    if next_arg_index > #matcher._args then
        if matcher._loop then
            self._arg_index = math.min(math.max(matcher._loop, 1), #matcher._args)
        else
            -- If next word is a flag, don't pop.  Flags are not positional, so
            -- a matcher can only be exhausted by a word that exceeds the number
            -- of argument slots the matcher has.
            if is_flag then
                self._arg_index = next_arg_index
            elseif not pushed_flags and next_is_flag then
                self._arg_index = next_arg_index
            elseif not self:_pop(next_is_flag) then
                -- Popping must use the _arg_index as is, without incrementing
                -- (it was already incremented before it got pushed).
                self._arg_index = next_arg_index
            end
        end
    else
        if end_flags then
            self:_pop(next_is_flag)
        end
        self._arg_index = next_arg_index
    end

    -- Some matchers have no args at all.  Or ran out of args.
    if not arg then
        if self._word_classifier and word_index >= 0 then
            if matcher._no_file_generation then
                self._word_classifier:classifyword(word_index, "n", false)  --none
            else
                self._word_classifier:classifyword(word_index, "o", false)  --other
            end
        end
        return
    end

    -- Delay initialize the argmatcher, if needed.
    if arg.delayinit then
        do_delayed_init(arg, realmatcher, arg_index)
    end

    -- Generate matches from history.
    if self._fromhistory_matcher then
        if self._fromhistory_matcher == matcher and self._fromhistory_argindex == arg_index then
            if _argmatcher_fromhistory.builder then
                _argmatcher_fromhistory.builder:addmatch(word, "word")
            end
        end
    end

    -- Parse the word type.
    if self._word_classifier and word_index >= 0 then
        if matcher._classify_func and matcher._classify_func(arg_index, word, word_index, line_state, self._word_classifier) then
            -- The classifier function says it handled the word.
        else
            -- Use the argmatcher's data to classify the word.
            local t = "o" --other
            if arg._links and arg._links[word] then
                t = arg_match_type
            else
                -- For performance reasons, don't run argmatcher functions
                -- during classify.  If that's needed, a script can provide a
                -- :classify function to complement a :generate function.
                local matched = false
                if arg_match_type == "f" then
                    -- When the word is a flag and ends with : or = then check
                    -- if the word concatenated with an adjacent following word
                    -- matches a known flag.  When so, classify both words.
                    if word:sub(-1):match("[:=]") then
                        if arg._links and arg._links[word] then
                            t = arg_match_type
                        else
                            local this_info = line_state:getwordinfo(word_index)
                            local next_info = line_state:getwordinfo(word_index + 1)
                            if this_info and next_info and this_info.offset + this_info.length == next_info.offset then
                                local combined_word = word..line_state:getword(word_index + 1)
                                for _, i in ipairs(arg) do
                                    if type(i) ~= "function" and i == combined_word then
                                        t = arg_match_type
                                        self._word_classifier:classifyword(word_index + 1, t, false)
                                        matched = true
                                        break
                                    end
                                end
                            end
                        end
                    elseif end_flags then
                        t = arg_match_type
                    end
                end
                if not matched then
                    for _, i in ipairs(arg) do
                        local it = type(i)
                        if it == "function" then
                            t = 'o' --other (placeholder; superseded by :classifyword).
                        elseif i == word or (it == "table" and i.match == word) then
                            t = arg_match_type
                            break
                        end
                    end
                end
            end
            self._word_classifier:classifyword(word_index, t, false)
        end
    end

    -- Does the word lead to another matcher?
    local linked
    if arg._links then
        linked = arg._links[word]
        if linked then
            if is_flag and word:match("[:=]$") and word_index >= 0 then
                local info = line_state:getwordinfo(word_index)
                if info and
                        line_state:getcursor() ~= info.offset + info.length and
                        line_state:getline():sub(info.offset + info.length, info.offset + info.length) == " " then
                    -- Don't follow linked parser on `--foo=` flag if there's a
                    -- space after the `:` or `=` unless the cursor is on the
                    -- space.
                    linked = nil
                end
            end
            if linked then
                self:_push(linked)
            end
        end
    end

    -- If it's a flag and doesn't have a linked matcher, then pop to restore the
    -- matcher that should be active for the next word.
    if not linked and is_flag then
        self:_pop(next_is_flag)
    end
end

--------------------------------------------------------------------------------
-- When matcher is a flags matcher, its outer matcher must be passed in (as
-- realmatcher) so that delayinit can be given the real matcher from the API's
-- perspective.
function _argreader:_push(matcher, realmatcher)
    -- v0.4.9 effectively pushed flag matchers, but not arg matchers.
    -- if not self._matcher._deprecated or self._matcher._is_flag_matcher or matcher._is_flag_matcher then
    if not matcher._deprecated or matcher._is_flag_matcher then
        table.insert(self._stack, { self._matcher, self._arg_index, self._realmatcher, self._noflags })
        --[[
        self:trace(self._dbgword, "push", matcher, "stack", #self._stack)
    else
        self:trace(self._dbgword, "set", matcher, "stack", #self._stack)
        --if self._tracing then pause() end
        --]]
    end

    self._matcher = matcher
    self._arg_index = 1
    self._realmatcher = realmatcher or matcher
    self._noflags = nil
end

--------------------------------------------------------------------------------
function _argreader:_pop(next_is_flag)
    if #self._stack <= 0 then
        return false
    end

    while #self._stack > 0 do
        if self._matcher and self._matcher._no_file_generation then
            -- :nofiles() dead-ends the parser.
            return false
        end

        self._matcher, self._arg_index, self._realmatcher, self._noflags = table.unpack(table.remove(self._stack))

        if self._matcher._loop then
            -- Matcher is looping; stop popping so it can handle the argument.
            break
        end
        if next_is_flag and self._matcher._flags then
            -- Matcher has flags and next_is_flag; stop popping so it can
            -- handle the flag.
            break
        end
        if self._arg_index <= #self._matcher._args then
            -- Matcher has arguments remaining; stop popping so it can handle
            -- the argument.
            break
        end
        if #self._matcher._args == 0 and self._matcher._flags then
            -- A matcher with flags but no args is a special case that means
            -- match one file argument.
            -- REVIEW:  Consider giving it a file argument to eliminate the
            -- special case treatments?
            if next_is_flag or self._arg_index == 1 then
                -- Stop popping so the matcher can handle the flag or argument.
                break
            end
        end
    end

    --[[
    self:trace("", "pop =>", self._matcher, "stack", #self._stack, "arg_index", self._arg_index, "realmatcher", self._realmatcher, "noflags", self._noflags)
    self._dbgword = ""
    --]]
    return true
end



--------------------------------------------------------------------------------
local _argmatcher = {}
_argmatcher.__index = _argmatcher
setmetatable(_argmatcher, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
local function apply_options_to_list(addee, list)
    if addee.nosort then
        list.nosort = true
    end
    if addee.delayinit then
        if type(addee.delayinit) == "function" then
            list.delayinit = addee.delayinit
        end
    end
    if addee.fromhistory then
        list.fromhistory = true
    end
end

--------------------------------------------------------------------------------
local function apply_options_to_builder(reader, arg, builder)
    -- Disable sorting, if requested.  This goes first because it is
    -- unconditional and should take effect immediately.
    if arg.nosort then
        builder:setnosort()
    end

    -- Delay initialize the argmatcher, if needed.
    if arg.delayinit then
        do_delayed_init(arg, reader._realmatcher, reader._arg_index)
    end

    -- Generate matches from history, if requested.
    if arg.fromhistory then
        -- Lua/C++/Lua language transition precludes running this in a
        -- coroutine, but also the performance of this might not always be
        -- responsive enough to run as often as suggestions would like to.
        local _, ismain = coroutine.running()
        if ismain then
            _argmatcher_fromhistory.argmatcher = reader._matcher
            _argmatcher_fromhistory.argslot = reader._arg_index
            _argmatcher_fromhistory.builder = builder
            -- Let the C++ code iterate through the history and call back into
            -- Lua to parse individual history lines.
            clink._generate_from_history()
            -- Clear references.  Clear builder because it goes out of scope,
            -- and clear other references to facilitate garbage collection.
            _argmatcher_fromhistory = {}
        end
    end
end

--------------------------------------------------------------------------------
local function add_prefix(prefixes, string)
    if string and type(string) == "string" then
        local prefix = string:sub(1, 1)
        if prefix:len() > 0 then
            if prefix:match('[A-Za-z]') then
                error("Flag string '"..string.."' is invalid because it starts with a letter and would interfere with argument matching.")
            else
                prefixes[prefix] = (prefixes[prefix] or 0) + 1
            end
        end
    end
end

--------------------------------------------------------------------------------
function _argmatcher._new()
    local matcher = setmetatable({
        _args = {},
    }, _argmatcher)
    matcher._flagprefix = {}
    matcher._nextargindex = 1
    return matcher
end

--------------------------------------------------------------------------------
function _argmatcher:setdebugname(name)
    self._dbgname = name
    return self
end

--------------------------------------------------------------------------------
function _argmatcher:getdebugname()
    return self._dbgname or "<unnamed>"
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:reset
--- -ver:   1.3.10
--- -ret:   self
--- Resets the argmatcher to an empty state.  All flags, arguments, and settings
--- are cleared and reset back to a freshly-created state.
---
--- See <a href="#adaptive-argmatchers">Adaptive Argmatchers</a> for more
--- information.
function _argmatcher:reset()
    if self._is_flag_matcher then
        error("Cannot reset a flag matcher (it is internal and not exposed)")
    end
    self._args = {}
    self._flags = nil
    self._flagprefix = {}
    self._descriptions = {}
    self._nextargindex = 1
    self._loop = nil
    self._no_file_generation = nil
    self._hidden = nil
    self._classify_func = nil
    self._init_coroutine = nil
    self._init_generation = nil
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addarg
--- -ver:   1.0.0
--- -arg:   choices...:string|table
--- -ret:   self
--- This adds argument matches.  Arguments can be a string, a string linked to
--- another parser by the concatenation operator, a table of arguments, or a
--- function that returns a table of arguments.  See
--- <a href="#argumentcompletion">Argument Completion</a> for more information.
--- -show:  local my_parser = clink.argmatcher("git")
--- -show:  :addarg("add", "status", "commit", "checkout")
function _argmatcher:addarg(...)
    local list = self._args[self._nextargindex]
    if not list then
        list = { _links = {} }
        table.insert(self._args, list)
        self._nextargindex = #self._args
    end
    self._nextargindex = self._nextargindex + 1

    self:_add(list, {...})
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addargunsorted
--- -ver:   1.3.3
--- -arg:   choices...:string|table
--- -ret:   self
--- This is the same as <a href="#_argmatcher:addarg">_argmatcher:addarg</a>
--- except that this disables sorting the matches.
function _argmatcher:addargunsorted(...)
    local list = self._args[self._nextargindex]
    self:addarg(...)
    list.nosort = true
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addflags
--- -ver:   1.0.0
--- -arg:   flags...:string
--- -ret:   self
--- This adds flag matches.  Flags are separate from arguments:  When listing
--- possible completions for an empty word, only arguments are listed.  But when
--- the word being completed starts with the first character of any of the
--- flags, then only flags are listed.  See
--- <a href="#argumentcompletion">Argument Completion</a> for more information.
--- -show:  local my_parser = clink.argmatcher("git")
--- -show:  :addarg({ "add", "status", "commit", "checkout" })
--- -show:  :addflags("-a", "-g", "-p", "--help")
function _argmatcher:addflags(...)
    local flag_matcher = self._flags or _argmatcher()
    local list = flag_matcher._args[1] or { _links = {} }
    local prefixes = self._flagprefix or {}

    flag_matcher:_add(list, {...}, prefixes)

    flag_matcher._is_flag_matcher = true
    flag_matcher._args[1] = list
    self._flags = flag_matcher

    if self._deprecated and not prefixes["-"] then
        prefixes["-"] = 0
    end

    self._flagprefix = prefixes
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addflagsunsorted
--- -ver:   1.3.3
--- -arg:   flags...:string
--- -ret:   self
--- This is the same as <a href="#_argmatcher:addflags">_argmatcher:addflags</a>
--- except that this also disables sorting for flags.
function _argmatcher:addflagsunsorted(...)
    self:addflags(...)
    self._flags._args[1].nosort = true
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:hideflags
--- -ver:   1.3.3
--- -arg:   flags...:string
--- -ret:   self
--- This hides the specified flags when displaying possible completions (the
--- flags are still recognized).
---
--- This is intended for use when there are several synonyms for a flag, so that
--- input coloring and linked argmatchers work, without cluttering the possible
--- completion list.
--- -show:  local dirs = clink.argmatcher():addarg(clink.dirmatches)
--- -show:  local my_parser = clink.argmatcher("mycommand")
--- -show:  :addflags("-a", "--a", "--al", "--all",
--- -show:            "-d"..dirs, "--d"..dirs, "--di"..dirs, "--dir"..dirs)
--- -show:  :hideflags("--a", "--al", "--all",      -- Only "-a" is displayed.
--- -show:             "-d", "--d", "--di")         -- Only "--dir" is displayed.
function _argmatcher:hideflags(...)
    local flag_matcher = self._flags or _argmatcher()
    local list = flag_matcher._hidden or {}

    flag_matcher:_hide(list, {...})
    flag_matcher._is_flag_matcher = true
    flag_matcher._hidden = list
    self._flags = flag_matcher
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:loop
--- -ver:   0.4.9
--- -arg:   [index:integer]
--- -ret:   self
--- This makes the parser loop back to argument position
--- <span class="arg">index</span> when it runs out of positional sets of
--- arguments (if <span class="arg">index</span> is omitted it loops back to
--- argument position 1).
--- -show:  clink.argmatcher("xyzzy")
--- -show:  :addarg("zero", "cero")     -- first arg can be zero or cero
--- -show:  :addarg("one", "uno")       -- second arg can be one or uno
--- -show:  :addarg("two", "dos")       -- third arg can be two or dos
--- -show:  :loop(2)    -- fourth arg loops back to position 2, for one or uno, and so on
function _argmatcher:loop(index)
    self._loop = index or -1
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setflagprefix
--- -ver:   1.0.0
--- -arg:   [prefixes...:string]
--- -ret:   self
--- This is almost never needed, because <code>:addflags()</code> automatically
--- identifies flag prefix characters.
---
--- However, any flags generated by functions can't influence the automatic
--- flag prefix character(s) detection.  So in some cases it may be necessary to
--- directly set the flag prefix.
---
--- <strong>Note:</strong> <code>:setflagprefix()</code> behaves differently in
--- different versions of Clink:
--- <table>
--- <tr><th>Version</th><th>Description</th></tr>
--- <tr><td>v1.0.0 through v1.1.3</td><td>Sets the flag prefix characters.</td></tr>
--- <tr><td>v1.1.4 through v1.2.35</td><td>Only sets flag prefix characters in an argmatcher created using the deprecated <a href="#clink.arg.register_parser">clink.arg.register_parser()</a> function.  Otherwise it has no effect.</td></tr>
--- <tr><td>v1.2.36 through v1.3.8</td><td>Does nothing.</td></tr>
--- <tr><td>v1.3.9 onward</td><td>Adds flag prefix characters, in addition to the ones automatically identified.</td></tr>
--- </table>
--- -show:  local function make_flags()
--- -show:  &nbsp;   return { '-a', '-b', '-c' }
--- -show:  end
--- -show:
--- -show:  clink.argmatcher('some_command')
--- -show:  :addflags(make_flags)   -- Only a function is added, so flag prefix characters cannot be determined automatically.
--- -show:  :setflagprefix('-')     -- Force '-' to be considered as a flag prefix character.
function _argmatcher:setflagprefix(...)
    for _, i in ipairs({...}) do
        if type(i) ~= "string" or #i ~= 1 then
            error("Flag prefixes must be single character strings", 2)
        end
        if not self._flagprefix[i] or self._flagprefix[i] == 0 then
            self._flagprefix[i] = 1
        end
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setflagsanywhere
--- -ver:   1.3.12
--- -arg:   anywhere:boolean
--- -ret:   self
--- When <span class="arg">anywhere</span> is false, flags are only recognized
--- until an argument is encountered.  Otherwise they are recognized anywhere
--- (which is the default).
function _argmatcher:setflagsanywhere(anywhere)
    if anywhere then
        self._flagsanywhere = true
    else
        self._flagsanywhere = false
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setendofflags
--- -ver:   1.3.12
--- -arg:   [endofflags:string|boolean]
--- -ret:   self
--- When <span class="arg">endofflags</span> is a string, it is a special flag
--- that signals the end of flags.  When <span class="arg">endflags</span> is
--- true or nil, then "<code>--</code>" is used as the end of flags string.
--- Otherwise, the end of flags string is cleared.
function _argmatcher:setendofflags(endofflags)
    if endofflags == true or endofflags == nil then
        endofflags = "--"
    elseif type(endofflags) ~= "string" then
        endofflags = nil
    end
    if endofflags then
        add_prefix(self._flagprefix, endofflags)
    end
    self._endofflags = endofflags
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:adddescriptions
--- -ver:   1.2.38
--- -arg:   [descriptions...:table]
--- -ret:   self
--- Adds descriptions for arg matches and/or flag matches.  Descriptions are
--- displayed for their associated args or flags whenever possible completions
--- are listed, for example by the <code>complete</code> or
--- <code>clink-select-complete</code> or <code>possible-completions</code>
--- commands.
---
--- Any number of descriptions tables may be passed to the function, and each
--- table must use one of the following schemes:
--- <ul>
--- <li>One or more string values that are args or flags, and a
--- <code>description</code> field that is the associated description string.
--- <li>Key/value pairs where each key is an arg or flag, and its value is
--- either a description string or a table containing an optional arguments
--- string and a description string.  If an arguments string is provided, it is
--- appended to the arg or flag string when listing possible completions.  For
--- example, <code>["--user"] = { " name", "Specify username"}</code> gets
--- printed as:
---
--- <pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black">
--- <span style="color:#c0c0c0">--user</span> <span style="color:#808000">name</span>&nbsp;&nbsp;&nbsp;&nbsp;<span style="color:#00ffff">Specify username</span>
--- </code></pre>
--- </ul>
---
--- -show:  local foo = clink.argmatcher("foo")
--- -show:  foo:addflags("-h", "--help", "--user")
--- -show:  foo:addarg("info", "set")
--- -show:  -- Example using first scheme and one table per description:
--- -show:  foo:adddescriptions(
--- -show:  &nbsp;   { "-h", "--help",   description = "Show help" },
--- -show:  &nbsp;   { "--user",         description = "Specify user name" },
--- -show:  &nbsp;   { "info",           description = "Prints information" },
--- -show:  &nbsp;   { "set",            description = "Show or change settings" },
--- -show:  )
--- -show:  -- Example using second scheme and just one table:
--- -show:  foo:adddescriptions( {
--- -show:  &nbsp;   ["-h"]              = "Show help",
--- -show:  &nbsp;   ["--help"]          = "Show help",
--- -show:  &nbsp;   ["--user"]          = { " name", "Specify user name" },
--- -show:  &nbsp;   ["info"]            = { "Prints information" },
--- -show:  &nbsp;   ["set"]             = { " var[=value]", "Show or change settings" },
--- -show:  } )
--- You can make your scripts backward compatible with older Clink versions by
--- adding a helper function.  The following is the safest and simplest way to
--- support backward compatibility:
--- -show:  -- Helper function to add descriptions, when possible.
--- -show:  local function maybe_adddescriptions(matcher, ...)
--- -show:  &nbsp;   if matcher and matcher.adddescriptions then
--- -show:  &nbsp;       matcher:adddescriptions(...)
--- -show:  &nbsp;   end
--- -show:  end
--- -show:
--- -show:  -- This adds descriptions only if the Clink version being used
--- -show:  -- supports them, otherwise it does nothing.
--- -show:  maybe_adddescriptions(foo, {
--- -show:  &nbsp;   ["-h"] = "Show help",
--- -show:  &nbsp;   -- etc
--- -show:  })
function _argmatcher:adddescriptions(...)
    self._descriptions = self._descriptions or {}
    for _,t in ipairs({...}) do
        if type(t) ~= "table" then
            error("bad argument #".._.." (must be a table)")
        end
        if t[1] then
            local desc = t["description"]
            if type(desc) == "table" then
                if type(desc[1]) ~= "string" then
                    error("bad argument #".._.." (descriptions table starting with '"..tostring(t[1]).."' does not have a string at index 1")
                elseif desc[2] and type(desc[2]) ~= "string"  then
                    error("bad argument #".._.." (descriptions table starting with '"..tostring(t[1]).."' has a non-string at index 2")
                end
            elseif type(desc) ~= "string" then
                error("bad argument #".._.." (descriptions table starting with '"..tostring(t[1]).."' is missing a 'description' field)")
            end
            for _,key in ipairs(t) do
                self._descriptions[key] = desc
            end
        else
            for key,desc in pairs(t) do
                self._descriptions[key] = desc
            end
        end
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:nofiles
--- -ver:   1.0.0
--- -ret:   self
--- This makes the parser prevent invoking <a href="#matchgenerators">match
--- generators</a>.  You can use it to "dead end" a parser and suggest no
--- completions.
function _argmatcher:nofiles()
    self._no_file_generation = true
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setclassifier
--- -ver:   1.1.18
--- -arg:   func:function
--- -ret:   self
--- This registers a function that gets called for each word the argmatcher
--- handles, to classify the word as part of coloring the input text.  See
--- <a href="#classifywords">Coloring the Input Text</a> for more information.
function _argmatcher:setclassifier(func)
    self._classify_func = func
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setdelayinit
--- -ver:   1.3.10
--- -arg:   func:function
--- -ret:   self
--- This registers a function that gets called the first time the argmatcher is
--- used in each edit line session.  See
--- <a href="#adaptive-argmatchers">Adaptive Argmatchers</a> for more
--- information.
function _argmatcher:setdelayinit(func)
    self._delayinit_func = func
    return self
end

--------------------------------------------------------------------------------
function _argmatcher.__concat(lhs, rhs)
    if getmetatable(rhs) ~= _argmatcher then
        error("Right-hand side must be an argmatcher object", 2)
    end

    local t = type(lhs)
    if t == "string" then
        return _arglink(lhs, rhs)
    end

    if t == "table" then
        local ret = {}
        for _, i in ipairs(lhs) do
            table.insert(ret, i .. rhs)
        end
        return ret
    end

    error("Left-hand side must be a string or a table of strings", 2)
end

--------------------------------------------------------------------------------
function _argmatcher:__call(arg)
    if type(arg) ~= "table" then
        error("Shorthand matcher arguments must be tables", 2)
    end

    local is_flag
    is_flag = function(x)
        local is_link = (getmetatable(x) == _arglink)
        if type(x) == "table" and not is_link then
            return is_flag(x[1])
        end

        if is_link then
            x = x._key
        end

        if self:_is_flag(tostring(x)) then
            return true
        end

        if x then
            local first_char = x:sub(1, 1)
            if first_char and first_char:match("[-/]") then
                return true
            end
        end

        return false
    end

    if is_flag(arg[1]) then
        return self:addflags(table.unpack(arg))
    end

    return self:addarg(table.unpack(arg))
end

--------------------------------------------------------------------------------
function _argmatcher:_is_flag(word)
    local first_char = word:sub(1, 1)

    if first_char == "/" and clink.translateslashes() == 2 then
        -- When slash translation is set to forward slashes, then disable
        -- recognizing forward slash as a flag character so that path completion
        -- can work.  See https://github.com/chrisant996/clink/issues/114.
        return false
    end

    for i, num in pairs(self._flagprefix) do
        if first_char == i then
            return num > 0
        end
    end

    return false
end

--------------------------------------------------------------------------------
local function get_sub_parser(argument, str)
    if argument._links then
        for key, matcher in pairs(argument._links) do
            if key == str then
                return matcher
            end
        end
    end
end

--------------------------------------------------------------------------------
local function merge_parsers(lhs, rhs)
    -- Merging parsers is not a trivial matter and this implementation is far
    -- from correct.  It behaves reasonably for common cases.

    -- Merge flags.
    if rhs._flags then
        lhs:addflags(rhs._flags._args[1])
    end

    -- Get the first argument in RHS.  Merging is only applied to the first
    -- argument.
    local rhs_arg_1 = rhs._args[1]
    if rhs_arg_1 == nil then
        return
    end

    -- Get reference to the LHS's first argument table (creating it if needed).
    local lhs_arg_1 = lhs._args[1]
    if lhs_arg_1 == nil then
        lhs_arg_1 = {}
        lhs._args[1] = lhs_arg_1
    end

    -- Link RHS to LHS through sub-parsers.
    local rlinks = rhs_arg_1._links or {}
    for _, rarg in ipairs(rhs_arg_1) do
        local key
        if type(key) == "table" then
            key = key.match
        end
        if not key then
            key = rarg
        end

        local child = rlinks[key]
        if child then
            -- If LHS's first argument has rarg in it which links to a sub-parser
            -- then we need to recursively merge them.
            local lhs_sub_parser = get_sub_parser(lhs_arg_1, key)
            if lhs_sub_parser then
                merge_parsers(lhs_sub_parser, child)
            else
                lhs:_add(lhs_arg_1, key .. child)
            end
        else
            lhs:_add(lhs_arg_1, rarg)
        end
    end

    -- Merge special directives.
    if rhs_arg_1.fromhistory then lhs_arg_1.fromhistory = rhs_arg_1.fromhistory end
    if rhs_arg_1.nosort then lhs_arg_1.nosort = rhs_arg_1.nosort end
    if rhs_arg_1.delayinit then lhs_arg_1.delayinit = rhs_arg_1.delayinit end

    -- Merge descriptions.
    if rhs._descriptions then
        local lhs_desc = lhs._descriptions
        if not lhs_desc then
            lhs_desc = {}
            lhs._descriptions = lhs_desc
        end
        for k,d in pairs(rhs._descriptions) do
            lhs_desc[k] = d
        end
    end
end

--------------------------------------------------------------------------------
local function merge_or_assign(lhs, rhs)
    if lhs then
        merge_parsers(lhs, rhs)
        return lhs
    else
        return rhs
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_add(list, addee, prefixes)
    -- If addee is a flag like --foo= and is not linked, then link it to a
    -- default parser so its argument doesn't get confused as an arg for its
    -- parent argmatcher.
    if prefixes and type(addee) == "string" and addee:match("[:=]$") then
        addee = addee..clink.argmatcher():addarg(clink.filematches)
    end

    -- Flatten out tables unless the table is a link
    local is_link = (getmetatable(addee) == _arglink)
    if type(addee) == "table" and not is_link and not addee.match then
        apply_options_to_list(addee, list)
        if getmetatable(addee) == _argmatcher then
            for _, i in ipairs(addee._args) do
                for _, j in ipairs(i) do
                    table.insert(list, j)
                    if prefixes then add_prefix(prefixes, j) end
                end
                if i._links then
                    for k, m in pairs(i._links) do
                        list._links[k] = merge_or_assign(list._links[k], m)
                        if prefixes then add_prefix(prefixes, k) end
                    end
                end
            end
        else
            for _, i in ipairs(addee) do
                self:_add(list, i, prefixes)
            end
        end
        return
    end

    if is_link then
        list._links[addee._key] = merge_or_assign(list._links[addee._key], addee._matcher)
        table.insert(list, addee._key) -- Necessary to maintain original unsorted order.
        if prefixes then add_prefix(prefixes, addee._key) end
    else
        table.insert(list, addee)
        if prefixes then add_prefix(prefixes, addee) end
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_hide(list, addee)
    -- Flatten out tables unless the table is a link
    local is_link = (getmetatable(addee) == _arglink)
    if type(addee) == "table" and not is_link and not addee.match then
        if getmetatable(addee) == _argmatcher then
            for _, i in ipairs(addee._args) do
                for _, j in ipairs(i) do
                    list[j] = true
                end
            end
        else
            for _, i in ipairs(addee) do
                self:_hide(list, i)
            end
        end
        return
    end

    if not is_link then
        list[addee] = true
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_generate(line_state, match_builder, extra_words)
    local reader = _argreader(self, line_state)

    --[[
    reader:starttracing(line_state:getword(1))
    --]]

    -- Consume extra words from expanded doskey alias.
    if extra_words then
        for word_index = 2, #extra_words do
            reader:update(extra_words[word_index], -1)
        end
    end

    -- Consume words and use them to move through matchers' arguments.
    local word_count = line_state:getwordcount()
    local command_word_index = line_state:getcommandwordindex()
    for word_index = command_word_index + 1, (word_count - 1) do
        local info = line_state:getwordinfo(word_index)
        if not info.redir then
            local word = line_state:getword(word_index)
            reader:update(word, word_index)
        end
    end

    -- There should always be a matcher left on the stack, but the arg_index
    -- could be well out of range.
    local matcher = reader._matcher
    local arg_index = reader._arg_index
    local match_type = ((not matcher._deprecated) and "arg") or nil
    local hidden

    local endword
    local endwordinfo = line_state:getwordinfo(line_state:getwordcount())
    if clink.use_old_filtering then
        endword = line_state:getline():sub(endwordinfo.offset, line_state:getcursor() - 1)
    else
        endword = line_state:getendword()
    end

    -- Are we left with a valid argument that can provide matches?
    local add_matches -- Separate decl and init to enable recursion.
    add_matches = function(arg, match_type)
        local descs = matcher._descriptions
        local is_arg_type = match_type == "arg"
        local make_match = function(key)
            local m = {}
            local t = type(key)
            if t == "string" or t == "number" then
                m.match = tostring(key)
            elseif t == "table" and key.match then
                for n,v in pairs(key) do
                    m[n] = v
                end
                t = type(m.match)
                if t == "number" then
                    m.match = tostring(m.match)
                elseif t ~= "string" then
                    return nil
                end
            else
                return nil
            end
            if descs then
                local d = descs[key]
                if type(d) ~= "table" then
                    m.description = m.description or d
                else
                    if #d > 1 then
                        m.display = m.display or d[1]
                        m.description = m.description or d[2]
                        m.appenddisplay = true
                    else
                        m.description = m.description or d[1]
                    end
                end
            end
            return m
        end

        apply_options_to_builder(reader, arg, match_builder)
        for _, i in ipairs(arg) do
            local t = type(i)
            if t == "function" then
                local j = i(endword, word_count, line_state, match_builder)
                if type(j) ~= "table" then
                    return j or false
                end

                apply_options_to_builder(reader, j, match_builder)
                match_builder:addmatches(j, match_type)
            elseif t == "string" or t == "number" then
                i = tostring(i)
                if not hidden or not hidden[i] then
                    match_builder:addmatch(make_match(i), match_type)
                end
            elseif t == "table" then
                if i.match ~= nil then
                    if not hidden or not hidden[i.match] then
                        match_builder:addmatch(make_match(i), match_type)
                    end
                else
                    add_matches(i, match_type)
                end
            end
        end

        return true
    end

    -- Backward compatibility shim.
    if rl_state then
        rl_state.first = endwordinfo.offset
        rl_state.last = line_state:getcursor()
        rl_state.text = line_state:getline():sub(rl_state.first, rl_state.last - 1)
    end

    -- Select between adding flags or matches themselves. Works in conjunction
    -- with getwordbreakinfo()'s return.
    if endwordinfo.redir then
        -- The word is an argument to a redirection symbol, so generate file
        -- matches.
        match_builder:addmatches(clink.filematches(line_state:getendword()))
        return true
    elseif matcher._flags and matcher:_is_flag(line_state:getendword()) then
        -- Flags are always "arg" type, which helps differentiate them from
        -- filename completions even when using _deprecated matcher mode, so
        -- that path normalization can avoid affecting flags like "/c", etc.
        hidden = matcher._flags._hidden
        add_matches(matcher._flags._args[1], "arg")
        return true
    else
        -- When endword is adjacent the previous word and the previous word ends
        -- with : or = then this is a flag-attached arg, not an arg position.
        if line_state:getwordcount() > 1 then
            local prevwordinfo = line_state:getwordinfo(line_state:getwordcount() - 1)
            if prevwordinfo.offset + prevwordinfo.length == endwordinfo.offset and
                    matcher:_is_flag(line_state:getword(line_state:getwordcount() - 1)) and
                    line_state:getline():sub(endwordinfo.offset - 1, endwordinfo.offset - 1):find("[:=]") then
                match_builder:addmatches(clink.filematches(line_state:getendword()))
                return true
            end
        end
        local arg = matcher._args[arg_index]
        if arg then
            return add_matches(arg, match_type) and true or false
        end
    end

    -- No valid argument. Decide if we should match files or not.
    local no_files = false
    if matcher._no_file_generation then
        -- Don't match files if :nofiles() was explicitly used.
        no_files = true
    elseif #matcher._args == 0 and not matcher._flags then
        -- A completely empty argmatcher is a synonym for :nofiles().  It's
        -- empty if neither :addarg() nor :addflags() have been called on it
        -- (this is not the same thing as having an empty arg list for an arg
        -- index slot, nor as having an empty flags list).  This nuance is
        -- important so that it's possible to generate flag matches without
        -- losing the ability to generate file matches.
        no_files = true
    end
    return no_files
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:add_arguments
--- -deprecated: _argmatcher:addarg
--- Adds one argument slot per table passed to it (as v0.4.9 did).
---
--- <strong>Note:</strong> v1.3.10 and lower <code>:add_arguments()</code>
--- mistakenly added all arguments into the first argument slot.
function _argmatcher:add_arguments(...)
    for _,arg in pairs({...}) do
        self:addarg(arg)
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:add_flags
--- -deprecated: _argmatcher:addflags
function _argmatcher:add_flags(...)
    self:addflags(...)
    return self
end

--------------------------------------------------------------------------------
-- Deprecated.  This was an undocumented function, but some scripts found it and
-- used it anyway.  The compatibility shim tries to make them work essentially
-- the same as in 0.4.8, but it may not be exactly accurate.
function _argmatcher:flatten_argument(index)
    local t = {}

    if index > 0 and index <= #self._args then
        local args = self._args[index]
        for _, i in ipairs(args) do
            if type(i) == "string" then
                table.insert(t, i)
            end
        end
        if args._links then
            for k, _ in pairs(args._links) do
                table.insert(t, k)
            end
        end
    end

    return t
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:set_arguments
--- -deprecated: _argmatcher:addarg
--- -arg:   choices...:string|table
--- -ret:   self
--- Sets one argument slot per table passed to it (as v0.4.9 did).
---
--- <strong>Note:</strong> v1.3.10 and lower <code>:add_arguments()</code>
--- mistakenly set all arguments into the first argument slot.
function _argmatcher:set_arguments(...)
    self._args = { _links = {} }
    for _,arg in pairs({...}) do
        self:addarg(arg)
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:set_flags
--- -deprecated: _argmatcher:addflags
--- -arg:   flags...:string
--- -ret:   self
--- Note:  The new API has no way to remove flags that were previously added, so
--- converting from <code>:set_flags()</code> to <code>:addflags()</code> may
--- require the calling script to reorganize how and when it adds flags.
function _argmatcher:set_flags(...)
    self._flags = nil
    self:addflags(...)
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:disable_file_matching
--- -deprecated: _argmatcher:nofiles
--- -ret:   self
function _argmatcher:disable_file_matching()
    self:nofiles()
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:be_precise
--- -deprecated:
--- -ret:   self
function _argmatcher:be_precise()
    _compat_warning("warning: ignoring _argmatcher:be_precise()", " -- this is not supported yet")
    return self
end



--------------------------------------------------------------------------------
clink = clink or {}
local _argmatchers = {}

if settings.get("lua.debug") or clink.DEBUG then
    clink.debug = clink.debug or {}
    clink.debug._argmatchers = _argmatchers
end

--------------------------------------------------------------------------------
--- -name:  clink.argmatcher
--- -ver:   1.0.0
--- -arg:   [priority:integer]
--- -arg:   commands...:string
--- -ret:   <a href="#_argmatcher">_argmatcher</a>
--- Creates and returns a new argument matcher parser object.  Use
--- <a href="#_argmatcher:addarg">:addarg()</a> and etc to add arguments, flags,
--- other parsers, and more.  See <a href="#argumentcompletion">Argument
--- Completion</a> for more information.
---
--- If one <span class="arg">command</span> is provided and there is already an
--- argmatcher for it, then this returns the existing parser rather than
--- creating a new parser.  Using :addarg() starts at arg position 1, making it
--- possible to merge new args and etc into the existing parser.
---
--- <strong>Note:</strong>  Merging <a href="#linked-parsers">linked
--- argmatchers</a> only merges the first argument position.  The merge is
--- simple, but should be sufficient for common simple cases.
function clink.argmatcher(...)
    -- Extract priority from the arguments.
    local priority = 999
    local input = {...}
    if (#input > 0) and (type(input[1]) == "number") then
        priority = input[1]
        table.remove(input, 1)
    end

    -- If multiple commands are listed, merging isn't supported.
    local matcher = nil
    for _, i in ipairs(input) do
        matcher = _argmatchers[clink.lower(i)]
        if #input <= 1 then
            break
        end
        if matcher then
            error("Command '"..i.."' already has an argmatcher; clink.argmatcher() with multiple commands fails if any of the commands already has an argmatcher.")
            return
        end
    end

    if matcher then
        -- Existing matcher; use the smaller of the old and new priorities.
        if matcher._priority > priority then
            matcher._priority = priority
        end
        matcher._nextargindex = 1 -- so the next :addarg() affects position 1
    else
        -- No existing matcher; create a new matcher and set the priority.
        matcher = _argmatcher()
        matcher._priority = priority
        for _, i in ipairs(input) do
            _argmatchers[clink.lower(i)] = matcher
        end
    end

    return matcher
end

--------------------------------------------------------------------------------
--- -name:  clink.dirmatches
--- -ver:   1.1.18
--- -arg:   word:string
--- -ret:   table
--- You can use this function in an argmatcher to supply directory matches.
--- This automatically handles Readline tilde completion.
--- -show:  -- Make "cd" generate directory matches (no files).
--- -show:  clink.argmatcher("cd")
--- -show:  :addflags("/d")
--- -show:  :addarg({ clink.dirmatches })
function clink.dirmatches(match_word)
    local word, expanded = rl.expandtilde(match_word)

    local root = (path.getdirectory(word) or ""):gsub("/", "\\")
    if expanded then
        root = rl.collapsetilde(root)
    end

    local _, ismain = coroutine.running()

    local matches = {}
    for _, i in ipairs(os.globdirs(word.."*", true)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
        if not ismain and _ % 250 == 0 then
            coroutine.yield()
        end
    end
    return matches
end

--------------------------------------------------------------------------------
--- -name:  clink.filematches
--- -ver:   1.1.18
--- -arg:   word:string
--- -ret:   table
--- You can use this function in an argmatcher to supply file matches.  This
--- automatically handles Readline tilde completion.
---
--- Argmatchers default to matching files, so it's unusual to need this
--- function.  However, some exceptions are when a flag needs to accept file
--- matches but other flags and arguments don't, or when matches need to include
--- more than files.
--- -show:  -- Make "foo --file" generate file matches, but other flags and args don't.
--- -show:  -- And the third argument can be a file or $stdin or $stdout.
--- -show:  clink.argmatcher("foo")
--- -show:  :addflags(
--- -show:  &nbsp;   "--help",
--- -show:  &nbsp;   "--file"..clink.argmatcher():addarg({ clink.filematches })
--- -show:  )
--- -show:  :addarg({ "one", "won" })
--- -show:  :addarg({ "two", "too" })
--- -show:  :addarg({ clink.filematches, "$stdin", "$stdout" })
function clink.filematches(match_word)
    local word, expanded = rl.expandtilde(match_word)

    local root = (path.getdirectory(word) or ""):gsub("/", "\\")
    if expanded then
        root = rl.collapsetilde(root)
    end

    local _, ismain = coroutine.running()

    local matches = {}
    for _, i in ipairs(os.globfiles(word.."*", true)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
        if not ismain and _ % 250 == 0 then
            coroutine.yield()
        end
    end
    return matches
end



--------------------------------------------------------------------------------
local function _has_argmatcher(command_word)
    command_word = clink.lower(command_word)

    -- Check for an exact match.
    local argmatcher = _argmatchers[path.getname(command_word)]
    if argmatcher then
        return argmatcher
    end

    -- If the extension is in PATHEXT then try stripping the extension.
    if path.isexecext(command_word) then
        argmatcher = _argmatchers[path.getbasename(command_word)]
        if argmatcher then
            return argmatcher
        end
    end
end

--------------------------------------------------------------------------------
local function _do_onuse_callback(argmatcher, command_word)
    -- Don't init while generating matches from history, as that could be
    -- excessively expensive (could run thousands of callbacks).
    if _argmatcher_fromhistory and _argmatcher_fromhistory.argmatcher then
        return
    end

    if (argmatcher._onuse_generation or 0) < _delayinit_generation then
        argmatcher._onuse_generation = _delayinit_generation
    else
        return
    end

    local _, ismain = coroutine.running()
    local async_delayinit = not ismain or not clink._in_generate()

    -- Start the delay init callback if it hasn't already started.
    local c = argmatcher._onuse_coroutine
    if not c then
        -- Run the delayinit callback in a coroutine so typing is responsive.
        c = coroutine.create(function ()
            argmatcher._delayinit_func(argmatcher, command_word)
            argmatcher._onuse_coroutine = nil
            _clear_onuse_coroutine[argmatcher] = nil
            if async_delayinit then
                clink.reclassifyline()
            end
        end)
        argmatcher._onuse_coroutine = c

        -- Set up to be able to efficiently clear dangling coroutine references,
        -- e.g. in case a coroutine doesn't finish before a new edit line.
        _clear_onuse_coroutine[argmatcher] = argmatcher
    end

    -- Finish (run) the coroutine immediately only when the main coroutine is
    -- generating matches.
    if not async_delayinit then
        clink._finish_coroutine(c)
    else
        -- Run the coroutine up to the first yield, so that if it doesn't need
        -- to yield at all then it completes right now.
        coroutine.resume(c)
    end
end

--------------------------------------------------------------------------------
-- Finds an argmatcher for the first word and returns:
--  argmatcher  = The argmatcher, unless there are too few words to use it.
--  exists      = True if argmatcher exists (even if too few words to use it).
--  words       = Table of words to run through reader before continuing.
local function _find_argmatcher(line_state, check_existence)
    -- Running an argmatcher only makes sense if there's two or more words.
    local word_count = line_state:getwordcount()
    local command_word_index = line_state:getcommandwordindex()
    if word_count < command_word_index + (check_existence and 0 or 1) then
        return
    end
    if word_count > command_word_index then
        check_existence = nil
    end

    local command_word = line_state:getword(command_word_index)
    local argmatcher = _has_argmatcher(command_word)
    if argmatcher then
        if check_existence then
            argmatcher = nil
        elseif argmatcher._delayinit_func then
            _do_onuse_callback(argmatcher, command_word)
        end
        return argmatcher, true
    end

    if command_word_index == 1 then
        local info = line_state:getwordinfo(1)
        local next_ofs = info.offset + info.length
        local next_char = line_state:getline():sub(next_ofs, next_ofs)
        if next_char == "" or next_char == " " or next_char == "\t" then
            local alias = os.getalias(command_word)
            if alias and alias ~= "" then
                -- This doesn't even try to handle redirection symbols in the alias
                -- because the cost/benefit ratio is unappealing.
                alias = alias:gsub("%$.*$", "")
                local words = string.explode(alias, " \t", '"')
                if #words > 0 then
                    argmatcher = _has_argmatcher(words[1])
                    if argmatcher then
                        if check_existence then
                            argmatcher = nil
                        elseif argmatcher._delayinit_func then
                            _do_onuse_callback(argmatcher, words[1])
                        end
                        return argmatcher, true, words
                    end
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
function clink._generate_from_historyline(line_state)
    local argmatcher, has_argmatcher, extra_words = _find_argmatcher(line_state)
    if not argmatcher or argmatcher ~= _argmatcher_fromhistory_root then
        return
    end

    local reader = _argreader(argmatcher, line_state)
    reader._fromhistory_matcher = _argmatcher_fromhistory.argmatcher
    reader._fromhistory_argindex = _argmatcher_fromhistory.argslot

    -- Consume extra words from expanded doskey alias.
    if extra_words then
        for word_index = 2, #extra_words do
            reader:update(extra_words[word_index], -1)
        end
    end

    -- Consume words and use them to move through matchers' arguments.
    local word_count = line_state:getwordcount()
    local command_word_index = line_state:getcommandwordindex()
    for word_index = command_word_index + 1, word_count do
        local info = line_state:getwordinfo(word_index)
        if not info.redir then
            local word = line_state:getword(word_index)
            reader:update(word, word_index)
        end
    end
end



--------------------------------------------------------------------------------
clink.argmatcher_generator_priority = 24
local argmatcher_generator = clink.generator(clink.argmatcher_generator_priority)
local argmatcher_classifier = clink.classifier(clink.argmatcher_generator_priority)

--------------------------------------------------------------------------------
function argmatcher_generator:generate(line_state, match_builder)
    local argmatcher, has_argmatcher, extra_words = _find_argmatcher(line_state)
    if argmatcher then
        _argmatcher_fromhistory = {}
        _argmatcher_fromhistory_root = argmatcher
        local ret = argmatcher:_generate(line_state, match_builder, extra_words)
        _argmatcher_fromhistory = {}
        _argmatcher_fromhistory_root = nil
        return ret
    end

    return false
end

--------------------------------------------------------------------------------
function argmatcher_generator:getwordbreakinfo(line_state)
    local argmatcher, has_argmatcher, extra_words = _find_argmatcher(line_state)
    if argmatcher then
        local reader = _argreader(argmatcher, line_state)

        -- Consume extra words from expanded doskey alias.
        if extra_words then
            for word_index = 2, #extra_words do
                reader:update(extra_words[word_index], -1)
            end
        end

        -- Consume words and use them to move through matchers' arguments.
        local word_count = line_state:getwordcount()
        local command_word_index = line_state:getcommandwordindex()
        for word_index = command_word_index + 1, (word_count - 1) do
            local info = line_state:getwordinfo(word_index)
            if not info.redir then
                local word = line_state:getword(word_index)
                reader:update(word, word_index)
            end
        end

        -- There should always be a matcher left on the stack, but the arg_index
        -- could be well out of range.
        argmatcher = reader._matcher
        if argmatcher and argmatcher._flags then
            local word = line_state:getendword()
            if argmatcher:_is_flag(word) then
                -- Accommodate `-flag:text` and `-flag=text` (with or without
                -- quotes) so that matching can happen for the `text` portion.
                local attached_arg,attach_pos = word:find("^[^:=][^:=]+[:=]")
                if attached_arg then
                    return attach_pos, 0
                end
                return 0, 1
            end
        end
    end

    return 0
end

--------------------------------------------------------------------------------
function argmatcher_classifier:classify(commands)
    local unrecognized_color = settings.get("color.unrecognized") ~= ""
    local executable_color = settings.get("color.executable") ~= ""
    for _,command in ipairs(commands) do
        local line_state = command.line_state
        local word_classifier = command.classifications

        local argmatcher, has_argmatcher, extra_words = _find_argmatcher(line_state, true)
        local command_word_index = line_state:getcommandwordindex()

        local word_count = line_state:getwordcount()
        local command_word = line_state:getword(command_word_index) or ""
        if #command_word > 0 then
            local info = line_state:getwordinfo(command_word_index)
            local m = has_argmatcher and "m" or ""
            if info.alias then
                word_classifier:classifyword(command_word_index, m.."d", false); --doskey
            elseif clink.is_cmd_command(command_word) then
                word_classifier:classifyword(command_word_index, m.."c", false); --command
            elseif unrecognized_color or executable_color then
                local cl
                local recognized = clink._recognize_command(line_state:getline(), command_word, info.quoted)
                if recognized < 0 then
                    cl = unrecognized_color and "u"                              --unrecognized
                elseif recognized > 0 then
                    cl = executable_color and "x"                                --executable
                end
                cl = cl or "o"                                                   --other
                word_classifier:classifyword(command_word_index, m..cl, false);
            else
                word_classifier:classifyword(command_word_index, m.."o", false); --other
            end
        end

        if argmatcher then
            local reader = _argreader(argmatcher, line_state)
            reader._word_classifier = word_classifier

            -- Consume extra words from expanded doskey alias.
            if extra_words then
                for word_index = 2, #extra_words do
                    reader:update(extra_words[word_index], -1)
                end
            end

            -- Consume words and use them to move through matchers' arguments.
            for word_index = command_word_index + 1, word_count do
                local info = line_state:getwordinfo(word_index)
                if not info.redir then
                    local word = line_state:getword(word_index)
                    reader:update(word, word_index)
                end
            end
        end
    end

    return false -- continue
end



--------------------------------------------------------------------------------
clink.arg = clink.arg or {}

--------------------------------------------------------------------------------
local function starts_with_flag_character(parser, part)
    if part == nil then
        return false
    end

    -- For compatibility, always consider '-' and '/' to be flag prefix
    -- characters while initializing a parser.  This is important so that
    -- `parser('-x'..parser(...))` works.
    local prefix = part:sub(1, 1)
    if prefix == "-" or prefix == "/" then
        return true
    end

    local num_with_prefix = parser._flagprefix[prefix]
    return num_with_prefix and (num_with_prefix > 0) and true or false
end

--------------------------------------------------------------------------------
local function parser_initialise(parser, ...)
    parser._nextargindex = 1
    for _, word in ipairs({...}) do
        local t = type(word)
        if t == "string" then
            parser:addflags(word)
        elseif t == "table" then
            if getmetatable(word) == _arglink and starts_with_flag_character(parser, word._key) then
                parser:addflags(word)
            else
                parser:addarg(word)
            end
        else
            error("Additional arguments to new_parser() must be tables or strings", 2)
        end
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.arg.new_parser
--- -deprecated: clink.argmatcher
--- -arg:   ...
--- -ret:   table
--- Creates a new parser and adds <span class="arg">...</span> to it.
--- -show:  -- Deprecated form:
--- -show:  local parser = clink.arg.new_parser(
--- -show:  &nbsp; { "abc", "def" },       -- arg position 1
--- -show:  &nbsp; { "ghi", "jkl" },       -- arg position 2
--- -show:  &nbsp; "--flag1", "--flag2"    -- flags
--- -show:  )
--- -show:
--- -show:  -- Replace with form:
--- -show:  local parser = clink.argmatcher()
--- -show:  :addarg("abc", "def")               -- arg position 1
--- -show:  :addarg("ghi", "jkl")               -- arg position 2
--- -show:  :addflags("--flag1", "--flag2")     -- flags
function clink.arg.new_parser(...)
    local parser = clink.argmatcher()
    parser._deprecated = true
    parser._flagprefix = {}
    if ... then
        local success, msg = xpcall(parser_initialise, _error_handler_ret, parser, ...)
        if not success then
            error(msg, 2)
        end
    end
    return parser
end

--------------------------------------------------------------------------------
--- -name:  clink.arg.register_parser
--- -deprecated: clink.argmatcher
--- -arg:   cmd:string
--- -arg:   parser:table
--- -ret:   table
--- Adds <span class="arg">parser</span> to the argmatcher for
--- <span class="arg">cmd</span>.
---
--- If there is already an argmatcher for <span class="arg">cmd</span> then the
--- two argmatchers are merged.  It is only a simple merge; a more sophisticated
--- merge would be much slower and use much more memory.  The simple merge
--- should be sufficient for common simple cases.
---
--- <strong>Note:</strong> In v1.3.11, merging parsers should be a bit improved
--- compared to how v0.4.9 merging worked.  In v1.0 through v1.3.10, merging
--- parsers doesn't work very well.
--- -show:  -- Deprecated form:
--- -show:  local parser1 = clink.arg.new_parser():set_arguments({ "abc", "def" }, { "old_second" })
--- -show:  local parser2 = clink.arg.new_parser():set_arguments({ "ghi", "jkl" }, { "new_second" })
--- -show:  clink.arg.register_parser("foo", parser1)
--- -show:  clink.arg.register_parser("foo", parser2)
--- -show:  -- In v0.4.9 that syntax only merged the first argument position, and "ghi" and
--- -show:  -- "jkl" ended up with no further arguments.  In v1.3.11 and higher that syntax
--- -show:  -- ends up with "ghi" and "jkl" having only "old_second" as a second argument.
--- -show:
--- -show:  -- Replace with new form:
--- -show:  clink.argmatcher("foo"):addarg(parser1)
--- -show:  clink.argmatcher("foo"):addarg(parser2)
--- -show:  -- In v1.3.11 and higher this syntax ends up with all 4 first argument strings
--- -show:  -- having both "old_second" and "new_second" as a second argument.
function clink.arg.register_parser(cmd, parser)
    cmd = clink.lower(cmd)

    if parser and parser._deprecated then
        clink._mark_deprecated_argmatcher(cmd)
    end

    local matcher = _argmatchers[cmd]
    if matcher then
        -- Merge new parser (parser) into existing parser (matcher).
        local success, msg = xpcall(merge_parsers, _error_handler_ret, matcher, parser)
        if not success then
            error(msg, 2)
        end
        return matcher
    end

    -- Register the parser.
    _argmatchers[cmd] = parser
    return matcher
end
