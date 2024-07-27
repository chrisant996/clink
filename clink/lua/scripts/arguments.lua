-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, arguments).

-- luacheck: no max line length

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
local _argmatcher = {}
_argmatcher.__index = _argmatcher
setmetatable(_argmatcher, { __call = function (x, ...) return x._new(...) end })



--------------------------------------------------------------------------------
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
        _line_state = line_state,
        _word_index = line_state:getcommandwordindex() + 1,
    }, _argreader)
    reader:start_command(root)
    return reader
end

--------------------------------------------------------------------------------
--[[
local enable_tracing = true
local debug_print = true
function _argreader:trace(...)
    if self._tracing then
        if debug_print then
            os.debugprint(...)
        else
            print(...)
        end
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
    if clink.co_state._argmatcher_fromhistory and clink.co_state._argmatcher_fromhistory.argmatcher then
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
            local mic = matcher._init_coroutine
            if mic then -- Avoid error if argmatcher was reset in the meantime.
                mic[arg_index] = nil
            end
            local cdc = _clear_delayinit_coroutine[matcher]
            if cdc then -- It may have been cleared by a new edit session.
                cdc[arg_index] = nil
            end
            list.delayinit = nil
            -- If originally started from not-main, then reclassify.
            if async_delayinit then
                clink._signal_delayed_init()
                clink.reclassifyline()
            end
        end)
        matcher._init_coroutine[arg_index] = c

        -- Make sure the delayinit coroutine runs to completion, even if a new
        -- prompt generation begins (which would normally orphan coroutines).
        clink.runcoroutineuntilcomplete(c)

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
        local ok, ret = coroutine.resume(c)
        if not ok and ret and settings.get("lua.debug") then
            print("")
            print("coroutine failed:")
            _co_error_handler(c, ret)
        end
    end
end

--------------------------------------------------------------------------------
local function do_onuse_callback(argmatcher, command_word)
    -- Don't init while generating matches from history, as that could be
    -- excessively expensive (could run thousands of callbacks).
    if clink.co_state._argmatcher_fromhistory and clink.co_state._argmatcher_fromhistory.argmatcher then
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
    if clink.DEBUG and c then
        print("\n\n\x1b[7m...already...  "..tostring(c).."\x1b[m")
        print("\n\n")
        pause()
    end
    if not c then
        -- Run the delayinit callback in a coroutine so typing is responsive.
        c = coroutine.create(function ()
            argmatcher._delayinit_func(argmatcher, command_word)
            argmatcher._onuse_coroutine = nil
            _clear_onuse_coroutine[argmatcher] = nil
            if async_delayinit then
                clink._signal_delayed_init()
                clink.reclassifyline()
            end
        end)
        argmatcher._onuse_coroutine = c

        -- Make sure the delayinit coroutine runs to completion, even if a new
        -- prompt generation begins (which would normally orphan coroutines).
        clink.runcoroutineuntilcomplete(c)

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
        local ok, ret = coroutine.resume(c)
        if not ok and ret and settings.get("lua.debug") then
            print("")
            print("coroutine failed:")
            _co_error_handler(c, ret)
        end
    end
end

--------------------------------------------------------------------------------
local function is_word_present(word, arg, t, arg_match_type)
    for _, i in ipairs(arg) do
        local it = type(i)
        if it == "function" then
            t = 'o' --other (placeholder; superseded by :classifyword).
        elseif i == word or (it == "table" and i.match == word) then
            return arg_match_type, true
        end
    end
    return t, false
end

--------------------------------------------------------------------------------
local function get_classify_color(code)
    if code:find("^m") then
        return settings.get("color.argmatcher")
    end

    if code == "a" then
        local color = settings.get("color.arg")
        if color ~= "" then
            return color
        end
        return settings.get("color.input")
    end

    local name
    if code == "c" then     name = "color.cmd"
    elseif code == "d" then name = "color.doskey"
    elseif code == "f" then name = "color.flag"
    elseif code == "x" then name = "color.executable"
    elseif code == "u" then name = "color.unrecognized"
    elseif code == "o" then name = "color.input"
    elseif code == "n" then name = "color.unexpected"
    end

    if name then
        return settings.get(name)
    end

    return ""
end

--------------------------------------------------------------------------------
local function parse_chaincommand_modes(modes)
    local mode = "cmd"
    local expand_aliases
    modes = modes or ""
    for _, m in ipairs(string.explode(modes:lower(), ", ")) do
        if m == "doskey" then
            expand_aliases = true
        elseif m == "cmd" or m == "start" or m == "run" then
            mode = m
        elseif m == "cmdquotes" then
            -- Undocumented mode.  CMD's command line parsing has quirks for
            -- quotes, so special handling is needed.
            mode = m
        end
    end
    return mode, expand_aliases
end

--------------------------------------------------------------------------------
local function break_slash(line_state, word_index, word_classifications)
    -- If word_index not specified, then find the command word.  This loops to
    -- find it instead of using getcommandwordindex() so that it can work even
    -- when chain command parsing is re-parsing an existing line_state
    -- starting from a later word index.
    if not word_index then
        for i = 1, line_state:getwordcount() do
            local info = line_state:getwordinfo(i)
            if not info.redir then
                if info.quoted then
                    return
                end
                word_index = i
                break
            end
        end
    end

    -- If the first character is not a forward slash but a forward slash is
    -- present later in the word, then split the word into two.
    local word = line_state:getword(word_index)
    local slash = word:find("/")
    if slash and slash > 1 then
        -- In `xyz/whatever` the `xyz` can never be an alias because it isn't
        -- whitespace delimited.
        local ls = line_state:_break_word(word_index, slash - 1)
        if ls then
            if word_classifications then
                word_classifications:_break_word(word_index, slash - 1)
            end
            return ls, word_classifications
        end
    end
end

--------------------------------------------------------------------------------
function _argreader:start_command(matcher)
    assert(not self._last_word)

    -- Start with argmatcher for new command.
    self._matcher = matcher
    self._realmatcher = matcher
    -- self._word_classifier = self._word_classifier
    -- self._extra = self._extra
    self._arg_index = 1                 -- Arg index in current argmatcher.
    self._stack = {}                    -- Stack of pushed argmatchers.

    -- Reset user_data.
    local shared_user_data = {}
    self._shared_user_data = shared_user_data
    self._user_data = { shared_user_data=shared_user_data }

    -- Reset parsing state.
    self._no_cmd = nil
    self._noflags = nil
    self._phantomposition = nil
    self._cmd_wordbreak = clink.is_cmd_wordbreak(self._line_state)

    -- Reset chaining state.
    self._chain_command = nil
    self._chain_command_expand_aliases = nil
    self._chain_command_mode = nil

    --self._fromhistory_matcher = self._fromhistory_matcher
    --self._fromhistory_argindex = self._fromhistory_argindex

    -- Reset cycle detection.
    self._cycle_detection = nil
    self._disabled = nil
end

--------------------------------------------------------------------------------
function _argreader:push_line_state(line_state, word_index, no_onalias)
    -- Only push the current one if it isn't finished.
    if self._word_index <= self._line_state:getwordcount() then
        local extra = self._extra or {}
        table.insert(extra, {
            line_state = self._line_state,
            word_index = self._word_index,
            no_onalias = self._no_onalias,
        })
        self._extra = extra
    end

    self._line_state = line_state
    self._word_index = word_index or 1
    self._no_onalias = (self._no_onalias or no_onalias) and true or nil
end

--------------------------------------------------------------------------------
function _argreader:pop_line_state()
    if self._extra then
        local popped = table.remove(self._extra)
        if popped then
            self._line_state = popped.line_state
            self._word_index = popped.word_index
            self._no_onalias = popped._no_onalias
        end
        if not self._extra[1] then
            self._extra = nil
        end
        return popped and true or nil
    end
end

--------------------------------------------------------------------------------
function _argreader:has_more_words(word_index)
    word_index = word_index or self._word_index
    if self._extra or self._line_state:getwordcount() > word_index then
        return true
    end
end

--------------------------------------------------------------------------------
function _argreader:next_word(always_last_word)
::retry::
    if self._last_word then
        return
    end

    local ls = self._line_state
    local word_count = ls:getwordcount()
    local word_index = self._word_index

    if word_index > word_count then
        if self:pop_line_state() then
            goto retry
        end
        return
    end

    self._word_index = word_index + 1

    if not self._extra and word_index >= word_count then
        self._last_word = true
    end

    local last_word = self._last_word or false
    local info = ls:getwordinfo(word_index)
    if info.redir and not always_last_word then
        goto retry
    end

    local word = ls:getword(word_index)

    return word, word_index, ls, last_word, info
end

--------------------------------------------------------------------------------
function _argreader:lookup_link(arg, arg_index, word, word_index, line_state)
    if word and arg then
        local link, forced

        if arg._links then
            local info = line_state:getwordinfo(word_index)
            if info then -- word_index may be -1 when expanding a doskey alias.
                local pos = info.offset + info.length
                if line_state:getline():sub(pos, pos) == "=" then
                    link = arg._links[word.."="]
                end
            end
            if not link then
                link = arg._links[word]
            end
        end

        if arg.onlink then
            local override = arg.onlink(link, arg_index, word, word_index, line_state, self._user_data)
            if override == false then
                link = nil
                forced = true
            elseif getmetatable(override) == _argmatcher then
                link = override
                forced = true
            end
        end

        return link, forced
    end
end

--------------------------------------------------------------------------------
-- Advancing twice from the same context indicates a cycle.  Disable the reader.
local __cycles = 0
function _argreader:_detect_arg_cycle()
    local d = self._cycle_detection
    if not d then
        d = {
            matcher=self._matcher,
            realmatcher=self._realmatcher,
            arg_index=self._arg_index,
            stack_depth=#self._stack,
        }
        self._cycle_detection = d
    elseif d.matcher == self._matcher and
            d.realmatcher == self._realmatcher and
            d.arg_index == self._arg_index and
            d.stack_depth == #self._stack then
        self._disabled = true
        if clink.DEBUG then
            __cycles = __cycles + 1
            clink.print("\x1b[s\x1b[H\x1b[0;97;41m cycle detected ("..__cycles..") \x1b[m\x1b[K\x1b[u", NONL)
        end
        return true
    end
end

--------------------------------------------------------------------------------
function _argreader:start_chained_command(word_index, mode, expand_aliases)
    local line_state = self._line_state
    self._no_cmd = nil
    self._chain_command = true
    self._chain_command_expand_aliases = expand_aliases
    mode = mode or "cmd"
    for i = word_index, line_state:getwordcount() do
        local info = line_state:getwordinfo(i)
        if not info.redir then
            if info.quoted then
                if mode == "cmdquotes" then
                    -- CMD has complicated special case handling when the text
                    -- after /C or /K or /R begins with a quote.  It's so
                    -- complicated that there's not a good way to accurately
                    -- predict how to parse it for all possible combinations,
                    -- so let's just disable input line coloring for the rest
                    -- of the line whenever that's encountered.
                    if self._word_classifier then
                        self._word_classifier:applycolor(info.offset - 1, 999999, settings.get("color.input"))
                    end
                end
                break
            end
            local alias = info.alias
            if expand_aliases and not alias then
                local word = line_state:getword(i)
                if word ~= "" then
                    local got = os.getalias(word)
                    alias = got and got ~= ""
                    if (not alias) ~= (not info.alias) then
                        local ls = line_state:_set_alias(i, alias)
                        if ls then
                            self._line_state = ls
                            line_state = ls
                        end
                    end
                end
            end
            if not alias and mode == "cmd" then
                local ls, wc = break_slash(line_state, i, self._word_classifier)
                if ls then
                    self._line_state = ls
                    if self._word_classifier then
                        self._word_classifier = wc
                    end
                end
            end
            self._no_cmd = not (mode == "cmd" or mode == "cmdquotes" or mode == "start")
            break
        end
    end

    line_state = self._line_state
    line_state:_shift(word_index)
    if self._word_classifier and not self._extra then
        self._word_classifier:_shift(word_index, line_state:getcommandwordindex())
    end
    self._word_index = 2
    return line_state:getword(1)
end

--------------------------------------------------------------------------------
-- When self._extra isn't nil, skip classifying the word.  This only happens
-- when parsing extra words from expanding a doskey alias, or from an inline
-- alias returned by an `onalias` callback function.
--
-- On return, the _argreader should be primed for generating matches for the
-- NEXT word in the line.
--
-- Returns TRUE when chaining due to chaincommand().
local default_flag_nowordbreakchars = "'`=+;,"
function _argreader:update(word, word_index, last_onadvance) -- luacheck: no unused
    self._chain_command = nil
    self._chain_command_expand_aliases = nil
    self._cycle_detection = nil
    if self._disabled then
        return
    end

::retry::
    local arg_match_type = "a" --arg
    local line_state = self._line_state

    --[[
    self._dbgword = word
    self:trace(word, "update")
    --]]

    -- When a flag ends with : or = but doesn't link to another matcher, and if
    -- the next word is adjacent, then treat the next word as an argument to the
    -- flag instead of advancing to the next argument position.
    if last_onadvance then -- luacheck: ignore 542
        -- Nothing to do here.
    elseif self._phantomposition then
        -- Skip past a phantom position.
        self._phantomposition = nil
        return
    elseif not self._noflags and
            self._matcher._flags and
            self._matcher:_is_flag(word) and
            word:find("..[:=]$") then
        -- Check if the word does not link to another matcher.
        local flagarg = self._matcher._flags._args[1]
        if not self:lookup_link(flagarg, 0, word, word_index, line_state) then
            -- Check if the next word is adjacent.
            local thiswordinfo = line_state:getwordinfo(word_index)
            local nextwordinfo = line_state:getwordinfo(word_index + 1)
            if nextwordinfo then
                local thisend = thiswordinfo.offset + thiswordinfo.length + (thiswordinfo.quoted and 1 or 0)
                local nextbegin = nextwordinfo.offset - (nextwordinfo.quoted and 1 or 0)
                if thisend >= nextbegin then
                    -- Treat the next word as though there were a linked matcher
                    -- that generates file matches.
                    self._phantomposition = true
                end
            end
        end
    end

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
        if matcher._flags and not last_onadvance then
            local arg = matcher._flags._args[1]
            if arg then
                if arg.delayinit then
                    do_delayed_init(arg, matcher, 0)
                end
                if arg.onalias and
                        not last_onadvance and
                        not (self._extra and self._extra.no_onalias) and
                        self:has_more_words(word_index) then
                    local expanded, chain = arg.onalias(0, word, word_index, line_state, self._user_data)
                    if expanded then
                        local line_states = clink.parseline(expanded)
                        if line_states and line_states[1].line_state then
                            if self._word_classifier and not self._extra then
                                self:classify_word(is_flag, arg_index, realmatcher, word, word_index, arg, arg_match_type)
                            end
                            self:push_line_state(line_states[1].line_state, 1, true--[[no_onalias]])
                            if chain then
                                self:start_chained_command(1, "cmd")
                                return true -- chain
                            end
                            return -- next word
                        end
                    end
                end
                if arg.onarg and clink._in_generate() then
                    arg.onarg(0, word, word_index, line_state, self._user_data)
                end
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
    elseif not self._noflags then
        next_is_flag = matcher:_is_flag(line_state:getword(word_index + 1))
    end

    -- Update matcher after possible _push.
    matcher = self._matcher
    realmatcher = self._realmatcher
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]

    -- Determine next arg index.
    local react, react_modes
    if arg and not is_flag then
        if arg.delayinit then
            do_delayed_init(arg, realmatcher, arg_index)
        end
        if arg.onadvance then
            react, react_modes = arg.onadvance(arg_index, word, word_index, line_state, self._user_data)
            if react then
                -- 1 = Ignore; advance to next arg_index.
                -- 0 = Repeat; stay on the arg_index.
                -- -1 = Chain; behave like :chaincommand().
                if react ~= 1 and react ~= 0 and react ~= -1 then
                    react = nil
                end
            end
            if react ~= -1 then
                react_modes = nil
            end
        end
        if arg.onalias and
                not last_onadvance and
                not (self._extra and self._extra.no_onalias) and
                self:has_more_words(word_index) then
            local expanded, chain = arg.onalias(arg_index, word, word_index, line_state, self._user_data)
            if expanded then
                local line_states = clink.parseline(expanded)
                if line_states and line_states[1].line_state then
                    if self._word_classifier and not self._extra then
                        self:classify_word(is_flag, arg_index, realmatcher, word, word_index, arg, arg_match_type)
                    end
                    self:push_line_state(line_states[1].line_state, 1, true--[[no_onalias]])
                    if chain then
                        self:start_chained_command(1, "cmd")
                        return true -- chain
                    end
                    return -- next word
                end
            end
        end
    end
    if last_onadvance then
        -- When in last_onadvance mode, bail out unless chaining or advancing
        -- is needed.
        if react ~= 1 and react ~= -1 and not ((not arg) and matcher._chain_command) then
            return
        end
    end
    local next_arg_index = arg_index + ((react ~= 0) and 1 or 0)

    -- Merge two adjacent words separated only by nowordbreakchars.
    if arg and (arg.nowordbreakchars or is_flag) and not self._cmd_wordbreak then
        -- Internal CMD commands and Batch scripts never use nowordbreakchars.
        -- Flags in other commands default to certain punctuation marks as
        -- nowordbreakchars.  This more accurately reflects how the command
        -- line will actually be parsed, especially for commas.
        --
        -- UNLESS the character is immediately preceded by ":", so that e.g.
        -- "-Q:+x" can still be interpreted as two words, "-Q:" and "+x".
        -- This exception is handled inside _unbreak_word() itself.
        local nowordbreakchars = arg.nowordbreakchars or default_flag_nowordbreakchars
        local adjusted, skip_word, len = line_state:_unbreak_word(word_index, nowordbreakchars)
        if adjusted then
            self._line_state = adjusted
            line_state = adjusted
            if self._word_classifier then
                self._word_classifier:_unbreak_word(word_index, len, skip_word)
            end
            if skip_word then
                if is_flag then
                    next_is_flag = matcher:_is_flag(line_state:getword(word_index + 1))
                    self:_pop(next_is_flag)
                end
                return
            end
            word = line_state:getword(word_index)
        end
    end

    -- If the arg has looping characters defined and a looping character
    -- separates this word from the next, then don't advance to the next
    -- argument index.
    if not react and arg and arg.loopchars and arg.loopchars ~= "" and word_index < line_state:getwordcount() then
        local thiswordinfo = line_state:getwordinfo(word_index)
        local nextwordinfo = line_state:getwordinfo(word_index + 1)
        local s = thiswordinfo.offset + thiswordinfo.length + (thiswordinfo.quoted and 1 or 0)
        local e = nextwordinfo.offset - 1 - (nextwordinfo.quoted and 1 or 0)
        if s == e then
            -- Two words are separated by a looping character, and the
            -- looping char is a natural word break char (e.g. semicolon).
            local line = line_state:getline()
            if arg.loopchars:find(line:sub(s, e), 1, true) then
                next_arg_index = arg_index
            end
        elseif s - 1 == e then
            local line = line_state:getline()
            if arg.loopchars:find(line:sub(e, e), 1, true) then
                -- End word is immediately preceded by a looping character.
                -- This is reached when getwordbreakinfo() splits a word due to
                -- a looping char that is not a natural word break char.
                next_arg_index = arg_index
            end
        end
    end

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

    if react == -1 then
        -- Chain due to request by `onadvance` callback.
        local mode, expand_aliases = parse_chaincommand_modes(react_modes)
        local lookup = self:start_chained_command(word_index, mode, expand_aliases)
        return true, lookup -- chaincommand.
    end

    -- Some matchers have no args at all.  Or ran out of args.
    if not arg then
        if matcher._chain_command then
            -- Chain due to :chaincommand() used in argmatcher.
            local lookup = self:start_chained_command(word_index, matcher._chain_command_mode, matcher._chain_command_expand_aliases)
            return true, lookup -- chaincommand.
        end
        if self._word_classifier and not self._extra then
            if matcher._no_file_generation then
                self._word_classifier:classifyword(word_index, "n", false)  --none
            else
                self._word_classifier:classifyword(word_index, "o", false)  --other
            end
        end
        return
    end

    -- If onadvance chose to ignore the arg index, then retry.
    if react == 1 then
        if self:_detect_arg_cycle() then
            return
        end
        goto retry
    elseif last_onadvance then
        return
    end

    -- Run delayinit and onarg (is_flag runs them further above).
    if not is_flag then
        if arg.delayinit then
            do_delayed_init(arg, realmatcher, arg_index)
        end
        if arg.onarg and clink._in_generate() then
            arg.onarg(arg_index, word, word_index, line_state, self._user_data)
        end
    end

    -- Generate matches from history.
    if self._fromhistory_matcher then
        if self._fromhistory_matcher == matcher and self._fromhistory_argindex == arg_index then
            if clink.co_state._argmatcher_fromhistory.builder then
                clink.co_state._argmatcher_fromhistory.builder:addmatch(word, "*")
            end
        end
    end

    -- Parse the word type.
    if self._word_classifier and not self._extra then
        self:classify_word(is_flag, arg_index, realmatcher, word, word_index, arg, arg_match_type)
    end

    -- Does the word lead to another matcher?
    local linked, forced = self:lookup_link(arg, is_flag and 0 or arg_index, word, word_index, line_state)
    if linked then
        if not forced and is_flag and word:match("..[:=]$") then
            local info = line_state:getwordinfo(word_index)
            if info and
                    line_state:getcursor() ~= info.offset + info.length and
                    line_state:getline():sub(info.offset + info.length, info.offset + info.length) == " " then
                -- Don't follow linked parser on `--foo=` flag if there's a
                -- space after the `:` or `=` unless the cursor is on the space.
                linked = nil
            end
        end
        if linked then
            if linked._delayinit_func then
                do_onuse_callback(linked, nil)
            end
            self:_push(linked)
        end
    end

    -- If it's a flag and doesn't have a linked matcher, then pop to restore the
    -- matcher that should be active for the next word.
    if not linked and is_flag then
        self:_pop(next_is_flag)
    end
