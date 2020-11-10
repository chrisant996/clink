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
local _argreader = {}
_argreader.__index = _argreader
setmetatable(_argreader, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argreader._new(root)
    local reader = setmetatable({
        _matcher = root,
        _arg_index = 1,
        _stack = {},
    }, _argreader)
    return reader
end

--------------------------------------------------------------------------------
function _argreader:update(word)
    -- Check for flags and switch matcher if the word is a flag.
    local matcher = self._matcher
    local is_flag = matcher:_is_flag(word)
    if is_flag then
        if matcher._flags then
            self:_push(matcher._flags)
        else
            return
        end
    end

    matcher = self._matcher
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]

    arg_index = arg_index + 1

    -- If arg_index is out of bounds we should loop if set or return to the
    -- previous matcher if possible.
    if arg_index > #matcher._args then
        if matcher._loop then
            self._arg_index = math.min(math.max(matcher._loop, 1), #matcher._args)
        elseif not self:_pop() then
            self._arg_index = arg_index
        end
    else
        self._arg_index = arg_index
    end

    -- Some matchers have no args at all.
    if not arg then
        return
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
local _argmatcher = {}
_argmatcher.__index = _argmatcher
setmetatable(_argmatcher, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argmatcher._new()
    local matcher = setmetatable({
        _args = {},
    }, _argmatcher)
    matcher:setflagprefix("-")
    return matcher
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addarg
--- -arg:   choices...:string
--- -ret:   self
function _argmatcher:addarg(...)
    local list = { _links = {} }
    self:_add(list, {...})
    table.insert(self._args, list)
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:addflags
--- -arg:   flags...:string
--- -ret:   self
function _argmatcher:addflags(...)
    local flag_matcher = self._flags or _argmatcher()
    local list = flag_matcher._args[1] or { _links = {} }
    flag_matcher:_add(list, {...})

    flag_matcher._args[1] = list
    self._flags = flag_matcher
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:loop
--- -arg:   [index:integer]
--- -ret:   self
function _argmatcher:loop(index)
    self._loop = index or -1
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:setflagprefix
--- -arg:   [prefixes...:string]
--- -ret:   self
function _argmatcher:setflagprefix(...)
    for _, i in ipairs({...}) do
        if type(i) ~= "string" or #i ~= 1 then
            error("Flag prefixes must be single character strings", 2)
        end
    end

    self._flagprefix = {...}
    return self
end

--------------------------------------------------------------------------------
--- -name:  _argmatcher:nofiles
--- -ret:   self
function _argmatcher:nofiles()
    self._no_file_generation = true
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

        return self:_is_flag(tostring(x))
    end

    if is_flag(arg[1]) then
        return self:addflags(table.unpack(arg))
    end

    return self:addarg(table.unpack(arg))
end

--------------------------------------------------------------------------------
function _argmatcher:_is_flag(word)
    local first_char = word:sub(1, 1)
    for _, i in ipairs(self._flagprefix) do
        if first_char == i then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function _argmatcher:_add(list, addee)
    -- Flatten out tables unless the table is a link
    local is_link = (getmetatable(addee) == _arglink)
    if type(addee) == "table" and not is_link and not addee.match then
        if getmetatable(addee) == _argmatcher then
            for _, i in ipairs(addee._args) do
                for _, j in ipairs(i) do
                    table.insert(list, j)
                end
                if i._links then
                    for k, m in pairs(i._links) do
                        list._links[k] = m
                    end
                end
            end
        else
            for _, i in ipairs(addee) do
                self:_add(list, i)
            end
        end
        return
    end

    if is_link then
        list._links[addee._key] = addee._matcher
    else
        table.insert(list, addee)
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_generate(line_state, match_builder)
    local reader = _argreader(self)

    -- Consume words and use them to move through matchers' arguments.
    local word_count = line_state:getwordcount()
    for word_index = 2, (word_count - 1) do
        local word = line_state:getword(word_index)
        reader:update(word)
    end

    -- There should always be a matcher left on the stack, but the arg_index
    -- could be well out of range.
    local matcher = reader._matcher
    local arg_index = reader._arg_index

    -- Are we left with a valid argument that can provide matches?
    local add_matches = function(arg)
        for key, _ in pairs(arg._links) do
            match_builder:addmatch(key)
        end

        for _, i in ipairs(arg) do
            if type(i) == "function" then
                local j = i(line_state:getendword(), word_count, line_state, match_builder)
                if type(j) ~= "table" then
                    return j or false
                end

                match_builder:addmatches(j)
            else
                match_builder:addmatch(i)
            end
        end

        return true
    end

    -- Select between adding flags or matches themselves. Works in conjunction
    -- with getprefixlength()'s return.
    if matcher._flags and matcher:_is_flag(line_state:getendword()) then
        match_builder:setprefixincluded()
        add_matches(matcher._flags._args[1])
    else
        local arg = matcher._args[arg_index]
        if arg then
            return add_matches(arg) and true or false
        end
    end

    -- No valid argument. Decide if we should match files or not.
    local no_files = matcher._no_file_generation or #matcher._args == 0
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
function clink.argmatcher(...)
    local matcher = _argmatcher()

    -- Extract priority from the arguments.
    matcher._priority = 999
    local input = {...}
    if #input > 0 and type(input[1]) == "number" then
        matcher._priority = input[1]
        table.remove(input, 1)
    end

    -- Register the argmatcher
    for _, i in ipairs(input) do
        _argmatchers[i:lower()] = matcher
    end

    return matcher
end



--------------------------------------------------------------------------------
local function _find_argmatcher(line_state)
    -- Running an argmatcher only makes sense if there's two or more words.
    if line_state:getwordcount() < 2 then
        return
    end

    local first_word = line_state:getword(1)

    -- Search for a valid argmatcher and return it.
    local argmatcher_keys = {
        path.getname(first_word):lower(),
        path.getbasename(first_word):lower(),
    }

    for _, key in ipairs(argmatcher_keys) do
        local argmatcher = _argmatchers[key]
        if argmatcher then
            return argmatcher
        end
    end
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
function argmatcher_generator:getprefixlength(line_state)
    local argmatcher = _find_argmatcher(line_state)
    local word = line_state:getendword()
    if argmatcher and argmatcher._flags and argmatcher:_is_flag(word) then
        return 1
    end

    return 0
end



--------------------------------------------------------------------------------
clink.arg = clink.arg or {}

--------------------------------------------------------------------------------
--- -name:  clink.arg.new_parser
--- -arg:   ...
--- -ret:   table
--- -show:  -- Deprecated form:
--- -show:  local parser = clink.arg.new_parser("abc", "def")<br/>
--- -show:  -- Replace with form:
--- -show:  local parser = clink.argmatcher():addarg("abc", "def")
--- -deprecated: clink.argmatcher
--- Creates a new parser and adds <em>...</em> to it.
function clink.arg.new_parser(...)
    local p = clink.argmatcher():addarg(...)
    return p
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
--- -show:  clink.arg.register_parser("foo", parser2)<br/>
--- -show:  -- Replace with new form:
--- -show:  clink.argmatcher("foo"):addarg(parser1, parser2)<br/>
--- -show:  -- Warning:  Note that the following are NOT the same as above!
--- -show:  -- This replaces parser1 with parser2:
--- -show:  clink.argmatcher("foo"):addarg(parser1)
--- -show:  clink.argmatcher("foo"):addarg(parser2)
--- -show:  -- This uses only parser2 if/when parser1 finishes parsing args:
--- -show:  clink.argmatcher("foo"):addarg(parser1):addarg(parser2)
--- -deprecated: clink.argmatcher
--- Adds <em>parser</em> to the first argmatcher for <em>cmd</em>.  This behaves
--- similarly to v0.4.8, but not identically.  The Clink schema has changed
--- significantly enough that there is no direct 1:1 translation.
function clink.arg.register_parser(cmd, parser)
    local matcher = _argmatchers[cmd] or clink.argmatcher(cmd)
    local list = matcher._args[1] or { _links = {} }
    matcher:_add(list, parser)
    matcher._args[1] = list
    return matcher
end
