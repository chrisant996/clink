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
local function dbg(...)
    if os.getenv("DEBUG_POP") then
        print(...)
    end
end
local function dbgdumpvar(...)
    if os.getenv("DEBUG_POP") then
        dumpvar(...)
    end
end



--------------------------------------------------------------------------------
local function make_dummy_builder()
    local dummy = {}
    function dummy:addmatch() end
    function dummy:addmatches() end
    function dummy:setappendcharacter() end
    function dummy:setsuppressappend() end
    function dummy:setsuppressquoting() end
    function dummy:setmatchesarefiles() end
    return dummy
end

--------------------------------------------------------------------------------
local _argreader = {}
_argreader.__index = _argreader
setmetatable(_argreader, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argreader._new(root, line_state)
dbg("--- new argreader ---")
    local reader = setmetatable({
        _matcher = root,
        _line_state = line_state,
        _arg_index = 1,
        _stack = {},
    }, _argreader)
    return reader
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
dbg("\nWORD", word)

    -- Check for flags and switch matcher if the word is a flag.
    local matcher = self._matcher
    local is_flag = matcher:_is_flag(word)
    local next_is_flag = matcher:_is_flag(line_state:getword(word_index + 1))
    local pushed_flags
    if is_flag then
        if matcher._flags then
dbg("PUSHING BECAUSE IS_FLAG")
            self:_push(matcher._flags)
            arg_match_type = "f" --flag
            pushed_flags = true
        else
dbg("stack depth", #self._stack, "(not matcher._flags)")
            return
        end
    end

    matcher = self._matcher -- Update matcher after possible _push.
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]
    local next_arg_index = arg_index + 1
dbg("is_flag", is_flag, "pushed_flags", pushed_flags, "next_arg_index", next_arg_index)

    -- If arg_index is out of bounds we should loop if set or return to the
    -- previous matcher if possible.
    if next_arg_index > #matcher._args then
dbg("out of bounds", next_arg_index, #matcher._args)
        if matcher._loop then
            self._arg_index = math.min(math.max(matcher._loop, 1), #matcher._args)
        else
            -- If next word is a flag, don't pop.  Flags are not positional, so
            -- a matcher can only be exhausted by a word that exceeds the number
            -- of argument slots the matcher has.
            -- if is_flag then
            --     self._arg_index = next_arg_index
dbg("next_is_flag", next_is_flag)
            if not pushed_flags and next_is_flag then
                -- Do nothing.
dbg("not pushed_flags and next_is_flag")
            elseif not self:_pop(pushed_flags, next_is_flag) then
                -- Popping must use the _arg_index as is, without incrementing
                -- (it was already incremented before it got pushed).
                self._arg_index = next_arg_index
            end
        end
    else
dbg("in bounds, update _arg_index", next_arg_index)
        self._arg_index = next_arg_index
    end

dbg("self._arg_index", self._arg_index)
dbg("self._matcher is '"..self._matcher:getdebugname().."'")
dbgdumpvar(self._matcher._args[self._arg_index], "self._matcher._args[self._arg_index]")
    -- Some matchers have no args at all.  Or ran out of args.
    if not arg then
        if self._word_classifier and word_index >= 0 then
            if matcher._no_file_generation then
                self._word_classifier:classifyword(word_index, "n", false)  --none
            else
                self._word_classifier:classifyword(word_index, "o", false)  --other
            end
        end
dbg("stack depth", #self._stack, "(not arg)")
        return
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
                    end
                end
                if not matched then
                    for _, i in ipairs(arg) do
                        if type(i) == "function" then
                            t = 'o' --other (placeholder; superseded by :classifyword).
                        elseif i == word then
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
    for key, linked in pairs(arg._links) do
        if key == word then
            if is_flag and word:match("[:=]$") and word_index >= 0 then
                local info = line_state:getwordinfo(word_index)
                if info and
                        line_state:getcursor() ~= info.offset + info.length and
                        line_state:getline():sub(info.offset + info.length, info.offset + info.length) == " " then
                    -- Don't follow linked parser on `--foo=` flag if there's a
                    -- space after the `:` or `=` unless the cursor is on the
                    -- space.
                    break
                end
            end
dbg("PUSHING BECAUSE LINKED", key)
            self:_push(linked)
            break
        end
    end
dbg("stack depth", #self._stack)
end

--------------------------------------------------------------------------------
function _argreader:_push(matcher)
dbg("push [ "..tostring(self._matcher)..", _arg_index "..self._arg_index.." ]")
    table.insert(self._stack, { self._matcher, self._arg_index })
    self._matcher = matcher
    self._arg_index = 1
end

--------------------------------------------------------------------------------
function _argreader:_pop(is_flag, next_is_flag)
    if #self._stack <= 0 then
        return false
    end

    while #self._stack > 0 do
        self._matcher, self._arg_index = table.unpack(table.remove(self._stack))
dbg("pop", "_matcher", self._matcher:getdebugname(), "_arg_index", self._arg_index)

        -- if is_flag then
        --     -- Pop only one level if it's a flag.  This balances one push with
        --     -- one pop, so that a flag matcher never stays on the stack.
        --     break
        -- end

        if self._matcher._loop then
dbg("  break: looping")
            -- Never pop a matcher that's looping.  It's looping!
            break
        end
        if self._arg_index + (next_is_flag and 0 or 1) <= #self._matcher._args then
dbg("  break: more args")
            -- If the matcher has arguments remaining, stop popping.
            break
        end

        is_flag = false
    end

    return true
end



--------------------------------------------------------------------------------
local _argmatcher = {}
_argmatcher.__index = _argmatcher
setmetatable(_argmatcher, { __call = function (x, ...) return x._new(...) end })

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

    flag_matcher._args[1] = list
    self._flags = flag_matcher

    if not self._deprecated then
        self._flagprefix = prefixes
    end
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
--- -deprecated: _argmatcher:addflags
--- -arg:   [prefixes...:string]
--- -ret:   self
--- This is no longer needed (and does nothing) because <code>:addflags()</code>
--- automatically identifies.
function _argmatcher:setflagprefix(...)
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
--- <a href="#classifywords">Coloring The Input Text</a> for more information.
function _argmatcher:setclassifier(func)
    self._classify_func = func
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
local function add_prefix(prefixes, string)
    if string and type(string) == "string" then
        local prefix = string:sub(1, 1)
        if prefix:len() > 0 then
            if prefix:match('[A-Za-z]') then
                error("flag string '"..string.."' is invalid because it starts with a letter and would interfere with argument matching.")
            else
                prefixes[prefix] = (prefixes[prefix] or 0) + 1
            end
        end
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
        if getmetatable(addee) == _argmatcher then
            for _, i in ipairs(addee._args) do
                for _, j in ipairs(i) do
                    table.insert(list, j)
                    if prefixes then add_prefix(prefixes, j) end
                end
                if i._links then
                    for k, m in pairs(i._links) do
                        if list._links[k] then
                            _compat_warning("warning: replacing arglink for '"..k.."'", " -- merging linked argmatchers was unreliable and is no longer supported")
                        end
                        list._links[k] = m
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
        if list._links[addee._key] then
            _compat_warning("warning: replacing arglink for '"..addee._key.."'", " -- merging linked argmatchers was unreliable and is no longer supported")
        end
        list._links[addee._key] = addee._matcher
        if prefixes then add_prefix(prefixes, addee._key) end
    else
        table.insert(list, addee)
        if prefixes then add_prefix(prefixes, addee) end
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_generate(line_state, match_builder, extra_words)
    local reader = _argreader(self, line_state)

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

    -- Are we left with a valid argument that can provide matches?
    local add_matches = function(arg, match_type)
        for key, _ in pairs(arg._links) do
            match_builder:addmatch(key, match_type)
        end

        for _, i in ipairs(arg) do
            if type(i) == "function" then
                local j = i(line_state:getendword(), word_count, line_state, match_builder)
                if type(j) ~= "table" then
                    return j or false
                end

                match_builder:addmatches(j, match_type)
            else
                match_builder:addmatch(i, match_type)
            end
        end

        return true
    end

    -- Select between adding flags or matches themselves. Works in conjunction
    -- with getwordbreakinfo()'s return.
    local endwordinfo = line_state:getwordinfo(line_state:getwordcount())
    if endwordinfo.redir then
        -- The word is an argument to a redirection symbol, so generate file
        -- matches.
        match_builder:addmatches(clink.filematches(line_state:getendword()))
        return true
    elseif matcher._flags and matcher:_is_flag(line_state:getendword()) then
        -- Flags are always "arg" type, which helps differentiate them from
        -- filename completions even when using _deprecated matcher mode, so
        -- that path normalization can avoid affecting flags like "/c", etc.
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
--- <code>:add_arguments()</code> adds one argument slot per table passed to it,
--- but <code>:addarg()</code> adds one argument slot containing everything
--- passed to it.  Be careful when updating scripts to use the new APIs; simply
--- renaming the call may not be enough, and it may be necessary to split it
--- into multiple separate calls to achieve the same behavior as before.
function _argmatcher:add_arguments(...)
    self:addarg(...)
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
--- <code>:set_arguments()</code> adds one argument slot per table passed to it,
--- but <code>:addarg()</code> adds one argument slot containing everything
--- passed to it.  Be careful when updating scripts to use the new APIs; simply
--- renaming the call may not be enough, and it may be necessary to split it
--- into multiple separate calls to achieve the same behavior as before.
---
--- Note:  The new API has no way to remove argument slots that were previously
--- added, so converting from <code>:set_arguments()</code> to
--- <code>:addarg()</code> may require the calling script to reorganize how and
--- when it adds arguments.
function _argmatcher:set_arguments(...)
    self._args = { _links = {} }
    self:addarg(...)
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
--- If one <span class="arg">command</span> is provided and an argument matcher
--- parser object is already associated with the command, this returns the
--- existing parser rather than creating a new parser.  Using :addarg() starts
--- at arg position 1, making it possible to merge new args and etc into the
--- existing parser.
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
            error("command '"..i.."' already has an argmatcher; clink.argmatcher() with multiple commands fails if any of the commands already has an argmatcher.")
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

    local matches = {}
    for _, i in ipairs(os.globdirs(word.."*", true)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
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
--- -show:  &nbsp; "--help",
--- -show:  &nbsp; "--file"..clink.argmatcher():addarg({ clink.filematches })
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

    local matches = {}
    for _, i in ipairs(os.globfiles(word.."*", true)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
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
        end
        return argmatcher, true
    end

    if command_word_index == 1 then
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
                    end
                    return argmatcher, true, words
                end
            end
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
        return argmatcher:_generate(line_state, match_builder, extra_words)
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

    local prefix = part:sub(1, 1)
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
function clink.arg.has_deprecated_parser(cmd)
    cmd = clink.lower(cmd)
    local parser = _argmatchers[cmd]
    return parser and parser._deprecated or false
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
    parser._flagprefix['-'] = 0
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
--- Adds <span class="arg">parser</span> to the first argmatcher for
--- <span class="arg">cmd</span>.  This behaves similarly to v0.4.8, but not
--- identically.  The Clink schema has changed significantly enough that there
--- is no direct 1:1 translation.  Calling
--- <code>clink.arg.register_parser</code> repeatedly with the same command to
--- merge parsers is not supported anymore.
--- -show:  -- Deprecated form:
--- -show:  local parser1 = clink.arg.new_parser("abc", "def")
--- -show:  local parser2 = clink.arg.new_parser("ghi", "jkl")
--- -show:  clink.arg.register_parser("foo", parser1)
--- -show:  clink.arg.register_parser("foo", parser2)
--- -show:
--- -show:  -- Replace with new form:
--- -show:  clink.argmatcher("foo"):addarg(parser1, parser2)
--- -show:
--- -show:  -- Warning:  Note that the following are NOT the same as above!
--- -show:  -- This replaces parser1 with parser2:
--- -show:  clink.argmatcher("foo"):addarg(parser1)
--- -show:  clink.argmatcher("foo"):addarg(parser2)
--- -show:  -- This uses only parser2 if/when parser1 finishes parsing args:
--- -show:  clink.argmatcher("foo"):addarg(parser1):addarg(parser2)
function clink.arg.register_parser(cmd, parser)
    cmd = clink.lower(cmd)

    local matcher = _argmatchers[cmd]
    if matcher then
        -- Merge new parser (parser) into existing parser (matcher).
        local success, msg = xpcall(parser_initialise, _error_handler_ret, matcher, parser)
        if not success then
            error(msg, 2)
        end
        return matcher
    end

    -- Register the parser.
    _argmatchers[cmd] = parser
    return matcher
end
