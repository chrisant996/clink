-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

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
function _argreader._new(root)
    local reader = setmetatable({
        _matcher = root,
        _arg_index = 1,
        _stack = {},
        _word_types = nil,
    }, _argreader)
    return reader
end

--------------------------------------------------------------------------------
function _argreader:update(word, word_index)
    local arg_match_type = "a" --arg

    -- Check for flags and switch matcher if the word is a flag.
    local matcher = self._matcher
    local is_flag = matcher:_is_flag(word)
    if is_flag then
        if matcher._flags then
            self:_push(matcher._flags)
            arg_match_type = "f" --flag
        else
            return
        end
    end

    matcher = self._matcher
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]
    local next_arg_index = arg_index + 1

    -- If arg_index is out of bounds we should loop if set or return to the
    -- previous matcher if possible.
    if next_arg_index > #matcher._args then
        if matcher._loop then
            self._arg_index = math.min(math.max(matcher._loop, 1), #matcher._args)
        elseif not self:_pop() then
            self._arg_index = next_arg_index
        end
    else
        self._arg_index = next_arg_index
    end

    -- Some matchers have no args at all.  Or ran out of args.
    if not arg then
        if self._word_types then
            local t
            if matcher._no_file_generation then
                t = "n" --none
            else
                t = "o" --other
            end
            self:_add_word_type(t)
        end
        return
    end

    -- Parse the word type.
    if self._word_types then
        if matcher._classify and matcher._classify(arg_index, word, word_index, self._line_state, self._word_classifier) then
            -- The classifier function says it handled the word.
        else
            -- Use the argmatcher's data to classify the word.
            local t = "o" --other
            if arg._links and arg._links[word] then
                t = arg_match_type
            else
                for _, i in ipairs(arg) do
                    if type(i) == "function" then
                        -- For performance reasons, don't run argmatcher functions
                        -- during classify.  If that's needed, a script can provide
                        -- a :classify function to complement a :generate function.
                        t = 'o' --other (placeholder; superseded by :classifyword).
                    elseif i == word then
                        t = arg_match_type
                        break
                    end
                end
            end
            self:_add_word_type(t)
        end
    end

    -- Does the word lead to another matcher?
    for key, linked in pairs(arg._links) do
        if key == word then
            self:_push(linked)
            break
        end
    end
end

--------------------------------------------------------------------------------
function _argreader:_push(matcher)
    table.insert(self._stack, { self._matcher, self._arg_index })
    self._matcher = matcher
    self._arg_index = 1
end

--------------------------------------------------------------------------------
function _argreader:_pop()
    if #self._stack > 0 then
        self._matcher, self._arg_index = table.unpack(table.remove(self._stack))
        return true
    end

    return false
end

--------------------------------------------------------------------------------
function _argreader:_add_word_type(t)
    if self._word_types then
        table.insert(self._word_types, t)
    end
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
--- -name:  _argmatcher:addarg
--- -arg:   choices...:string|table
--- -ret:   self
--- -show:  local my_parser = clink.argmatcher("git")
--- -show:  :addarg("add", "status", "commit", "checkout")
--- This adds argument matches.  Arguments can be a string, a string linked to
--- another parser by the concatenation operator, a table of arguments, or a
--- function that returns a table of arguments.  See
--- <a href="#argumentcompletion">Argument Completion</a> for more information.
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
--- -arg:   flags...:string
--- -ret:   self
--- -show:  local my_parser = clink.argmatcher("git")
--- -show:  :addarg({ "add", "status", "commit", "checkout" })
--- -show:  :addflags("-a", "-g", "-p", "--help")
--- This adds flag matches.  Flags are separate from arguments:  When listing
--- possible completions for an empty word, only arguments are listed.  But when
--- the word being completed starts with the first character of any of the
--- flags, then only flags are listed.  See
--- <a href="#argumentcompletion">Argument Completion</a> for more information.
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
--- -arg:   [index:integer]
--- -ret:   self
--- -show:  clink.argmatcher("xyzzy")
--- -show:  :addarg("zero", "cero")     -- first arg can be zero or cero
--- -show:  :addarg("one", "uno")       -- second arg can be one or uno
--- -show:  :addarg("two", "dos")       -- third arg can be two or dos
--- -show:  :loop(2)    -- fourth arg loops back to position 2, for one or uno, and so on
--- This makes the parser loop back to argument position
--- <span class="arg">index</span> when it runs out of positional sets of
--- arguments (if <span class="arg">index</span> is omitted it loops back to
--- argument position 1).
function _argmatcher:loop(index)
    self._loop = index or -1
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setflagprefix
--- -arg:   [prefixes...:string]
--- -ret:   self
--- -show:  local my_parser = clink.argmatcher()
--- -show:  :setflagprefix("-", "/", "+")
--- -show:  :addflags("--help", "/?", "+mode")
--- -deprecated: _argmatcher:addflags
--- This overrides the default flag prefix (<code>-</code>).  The flag prefixes are used to
--- switch between matching arguments versus matching flags.  When listing
--- possible completions for an empty word (e.g. <code>command _</code> where the cursor is
--- at the <code>_</code>), only arguments are listed.  And only flags are listed when the
--- word starts with one of the flag prefixes.  Each flag prefix must be a
--- single character, but there can be multiple prefixes.
---
--- This is no longer needed because <code>:addflags()</code> does it
--- automatically.
function _argmatcher:setflagprefix(...)
    if self._deprecated then
        local old = self._flagprefix
        self._flagprefix = {}
        for _, i in ipairs({...}) do
            if type(i) ~= "string" or #i ~= 1 then
                error("Flag prefixes must be single character strings", 2)
            end
            self._flagprefix[i] = old[i] or 0
        end
    end
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:nofiles
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
--- -arg:   func:function
--- -ret:   self
--- This registers a function that gets called for each word the argmatcher
--- handles, to classify the word as part of coloring the input text.  See
--- <a href="#classifywords">Coloring The Input Text</a> for more information.
function _argmatcher:setclassifier(func)
    self._classify = func
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
    for i, _ in pairs(self._flagprefix) do
        if first_char == i then
            return true
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
                            print("warning: replacing arglink for '"..k.."'; merging is not supported yet.")
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
            print("warning: replacing arglink for '"..addee.key.."'; merging is not supported yet.")
        end
        list._links[addee._key] = addee._matcher
        if prefixes then add_prefix(prefixes, addee._key) end
    else
        table.insert(list, addee)
        if prefixes then add_prefix(prefixes, addee) end
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_generate(line_state, match_builder)
    local reader = _argreader(self)

    -- Consume words and use them to move through matchers' arguments.
    local word_count = line_state:getwordcount()
    for word_index = 2, (word_count - 1) do
        local word = line_state:getword(word_index)
        reader:update(word, word_index)
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
print("calling func('"..line_state:getendword().."', "..word_count..")")
                local j = i(line_state:getendword(), word_count, line_state, match_builder, nil)
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
    if matcher._flags and matcher:_is_flag(line_state:getendword()) then
        add_matches(matcher._flags._args[1], match_type)
        return true
    else
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
-- Deprecated.
function _argmatcher:add_arguments(...)
    self:addarg(...)
    return self
end

--------------------------------------------------------------------------------
-- Deprecated.
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
-- Deprecated.
function _argmatcher:set_arguments(...)
    self._args = { _links = {} }
    self:addarg(...)
    return self
end

--------------------------------------------------------------------------------
-- Deprecated.
function _argmatcher:set_flags(...)
    self._flags = nil
    self:addflags(...)
    return self
end



--------------------------------------------------------------------------------
clink = clink or {}
local _argmatchers = {}

--------------------------------------------------------------------------------
--- -name:  clink.argmatcher
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
--- -arg:   word:string
--- -ret:   table
--- -show:  -- Make "cd" generate directory matches (no files).
--- -show:  clink.argmatcher("cd")
--- -show:  :addflags("/d")
--- -show:  :argarg(({ clink.dirmatches })
--- You can use this function in an argmatcher to supply directory matches.
--- This automatically handles Readline tilde completion.
function clink.dirmatches(match_word)
    local word = rl.expandtilde(match_word)

    local root = path.getdirectory(word) or ""
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
--- -arg:   word:string
--- -ret:   table
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
--- You can use this function in an argmatcher to supply file matches.  This
--- automatically handles Readline tilde completion.
---
--- Argmatchers default to matching files, so it's unusual to need this
--- function.  However, some exceptions are when a flag needs to accept file
--- matches but other flags and arguments don't, or when matches need to include
--- more than files.
function clink.filematches(match_word)
    local word = rl.expandtilde(match_word)

    local root = path.getdirectory(word) or ""
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
local function _find_argmatcher(line_state)
    -- Running an argmatcher only makes sense if there's two or more words.
    if line_state:getwordcount() < 2 then
        return
    end

    local first_word = clink.lower(line_state:getword(1))

    -- Check for an exact match.
    local argmatcher = _argmatchers[path.getname(first_word)]
    if argmatcher then
        return argmatcher
    end

    -- If the extension is in PATHEXT then try stripping the extension.
    if path.isexecext(first_word) then
        argmatcher = _argmatchers[path.getbasename(first_word)]
        if argmatcher then
            return argmatcher
        end
    end
end



------------------------------------------------------------------------------
-- This returns a string with classifications produced automatically by the
-- argmatcher.  word_classifier is also filled in with explicit classifications
-- that supersede the implicit classifications.
function clink._parse_word_types(line_state, word_classifier)
    local parsed_word_types = {}

    local word_count = line_state:getwordcount()
    local first_word = line_state:getword(1) or ""
    if word_count > 1 or string.len(first_word) > 0 then
        if string.len(os.getalias(first_word) or "") > 0 then
            table.insert(parsed_word_types, "d"); --doskey
        elseif clink.is_cmd_command(first_word) then
            table.insert(parsed_word_types, "c"); --command
        else
            table.insert(parsed_word_types, "o"); --other
        end
    end

    local argmatcher = _find_argmatcher(line_state)
    if argmatcher then
        local reader = _argreader(argmatcher)
        reader._line_state = line_state
        reader._word_classifier = word_classifier
        reader._word_types = parsed_word_types

        -- Consume words and use them to move through matchers' arguments.
        for word_index = 2, word_count do
            local word = line_state:getword(word_index)
            reader:update(word, word_index)
        end
    end

    local s = ""
    for _, t in ipairs(parsed_word_types) do
        s = s..t
    end

    return s
end



--------------------------------------------------------------------------------
local argmatcher_generator = clink.generator(24)

--------------------------------------------------------------------------------
function argmatcher_generator:generate(line_state, match_builder)
    local argmatcher = _find_argmatcher(line_state)
    if argmatcher then
        return argmatcher:_generate(line_state, match_builder)
    end

    return false
end

--------------------------------------------------------------------------------
function argmatcher_generator:getwordbreakinfo(line_state)
    local argmatcher = _find_argmatcher(line_state)
    if argmatcher then
        local reader = _argreader(argmatcher)

        -- Consume words and use them to move through matchers' arguments.
        local word_count = line_state:getwordcount()
        for word_index = 2, (word_count - 1) do
            local word = line_state:getword(word_index)
            reader:update(word, word_index)
        end

        -- There should always be a matcher left on the stack, but the arg_index
        -- could be well out of range.
        argmatcher = reader._matcher
        if argmatcher and argmatcher._flags then
            local word = line_state:getendword()
            if argmatcher:_is_flag(word) then
                return 0, 1
            end
        end
    end

    return 0
end



--------------------------------------------------------------------------------
clink.arg = clink.arg or {}

--------------------------------------------------------------------------------
local function starts_with_flag_character(parser, part)
    if part == nil then
        return false
    end

    local prefix = part:sub(1, 1)
    return parser._flagprefix[prefix] and true or false
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
--- -arg:   ...
--- -ret:   table
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
--- -deprecated: clink.argmatcher
--- Creates a new parser and adds <span class="arg">...</span> to it.
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
--- -arg:   cmd:string
--- -arg:   parser:table
--- -ret:   table
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
--- -deprecated: clink.argmatcher
--- Adds <span class="arg">parser</span> to the first argmatcher for
--- <span class="arg">cmd</span>.  This behaves similarly to v0.4.8, but not
--- identically.  The Clink schema has changed significantly enough that there
--- is no direct 1:1 translation.  Calling
--- <code>clink.arg.register_parser</code> repeatedly with the same command to
--- merge parsers is not supported anymore.
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