end

--------------------------------------------------------------------------------
function _argreader:classify_word(is_flag, arg_index, realmatcher, word, word_index, arg, arg_match_type)
    local aidx = is_flag and 0 or arg_index
    local line_state = self._line_state
    if realmatcher._classify_func and realmatcher._classify_func(aidx, word, word_index, line_state, self._word_classifier, self._user_data) then -- luacheck: ignore 542
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
                local this_info = line_state:getwordinfo(word_index)
                local pos = this_info.offset + this_info.length
                local line = line_state:getline()
                if line:sub(pos, pos) == "=" then
                    -- If "word" is immediately followed by an equal sign,
                    -- then check if "word=" is a recognized argument.
                    t, matched = is_word_present(word.."=", arg, t, arg_match_type)
                    if matched then
                        self._word_classifier:applycolor(pos, 1, get_classify_color(t))
                    end
                end
                if not matched then
                    if arg.loopchars and arg.loopchars ~= "" then
                        -- If the arg has looping characters defined, then
                        -- split the word and apply colors to the sub-words.
                        pos = this_info.offset
                        local split = string.explode(word, arg.loopchars, '"')
                        for _, w in ipairs(split) do
                            t, matched = is_word_present(w, arg, t, arg_match_type)
                            if matched then
                                local i = line:find(w, pos, true)
                                if i then
                                    self._word_classifier:applycolor(i, #w, get_classify_color(t))
                                    pos = i + #w
                                end
                            end
                        end
                        t = nil
                    else
                        t, matched = is_word_present(word, arg, t, arg_match_type) -- luacheck: no unused
                    end
                end
            end
        end
        if t then
            self._word_classifier:classifyword(word_index, t, false)
        end
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
        table.insert(self._stack, { self._matcher, self._arg_index, self._realmatcher, self._noflags, self._user_data })
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
    if not realmatcher then -- Don't start new user data when switching to flags matcher.
        self._user_data = { shared_user_data=self._shared_user_data }
    end
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

        self._matcher, self._arg_index, self._realmatcher, self._noflags, self._user_data = table.unpack(table.remove(self._stack))

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
local function append_uniq_chars(chars, find, add)
    chars = chars or ""
    find = find or "[]"
    for i = 1, #add do
        local c = add:sub(i, i)
        if not chars:find(c, 1, true) then
            local byte = string.byte(c)
            local pct = ""
            if byte < 97 or byte > 122 then -- <'a' or >'z'
                pct = "%"
            end
            -- Update the list.
            chars = chars .. c
            -- Update the find expression.
            find = find:sub(1, #find - 1) .. pct .. c .. "]"
        end
    end
    return chars, find
end

--------------------------------------------------------------------------------
local function apply_options_to_list(addee, list)
    if addee.nosort then
        list.nosort = true
    end
    if type(addee.delayinit) == "function" then
        list.delayinit = addee.delayinit
    end
    if type(addee.onadvance) == "function" then
        list.onadvance = addee.onadvance
    end
    if type(addee.onalias) == "function" then
        list.onalias = addee.onalias
    end
    if type(addee.onarg) == "function" then
        list.onarg = addee.onarg
    end
    if type(addee.onlink) == "function" then
        list.onlink = addee.onlink
    end
    if addee.fromhistory then
        list.fromhistory = true
    end
    if type(addee.loopchars) == "string" then
        -- Apply looping characters, but avoid duplicates.
        list.loopchars, list.loopcharsfind = append_uniq_chars(list.loopchars, list.loopcharsfind, addee.loopchars)
    end
    if type(addee.nowordbreakchars) == "string" then
        -- Apply non-wordbreak characters, but avoid duplicates.
        list.nowordbreakchars = append_uniq_chars(list.nowordbreakchars, nil, addee.nowordbreakchars)
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
        local _, ismain = coroutine.running()
        if ismain then
            clink.co_state._argmatcher_fromhistory.argmatcher = reader._matcher
            clink.co_state._argmatcher_fromhistory.argslot = reader._arg_index
            clink.co_state._argmatcher_fromhistory.builder = builder
            -- Let the C++ code iterate through the history and call back into
            -- Lua to parse individual history lines.
            clink._generate_from_history()
            -- Clear references.  Clear builder because it goes out of scope,
            -- and clear other references to facilitate garbage collection.
            clink.co_state._argmatcher_fromhistory = {}
        else
            -- Generating from history can take a long time, depending on the
            -- size of the history.  It isn't suitable to run in a suggestions
            -- coroutine.  However, the menu-complete family of completion
            -- commands reuse available match results, which then sees no
            -- matches.  So, the match pipeline needs to be informed that the
            -- matches are not reusable.
            builder:setvolatile()
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
function _argmatcher:setcmdcommand()
    self._cmd_command = true
    return self
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
    self._cmd_command = nil
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
--- This adds a new argument position with the matches given by
--- <span class="arg">choices</span>.  Arguments can be a string, a string
--- linked to another parser by the concatenation operator, a table of
--- arguments, or a function that returns a table of arguments.  See
--- <a href="#argumentcompletion">Argument Completion</a> for more information.
--- -show:  local my_parser = clink.argmatcher("make_color_shape")
--- -show:  :addarg("red", "green", "blue")             -- 1st argument is a color
--- -show:  :addarg("circle", "square", "triangle")     -- 2nd argument is a shape
--- When providing a table of arguments, the table can contain some special
--- entries:
--- <p><table>
--- <tr><th>Entry</th><th>More Info</th><th>Version</th></tr>
--- <tr><td><code>delayinit=<span class="arg">function</span></code></td><td>See <a href="#addarg_delayinit">Delayed initialization for an argument position</a>.</td><td class="version">v1.3.10 and newer</td></tr>
--- <tr><td><code>fromhistory=true</code></td><td>See <a href="#addarg_fromhistory">Generate Matches From History</a>.</td><td class="version">v1.3.9 and newer</td></tr>
--- <tr><td><code>loopchars="<span class="arg">characters</span>"</code></td><td>See <a href="#addarg_loopchars">Delimited Arguments</a>.</td><td class="version">v1.3.37 and newer</td></tr>
--- <tr><td><code>nosort=true</code></td><td>See <a href="#addarg_nosort">Disable Sorting Matches</a>.</td><td class="version">v1.3.3 and newer</td></tr>
--- <tr><td><code>onarg=<span class="arg">function</span></code></td><td>See <a href="#responsive-argmatchers">Responding to Arguments in Argmatchers</a>.</td><td class="version">v1.3.13 and newer</td></tr>
--- </table></p>
--- <strong>Note:</strong>  Arguments are positional in an argmatcher.  Using
--- <code>:addarg()</code> multiple times adds multiple argument positions, in
--- the order they are specified.
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
--- When providing a table of flags, the table can contain some special
--- entries:
--- <p><table>
--- <tr><th>Entry</th><th>More Info</th><th>Version</th></tr>
--- <tr><td><code>delayinit=<span class="arg">function</span></code></td><td>See <a href="#addarg_delayinit">Delayed initialization for an argument position</a>.</td><td class="version">v1.3.10 and newer</td></tr>
--- <tr><td><code>fromhistory=true</code></td><td>See <a href="#addarg_fromhistory">Generate Matches From History</a>.</td><td class="version">v1.3.9 and newer</td></tr>
--- <tr><td><code>nosort=true</code></td><td>See <a href="#addarg_nosort">Disable Sorting Matches</a>.</td><td class="version">v1.3.3 and newer</td></tr>
--- <tr><td><code>onarg=<span class="arg">function</span></code></td><td>See <a href="#responsive-argmatchers">Responding to Arguments in Argmatchers</a>.</td><td class="version">v1.3.13 and newer</td></tr>
--- </table></p>
--- <strong>Note:</strong>  Flags are not positional in an argmatcher.  Using
--- <code>:addarg()</code> multiple times with different flags is the same as
--- using <code>:addarg()</code> once with all of the flags.
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
--- -show:  &nbsp;         "-d"..dirs, "--d"..dirs, "--di"..dirs, "--dir"..dirs)
--- -show:  :hideflags("--a", "--al", "--all",      -- Only "-a" is displayed.
--- -show:  &nbsp;          "-d", "--d", "--di")         -- Only "--dir" is displayed.
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
--- are listed, for example by the <code><a href="#rlcmd-complete">complete</a></code>
--- or <code><a href="#rlcmd-clink-select-complete">clink-select-complete</a></code>
--- or <code><a href="#rlcmd-possible-completions">possible-completions</a></code>
--- commands.
---
--- Any number of descriptions tables may be passed to the function, and each
--- table must use one of the following schemes:
--- <ul>
--- <li>One or more string values that are args or flags, and a
--- <code>description</code> field that is the associated description string.
--- <li>Key/value pairs where each key is an arg or flag, and its value is
--- either a description string or a table containing an optional argument info
--- string and a description string.  If an argument info string is provided, it
--- is appended to the arg or flag string when listing possible completions.
--- For example, <code>["--user"] = { " name", "Specify username"}</code> gets
--- printed as:
---
--- <pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black">
--- <span class="color_default">--user</span> <span class="color_arginfo">name</span>&nbsp;&nbsp;&nbsp;&nbsp;<span class="color_description">Specify username</span>
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
--- -name:  _argmatcher:chaincommand
--- -ver:   1.3.13
--- -arg:   [modes:string]
--- -ret:   self
--- This makes the rest of the line be parsed as a separate command, after the
--- argmatcher reaches the end of its defined argument positions.  You can use
--- it to "chain" from one parser to another.
---
--- For example, <code>cmd.exe program arg</code> is example of a line where one
--- command can have another command within it.  <code>:chaincommand()</code>
--- enables <code>program arg</code> to be parsed separately.  If
--- <code>program</code> has an argmatcher, then it takes over and parses the
--- rest of the input line.
---
--- An example that chains in a linked argmatcher:
--- -show:  clink.argmatcher("program"):addflags("/x", "/y")
--- -show:  clink.argmatcher("cmd"):addflags(
--- -show:  &nbsp;   "/c" .. clink.argmatcher():chaincommand(),
--- -show:  &nbsp;   "/k" .. clink.argmatcher():chaincommand()
--- -show:  ):nofiles()
--- -show:  -- Consider the following input:
--- -show:  --    cmd /c program /
--- -show:  -- "cmd" is colored as an argmatcher.
--- -show:  -- "/c" is colored as a flag (by the "cmd" argmatcher).
--- -show:  -- "program" is colored as an argmatcher.
--- -show:  -- "/" generates completions "/x" and "/y".
---
--- Examples that chain at the end of their argument positions:
--- -show:  clink.argmatcher("program"):addflags("-x", "-y")
--- -show:  clink.argmatcher("sometool"):addarg(
--- -show:  &nbsp;   "exec" .. clink.argmatcher()
--- -show:  &nbsp;               :addflags("-a", "-b")
--- -show:  &nbsp;               :addarg("profile1", "profile2")
--- -show:  &nbsp;               :chaincommand()
--- -show:  )
--- -show:  -- Consider the following input:
--- -show:  --    sometool exec profile1 program -
--- -show:  -- "sometool" is colored as an argmatcher.
--- -show:  -- "exec" is colored as an argument (for "sometool").
--- -show:  -- "profile1" is colored as an argument (for "exec").
--- -show:  -- "program" is colored as an argmatcher.
--- -show:  -- "-" generates completions "-x" and "-y".
---
--- In Clink v1.6.2 and higher, an optional <span class="arg">modes</span>
--- argument can let Clink know how the command will get interpreted.  The
--- string can contain any combination of the following words, separated by
--- spaces or commas.  If some words are mutually exclusive, the one that
--- comes last in the string is used.
--- <table>
--- <tr><th>Mode</th><th>Description</th></tr>
--- <tr><td><code>cmd</code></td><td>Lets the argmatcher know that the command gets processed how CMD.exe would process it.  This is the default if <span class="arg">modes</span> is omitted.</td></tr>
--- <tr><td><code>start</code></td><td>Lets the argmatcher know the command gets processed how the START command would process it.</td></tr>
--- <tr><td><code>run</code></td><td>Lets the argmatcher know the command gets processed how the <kbd>Win</kbd>-<kbd>R</kbd> Windows Run dialog box would process it.</td></tr>
--- <tr><td><code>doskey</code></td><td>Lets the argmatcher know if the command starts with a doskey alias it will be expanded.  This is very unusual, but for example the <a href="https://github.com/chrisant996/clink-gizmos/blob/main/i.lua">i.lua</a> script in <a href="https://github.com/chrisant996/clink-gizmos">clink-gizmos</a> does this.</td></tr>
--- </table>
---
--- The <span class="arg">modes</span> string does not affect how the command
--- gets executed.  It only affects how the argmatcher performs completions
--- and input line coloring, to help the argmatcher be accurate.
function _argmatcher:chaincommand(modes)
    modes = modes or ""
    self._chain_command = true
    self._chain_command_mode = "cmd"
    self._no_file_generation = true -- So that pop() doesn't interfere.
    self._chain_command_mode, self._chain_command_expand_aliases = parse_chaincommand_modes(modes)
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

    local num = self._flagprefix[first_char]
    if num ~= nil then
        return num > 0
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
local function add_merge_source(matcher, src)
    if src then
        matcher._srcmerged = matcher._srcmerged or {}
        table.insert(matcher._srcmerged, src)
    end
end

--------------------------------------------------------------------------------
local function merge_parsers(lhs, rhs)
    -- Merging parsers is not a trivial matter and this implementation is far
    -- from correct.  It behaves reasonably for common cases.

    -- Don't merge with itself!
    if lhs == rhs then
        return
    end

    -- Keep track of the merge sources.
    add_merge_source(lhs, rhs._srccreated)

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
        if type(rarg) == "table" then
            key = rarg.match or rarg
        else
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
    if prefixes and type(addee) == "string" and addee:match("..[:=]$") then
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
--- -ret:   stop:bool;      When true, stop generating.
--- -ret:   lookup:string;  Word to lookup next argmatcher; restart generation loop with new argmatcher.
function _argmatcher:_generate(reader, match_builder) -- luacheck: no unused
    --[[
    reader:starttracing(reader._line_state:getword(1))
    --]]

    -- Consume words and use them to move through matchers' arguments.
    local word, word_index, line_state, last_word, info
    while true do
        word, word_index, line_state, last_word, info = reader:next_word(true--[[always_last_word]])
        if last_word then
            break
        end
        local chain, chainlookup = reader:update(word, word_index)
        if chain then
            return true, true, chainlookup
        end
    end

    -- If not generating matches, then just consume the end word and return.
    word_index = line_state:getwordcount()
    word = line_state:getendword()
    if not match_builder then
        reader:update(word, word_index)
        return
    end

    -- Special processing for last word, in case there's an onadvance callback.
    if reader:update(word, word_index, true--[[last_onadvance]]) then
        if reader._line_state:getwordcount() > word_index then
            return true, true
        end
    end
    line_state = reader._line_state -- reader:update() can swap to a different line_state.

    -- There should always be a matcher left on the stack, but the arg_index
    -- could be well out of range.
    local matcher = reader._matcher
    local arg_index = reader._arg_index
    local match_type = ((not matcher._deprecated) and "arg") or nil
    local hidden

    word_index = line_state:getwordcount()
    info = line_state:getwordinfo(word_index)
    if clink.co_state.use_old_filtering then
        word = line_state:getline():sub(info.offset, line_state:getcursor() - 1)
    else
        word = line_state:getendword()
    end

    -- Are we left with a valid argument that can provide matches?
    local add_matches -- Separate decl and init to enable recursion.
    add_matches = function(arg, match_type) -- luacheck: ignore 431
        local descs = matcher._descriptions
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
                        m.arginfo = m.arginfo or d[1]
                        m.description = m.description or d[2]
                    else
                        m.description = m.description or d[1]
                    end
                end
            end
            return m
        end

        if matcher._deprecated then
            -- Mark the build as deprecated, so it can infer match types the
            -- old way.
            match_builder:deprecated_addmatch()
        end

        apply_options_to_builder(reader, arg, match_builder)
        for _, i in ipairs(arg) do
            local t = type(i)
            if t == "function" then
                local j = i(word, word_index, line_state, match_builder, reader._user_data)
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
        rl_state.first = info.offset
        rl_state.last = line_state:getcursor()
        rl_state.text = line_state:getline():sub(rl_state.first, rl_state.last - 1)
    end

    -- IMPORTANT:  The following use line_state:getendword() NOT word.
    -- Because word may not equal line_state:getendword() in certain backward
    -- compatibility cases (clink.co_state.use_old_filtering).

    -- Select between adding flags or matches themselves. Works in conjunction
    -- with getwordbreakinfo()'s return.
    if info.redir then
        -- The word is an argument to a redirection symbol, so generate file
        -- matches.
        match_builder:addmatches(clink.filematches(line_state:getendword()))
        return true
    elseif not reader._noflags and matcher._flags and matcher:_is_flag(line_state:getendword()) then
        -- Flags are always "arg" type, which helps differentiate them from
        -- filename completions even when using _deprecated matcher mode, so
        -- that path normalization can avoid affecting flags like "/c", etc.
        hidden = matcher._flags._hidden
        add_matches(matcher._flags._args[1], "arg")
        return true
    elseif reader._phantomposition then
        -- Generate file matches for phantom positions, i.e. any flag ending
        -- with : or = that does not explicitly link to another matcher.
        match_builder:addmatches(clink.filematches(line_state:getendword()))
        return true
    else
        -- Generate matches for the argument position.
        local arg = matcher._args[arg_index]
        if arg then
            return add_matches(arg, match_type) and true or false
        end
        -- Check matcher._chain_command for :chaincommand().
        -- Check reader._chain_command for onadvance callback that returns -1.
        if matcher._chain_command or reader._chain_command then
            local expand_aliases = matcher._chain_command_expand_aliases or reader._chain_command_expand_aliases
            local exec = clink._exec_matches(line_state, match_builder, true--[[chained]], not expand_aliases)
            return exec or add_matches({clink.filematches}) or false
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
--- <strong>Note:</strong>  The new API has no way to remove flags that were
--- previously added, so converting from <code>:set_flags()</code> to
--- <code>:addflags()</code> may require the calling script to reorganize how
--- and when it adds flags.
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
    clink._compat_warning("warning: ignoring _argmatcher:be_precise()", " -- this is not supported yet")
    return self
end



--------------------------------------------------------------------------------
clink = clink or {}
local _argmatchers = {}

if settings.get("lua.debug") or clink.DEBUG then
    -- Make it possible to inspect these locals in the debugger.
    clink.debug = clink.debug or {}
    clink.debug._argmatchers = _argmatchers
end

--------------------------------------------------------------------------------
local function get_creation_srcinfo()
    local first, src
    for level = 3, 10 do
        local info = debug.getinfo(level, "Sl")
        if not info then
            break
        end
        src = info.short_src..":"..info.currentline
        if not first then
            first = src
        end
        -- Favor returning a user script location.
        if not clink._is_internal_script(info.short_src) then
            return src
        end
    end
    -- If no user script location, return the original internal location.
    return first or "?"
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
--- In Clink v1.3.38 and higher, if a <span class="arg">command</span> is a
--- fully qualified path, then it is only used when the typed command expands to
--- the same fully qualified path.  This makes it possible to create one
--- argmatcher for <code>c:\general\program.exe</code> and another for
--- <code>c:\special\program.exe</code>.  For example, aliases may be used to
--- make both programs runnable, or the system PATH might be changed temporarily
--- while working in a particular context.
---
--- <strong>Note:</strong>  Merging <a href="#argmatcher_linking">linked
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
        matcher = _argmatchers[path.normalise(clink.lower(i))]
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
            _argmatchers[path.normalise(clink.lower(i))] = matcher
        end
    end

    if matcher then
        if matcher._srccreated then
            add_merge_source(matcher, get_creation_srcinfo())
        else
            matcher._srccreated = get_creation_srcinfo()
        end
    end

    return matcher
end

--------------------------------------------------------------------------------
local function dir_matches_impl(match_word, exact)
    local word, expanded = rl.expandtilde(match_word or "")
    local hidden = settings.get("files.hidden") and rl.isvariabletrue("match-hidden-files")

    local server = word:match("^\\\\([^\\]+)\\[^\\]*$")
    if server then
        local matches = {}
        for share, special in os.enumshares(server, hidden) do
            table.insert(matches, { match = string.format("\\\\%s\\%s\\", server, share), type = special and "dir,hidden" or "dir" })
        end
        return matches
    end

    local root = (path.getdirectory(word) or ""):gsub("/", "\\")
    if expanded then
        root = rl.collapsetilde(root)
    end

    local _, ismain = coroutine.running()

    local flags = {
        hidden=hidden,
        system=settings.get("files.system"),
    }

    local matches = {}
    for _, i in ipairs(os.globdirs(word..(exact and "" or "*"), true, flags)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
        if not ismain and _ % 250 == 0 then
            coroutine.yield()
        end
    end
    return matches
end

--------------------------------------------------------------------------------
local function file_matches_impl(match_word, exact)
    local word, expanded = rl.expandtilde(match_word or "")
    local hidden = settings.get("files.hidden") and rl.isvariabletrue("match-hidden-files")

    local server = word:match("^[/\\][/\\]([^/\\]+)[/\\][^/\\]*$")
    if server then
        local matches = {}
        for share, special in os.enumshares(server, hidden) do
            table.insert(matches, { match = string.format("\\\\%s\\%s\\", server, share), type = special and "dir,hidden" or "dir" })
        end
        return matches
    end

    local root = (path.getdirectory(word) or "")
    if word:find("^/", #root + 1) == #root + 1 then
        -- Preserve forward slash after the directory part, when present.
        root = root.."/"
    end
    if expanded then
        root = rl.collapsetilde(root)
    end

    local _, ismain = coroutine.running()

    local flags = {
        hidden=hidden,
        system=settings.get("files.system"),
    }

    local matches = {}
    for _, i in ipairs(os.globfiles(word..(exact and "" or "*"), true, flags)) do
        local m = path.join(root, i.name)
        table.insert(matches, { match = m, type = i.type })
        if not ismain and _ % 100 == 0 then
            coroutine.yield()
        end
    end
    return matches
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
    return dir_matches_impl(match_word)
end

--------------------------------------------------------------------------------
--- -name:  clink.dirmatchesexact
--- -ver:   1.6.4
--- -arg:   word:string
--- -ret:   table
--- This is the same as <a href="#clink.dirmatches">clink.dirmatches()</a>
--- except this doesn't append a <code>*</code> to the search pattern.
function clink.dirmatchesexact(match_word)
    return dir_matches_impl(match_word, true)
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
    return file_matches_impl(match_word)
end

--------------------------------------------------------------------------------
--- -name:  clink.filematchesexact
--- -ver:   1.6.4
--- -arg:   word:string
--- -ret:   table
--- This is the same as <a href="#clink.filematches">clink.filematches()</a>
--- except this doesn't append a <code>*</code> to the search pattern.
--- -show:  local function zip_file_matches(word)
--- -show:  &nbsp;   return clink.filematchesexact(word.."*.zip")
--- -show:  end
--- -show:
--- -show:  clink.argmatcher("unzip")
--- -show:  :addarg(zip_file_matches)   -- First arg is name of a .zip file.
--- -show:  :addarg()
--- -show:  :loop(2)
function clink.filematchesexact(match_word)
    return file_matches_impl(match_word, true)
end



--------------------------------------------------------------------------------
local function _is_argmatcher_loaded(command_word, quoted, no_cmd)
    local argmatcher

    repeat
        -- Check for an exact match.
        argmatcher = _argmatchers[command_word]
        if argmatcher and not (no_cmd and argmatcher._cmd_command) then
            break
        end

        -- Check for a name match.
        argmatcher = _argmatchers[path.getname(command_word)]
        if argmatcher and not (no_cmd and argmatcher._cmd_command) then
            break
        end

        -- If the extension is in PATHEXT then try stripping the extension.
        if path.isexecext(command_word) then
            argmatcher = _argmatchers[path.getbasename(command_word)]
            if argmatcher and argmatcher._cmd_command then
                -- CMD commands do not have extensions.
                argmatcher = nil
            end
            if argmatcher then
                break
            end
        end
    until true

    if argmatcher and argmatcher._cmd_command then
        if quoted then
            -- CMD commands cannot be quoted.
            argmatcher = nil
        elseif no_cmd then
            -- Ignore CMD commands when they're not allowed.
            argmatcher = nil
        end
    end

    return argmatcher
end

--------------------------------------------------------------------------------
local loaded_argmatchers = {}

--------------------------------------------------------------------------------
local function add_dirs_from_var(t, var, subdir)
    if var and var ~= "" then
        local dirs = string.explode(var, ";", '"')
        for _,d in ipairs(dirs) do
            d = rl.expandtilde(d:gsub('"', ""))
            if subdir then
                d = path.join(d, "completions")
            end
            d = path.getdirectory(path.join(d, "")) -- Makes sure no trailing path separator.
            table.insert(t, d)
        end
        return true
    end
end

--------------------------------------------------------------------------------
local _completion_dirs_str = ""
local _completion_dirs_list = {}
function clink._set_completion_dirs(str)
    if str ~= _completion_dirs_str then
        local dirs = {}

        _completion_dirs_str = str
        _completion_dirs_list = dirs

        add_dirs_from_var(dirs, os.getenv("CLINK_COMPLETIONS_DIR"), false)
        for _,d in ipairs(string.explode(str, ";", '"')) do
            add_dirs_from_var(dirs, d, true)
        end
    end
end

--------------------------------------------------------------------------------
local function get_completion_dirs()
    return _completion_dirs_list
end

--------------------------------------------------------------------------------
local function attempt_load_argmatcher(command_word, quoted, no_cmd)
    if not command_word or command_word == "" then
        return
    end

    -- Make sure scripts aren't loaded multiple times.
    loaded_argmatchers[command_word] = 1 -- Attempted.

    -- Device names are not valid commands.
    if path.isdevice(command_word) then
        return
    end

    -- Where to look.
    local dirs = get_completion_dirs()

    -- What to look for.
    local primary = path.getname(command_word)..".lua"
    local secondary
    if path.isexecext(command_word) then
        secondary = path.getbasename(command_word)
        if secondary == "" then
            secondary = nil
        else
            secondary = secondary..".lua"
        end
    end

    -- Look for file.
    local loaded = {}
    for _,d in ipairs(dirs) do
        if d ~= "" then
            local file = path.join(d, primary)
            if not os.isfile(file) then
                if not secondary then
                    file = nil
                else
                    file = path.join(d, secondary)
                    if not os.isfile(file) then
                        file = nil
                    end
                end
            end
            if file and not loaded[file] then
                loaded[file] = true
                loaded_argmatchers[command_word] = 2 -- Attempted and Loaded.
                -- Load the file.
                local impl = function ()
                    local func, message = loadfile(file)
                    if not func then
                        error(message)
                    end
                    func(command_word)
                end
                local ok, ret = xpcall(impl, _error_handler_ret)
                if not ok then
                    print("")
                    print("loading completion script failed:")
                    print(ret)
                    return
                end
                -- Check again, and stop if argmatcher is loaded.
                local argmatcher = _is_argmatcher_loaded(command_word, quoted, no_cmd)
                if argmatcher then
                    loaded_argmatchers[command_word] = 3 -- Attempted, loaded, and has argmatcher.
                    return argmatcher
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
local function sanitize_command_word(command_word)
    if command_word then
        if command_word:find("^@") then
            local cw = command_word:gsub("^@?\"?", "")
            return cw, true
        else
            return command_word, false
        end
    end
end

--------------------------------------------------------------------------------
-- This checks if an argmatcher is already loaded for the specified word.  If
-- not, then it looks for a Lua script by that name in one of the completions
-- directories.  If found, the script is loaded, and it checks again whether an
-- argmatcher is already loaded for the specified word.
local function _has_argmatcher(command_word, quoted, no_cmd)
    command_word = sanitize_command_word(command_word)
    if not command_word or command_word == "" then
        return
    end

    command_word = clink.lower(command_word:gsub("/", "\\"))

    -- Don't invoke the recognizer while generating matches from history, as
    -- that could be excessively expensive (could queue thousands of lookups).
    local recognized
    if not (clink.co_state._argmatcher_fromhistory and clink.co_state._argmatcher_fromhistory.argmatcher) then
        -- Pass true because argmatcher lookups always treat ^ literally.
        local _, _, file = clink.recognizecommand(command_word, true)
        if file then
            command_word = clink.lower(file)
            recognized = file
        end
    end

    local argmatcher = _is_argmatcher_loaded(command_word, quoted, no_cmd)

    -- If an argmatcher isn't loaded, look for a Lua script by that name in one
    -- of the completions directories.  If found, load it and check again.
    if not argmatcher and not loaded_argmatchers[command_word] and recognized then
        argmatcher = attempt_load_argmatcher(command_word, quoted, no_cmd)
    end

    if argmatcher and not (clink.co_state._argmatcher_fromhistory and clink.co_state._argmatcher_fromhistory.argmatcher) then
        local alias = os.getalias(command_word)
        if not alias or alias == "" then
            -- Avoid coloring directories as having argmatchers, unless it's
            -- also CMD builtin command name in a context where CMD commands
            -- may be processed.
            if clink._async_path_type(command_word, 15, clink.reclassifyline) == "dir" then
                if no_cmd or not clink.is_cmd_command(command_word) then
                    return
                end
            end
        end
    end

    return argmatcher
end

--------------------------------------------------------------------------------
-- Finds an argmatcher for the first word and returns:
--  argmatcher  = The argmatcher, unless there are too few words to use it.
--  exists      = True if argmatcher exists (even if too few words to use it).
--  extra       = Extra line_state to run through reader before continuing.
--  no_cmd      = Don't find argmatchers for CMD builtin commands.
local function _find_argmatcher(line_state, check_existence, lookup, no_cmd, has_extra)
    -- Running an argmatcher only makes sense if there's two or more words.
    local word_count = line_state:getwordcount()
    local command_word_index = line_state:getcommandwordindex()
    if word_count < command_word_index + ((check_existence or has_extra) and 0 or 1) then
        return
    end
    if word_count > command_word_index then
        check_existence = nil
    end

    local command_word = lookup or line_state:getword(command_word_index)
    local original_command_word = command_word
    command_word = sanitize_command_word(command_word)
    if not command_word or command_word == "" then
        return
    end

    command_word = clink.lower(command_word:gsub("/", "\\"))

    local info = not lookup and line_state:getwordinfo(command_word_index)
    if command_word_index == 1 and not lookup and info then
        if info.alias then
            local alias = os.getalias(line_state:getline():sub(info.offset, info.offset + info.length - 1))
            if alias and alias ~= "" then
                -- This doesn't even try to handle redirection symbols in the alias
                -- because the cost/benefit ratio is unappealing.
                alias = alias:gsub("%$.*$", "")
                local extras = clink.parseline(alias)
                local extra = extras and extras[#extras]
                if extra then
                    local els = extra.line_state
                    local ecwi = els:getcommandwordindex()
                    local einfo = els:getwordinfo(ecwi)
                    local eword = els:getword(ecwi)
                    local argmatcher = _has_argmatcher(eword, einfo and einfo.quoted, no_cmd)
                    if argmatcher then
                        if check_existence then
                            argmatcher = nil
                        elseif argmatcher._delayinit_func then
                            do_onuse_callback(argmatcher, eword)
                        end
                        extra.next_index = ecwi + 1
                        return argmatcher, true, extra
                    end
                end
            end
        end
    end

    -- Don't invoke the recognizer while generating matches from history, as
    -- that could be excessively expensive (could queue thousands of lookups).
    if not (clink.co_state._argmatcher_fromhistory and clink.co_state._argmatcher_fromhistory.argmatcher) then
        -- Pass true because argmatcher lookups always treat ^ literally.
        local _, _, file = clink.recognizecommand(command_word, true)
        if file then
            command_word = file
            info = nil
        end
    end

    -- Pass original_command_word, otherwise it can end up stripping TWO
    -- leading @ characters.
    local argmatcher = _has_argmatcher(original_command_word, info and info.quoted, no_cmd)
    if argmatcher then
        if check_existence then
            argmatcher = nil
        elseif argmatcher._delayinit_func then
            do_onuse_callback(argmatcher, command_word)
        end
        return argmatcher, true
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.getargmatcher
--- -ver:   1.3.12
--- -arg:   find:string|line_state
--- -ret:   argmatcher|nil
--- Finds the argmatcher registered to handle a command, if any.
---
--- When <span class="arg">find</span> is a string it is interpreted as the
--- name of a command, and this looks up the argmatcher for the named command.
---
--- When <span class="arg">find</span> is a <a href="#line_state">line_state</a>
--- this looks up the argmatcher for the command line.
---
--- If no argmatcher is found, this returns nil.
function clink.getargmatcher(find)
    local t = type(find)
    if t == "string" then
        local quoted
        if find:match('^".*"$') then
            quoted = true
            find = find:gsub('^"(.*)"$', '%1')
        end
        return _has_argmatcher(find, quoted)
    elseif t == "userdata" then
        local matcher = _find_argmatcher(find)
        return matcher
    else
        return nil
    end
end

--------------------------------------------------------------------------------
function clink._generate_from_historyline(line_state)
    local lookup
    local no_cmd
    local reader
::do_command::
    local argmatcher, has_argmatcher, alias = _find_argmatcher(line_state, nil, lookup, no_cmd, reader and reader._extra) -- luacheck: no unused
    if not argmatcher or argmatcher ~= clink.co_state._argmatcher_fromhistory_root then
        return
    end
    lookup = nil

    if reader then
        reader:start_command(argmatcher)
    else
        reader = _argreader(argmatcher, line_state)
    end
    if alias and not reader._extra then
        alias.line_state = break_slash(alias.line_state) or alias.line_state
        reader:push_line_state(alias.line_state, alias.next_index)
    end
    reader._fromhistory_matcher = clink.co_state._argmatcher_fromhistory.argmatcher
    reader._fromhistory_argindex = clink.co_state._argmatcher_fromhistory.argslot

    -- Consume words and use them to move through matchers' arguments.
    local word, word_index, info
    while true do
        word, word_index, line_state, _, info = reader:next_word()
        if not word then
            break
        end
        local chain, chainlookup = reader:update(word, word_index)
        if chain then
            line_state = reader._line_state -- reader:update() can swap to a different line_state.
            lookup = chainlookup
            no_cmd = reader._no_cmd
            goto do_command
        end
    end
end



--------------------------------------------------------------------------------
clink.argmatcher_generator_priority = 24
local argmatcher_generator = clink.generator(clink.argmatcher_generator_priority)
local argmatcher_classifier = clink.classifier(clink.argmatcher_generator_priority)

--------------------------------------------------------------------------------
local function do_generate(line_state, match_builder)
    local lookup
    local no_cmd
    local reader
::do_command::
    local argmatcher, has_argmatcher, alias = _find_argmatcher(line_state, nil, lookup, no_cmd, reader and reader._extra) -- luacheck: no unused
    lookup = nil -- luacheck: ignore 311
    if argmatcher then
        clink.co_state._argmatcher_fromhistory = {}
        clink.co_state._argmatcher_fromhistory_root = argmatcher

        if reader then
            reader:start_command(argmatcher)
        else
            reader = _argreader(argmatcher, line_state)
        end
        if alias and not reader._extra then
            alias.line_state = break_slash(alias.line_state) or alias.line_state
            reader:push_line_state(alias.line_state, alias.next_index)
        end

        local ret, chain, chainlookup = argmatcher:_generate(reader, match_builder)
        if ret and chain then
            line_state = reader._line_state -- argmatcher:_generate() calls reader:update() which can swap to a different line_state.
            lookup = chainlookup
            no_cmd = reader._no_cmd
            goto do_command
        end

        clink.co_state._argmatcher_fromhistory = {}
        clink.co_state._argmatcher_fromhistory_root = nil
        return ret
    end

    return false
end

--------------------------------------------------------------------------------
function argmatcher_generator:generate(line_state, match_builder) -- luacheck: no unused
    if clink.co_state.argmatcher_line_states then
        local num = #clink.co_state.argmatcher_line_states - 1
        for i = 1, num do
            -- Don't pass match_builder; these parse without generating.
            do_generate(clink.co_state.argmatcher_line_states[i].line_state)
        end
    end

    return do_generate(line_state, match_builder)
end

--------------------------------------------------------------------------------
function argmatcher_generator:getwordbreakinfo(line_state) -- luacheck: no self
    local lookup
    local original_line_state = line_state
    local no_cmd
    local reader
::do_command::
    local argmatcher, has_argmatcher, alias = _find_argmatcher(line_state, nil, lookup, no_cmd, reader and reader._extra) -- luacheck: no unused
    lookup = nil
    if argmatcher then
        if reader then
            reader:start_command(argmatcher)
        else
            reader = _argreader(argmatcher, line_state)
        end
        if alias and not reader._extra then
            alias.line_state = break_slash(alias.line_state) or alias.line_state
            reader:push_line_state(alias.line_state, alias.next_index)
        end

        -- Consume words and use them to move through matchers' arguments.
        local word, word_index, last_word, info
        while true do
            word, word_index, line_state, last_word, info = reader:next_word(true--[[always_last_word]])
            if last_word then
                break
            end
            local chain, chainlookup = reader:update(word, word_index)
            if chain then
                line_state = reader._line_state -- reader:update() can swap to a different line_state.
                lookup = chainlookup
                no_cmd = reader._no_cmd
                goto do_command
            end
        end

        -- Special processing for last word, in case there's an onadvance callback.
        word_index = line_state:getwordcount()
        word = line_state:getendword()
        do
            local chain, chainlookup = reader:update(word, word_index, true--[[last_onadvance]])
            if chain then
                line_state = reader._line_state -- reader:update() can swap to a different line_state.
                if reader:has_more_words(word_index) then
                    lookup = chainlookup
                    no_cmd = reader._no_cmd
                    goto do_command
                end
            end
        end
        line_state = reader._line_state -- reader:update() can swap to a different line_state.
        -- word_count = line_state:getwordcount() -- Unused.

        if not reader._extra and original_line_state ~= line_state then
            original_line_state:_overwrite_from(line_state)
        end

        -- Looping characters are also word break characters.
        argmatcher = reader._matcher
        local arg = argmatcher._args[reader._arg_index]
        if arg and arg.loopchars then
            local word = line_state:getendword()
            local pos = 0
            while true do
                local next = word:find(arg.loopcharsfind, pos + 1)
                if not next then
                    break
                end
                pos = next
            end
            if pos > 0 then
                return pos, 0, line_state
            end
        end

        -- There should always be a matcher left on the stack, but the arg_index
        -- could be well out of range.
        if not reader._noflags and argmatcher and argmatcher._flags then
            local word = line_state:getendword()
            if argmatcher:_is_flag(word) then
                -- Accommodate `-flag:text` and `-flag=text` (with or without
                -- quotes) so that matching can happen for the `text` portion.
                local attached_arg,attach_pos = word:find("^[^:=][^:=]+[:=]")
                if attached_arg then
                    return attach_pos, 0, line_state
                end
                return 0, 1, line_state
            end
        end
    end

    return 0, nil, line_state
end

--------------------------------------------------------------------------------
function argmatcher_classifier:classify(commands) -- luacheck: no self
    local unrecognized_color = settings.get("color.unrecognized") ~= ""
    local executable_color = settings.get("color.executable") ~= ""
    for _,command in ipairs(commands) do
        local lookup
        local line_state = command.line_state
        local word_classifier = command.classifications
        local no_cmd
        local reader
::do_command::

        local argmatcher, has_argmatcher, alias = _find_argmatcher(line_state, true, lookup, no_cmd, reader and reader._extra)
        local command_word_index = line_state:getcommandwordindex()
        lookup = nil

        local command_word = line_state:getword(command_word_index) or ""
        local cw, sanitized = sanitize_command_word(command_word)
        if #cw > 0 then
            local info = line_state:getwordinfo(command_word_index)
            local m = has_argmatcher and "m" or ""
            if info.alias then
                word_classifier:classifyword(command_word_index, m.."d", false); --doskey
            elseif not info.quoted and not no_cmd and clink.is_cmd_command(command_word) then
                word_classifier:classifyword(command_word_index, m.."c", false); --command
            elseif unrecognized_color or executable_color then
                local cl
                local recognized = clink._recognize_command(line_state:getline(), cw, info.quoted)
                if recognized < 0 then
                    cl = unrecognized_color and "u" or "o"      --unrecognized
                elseif recognized > 0 then
                    cl = executable_color and "x" or "o"        --executable
                else
                    cl = "o"                                    --other
                end
                if sanitized then
                    word_classifier:applycolor(info.offset + info.length - #cw, #cw, get_classify_color(m..cl))
                else
                    word_classifier:classifyword(command_word_index, m..cl, false);
                end
            else
                word_classifier:classifyword(command_word_index, m.."o", false); --other
            end
        end
        if sanitized then
            local info = line_state:getwordinfo(command_word_index)
            word_classifier:applycolor(info.offset, 1, get_classify_color("c"))
        end

        if argmatcher then
            if reader then
                reader:start_command(argmatcher)
            else
                reader = _argreader(argmatcher, line_state)
                reader._word_classifier = word_classifier
            end
            if alias and not reader._extra then
                alias.line_state = break_slash(alias.line_state) or alias.line_state
                reader:push_line_state(alias.line_state, alias.next_index)
            end

            -- Consume words and use them to move through matchers' arguments.
            while true do
                local word, word_index, line_state = reader:next_word()
                if not word then
                    break
                end
                local chain, chainlookup = reader:update(word, word_index)
                if chain then
                    line_state = reader._line_state -- reader:update() can swap to a different line_state.
                    lookup = chainlookup
                    no_cmd = reader._no_cmd
                    goto do_command
                end
            end
        end
    end

    return false -- continue
end



--------------------------------------------------------------------------------
local function spairs(t, order)
    -- collect the keys
    local keys = {}
    for k in pairs(t) do keys[#keys+1] = k end

    -- if order function given, sort by it by passing the table and keys a, b,
    -- otherwise just sort the keys
    if order then
        table.sort(keys, function(a,b) return order(t, a, b) end)
    else
        table.sort(keys)
    end

    -- return the iterator function
    local i = 0
    return function()
        i = i + 1
        if keys[i] then
            return keys[i], t[keys[i]]
        end
    end
end

--------------------------------------------------------------------------------
function clink._diag_argmatchers(arg)
    arg = (arg and arg >= 2)
    if not arg then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.
    local merged_with = "  merged with"

    clink.print(bold.."argmatchers:"..norm)

    local width = 0
    for k,v in pairs(_argmatchers) do
        if width < #k then
            width = #k
        end
        if v._srcmerged and width < #merged_with then
            width = #merged_with
        end
    end

    local any = false
    local fmt = "  %-"..width.."s  :  %s"
    for k,v in spairs(_argmatchers) do
        local src = v._srccreated
        if src and (not clink._is_internal_script(src) or v._srcmerged) then
            any = true
            clink.print(string.format(fmt, k, src))
            if v._srcmerged then
                for _,sm in ipairs(v._srcmerged) do
                    clink.print(string.format(fmt, merged_with, sm))
                end
            end
        end
    end
    if not any then
        clink.print("  none")
    end
end

--------------------------------------------------------------------------------
function clink._diag_completions_dirs(arg)
    arg = (arg and arg >= 1)
    if not arg and not settings.get("lua.debug") then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.

    clink.print(bold.."completions:"..norm)

    clink.print("  completions lookup directories:")

    local dirs = get_completion_dirs()
    for _,d in ipairs(dirs) do
        clink.print("", d)
    end

    clink.print("  completions lookup statistics:")

    local attempted = 0
    local loaded = 0
    local found = 0
    for _,v in pairs(loaded_argmatchers) do
        if v >= 1 then
            attempted = attempted + 1
        end
        if v >= 2 then
            loaded = loaded + 1
        end
        if v >= 3 then
            found = found + 1
        end
    end

    clink.print("", "commands searched:", attempted)
    clink.print("", "Lua scripts loaded:", loaded)
    clink.print("", "argmatchers loaded:", found)
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
    if parser then
        parser._srccreated = get_creation_srcinfo()
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
    cmd = path.normalise(clink.lower(cmd))

    if not parser or getmetatable(parser) ~= _argmatcher then
        local p = clink.arg.new_parser()
        p:set_arguments({ parser })
        parser = p
    end

    if parser._deprecated then
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
